#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Headerul trimis la server
typedef struct {
    char magic[4];
    uint32_t file_size;
    char barem[40]; 
} app_header_t;

enum ConfigClient {
    PORT_SERVER        = 9090,
    DIM_BUFFER         = 4096,
    INVALID_DESCRIPTOR = -1,
    STATUS_SUCCESS     = 0,
    STATUS_FAILURE     = 1
};

void elimina_newline(char* str) {
    str[strcspn(str, "\r\n")] = '\0';
}

// Citirea fisierului de barem
int incarca_barem_din_fisier(const char* cale_fisier, char* barem_out) {
    FILE* f = fopen(cale_fisier, "r");
    if (!f) return -1;
    char linie[64];
    int gasite = 0;
    while (fgets(linie, sizeof(linie), f)) {
        int nr_intrebare;
        char raspuns_corect;
        if (sscanf(linie, "%d: %c", &nr_intrebare, &raspuns_corect) == 2) {
            if (nr_intrebare >= 1 && nr_intrebare <= 40) {
                barem_out[nr_intrebare - 1] = raspuns_corect;
                gasite++;
            }
        }
    }
    fclose(f);
    return (gasite == 40) ? 0 : -1;
}

int main(void) {
    char cale_barem[DIM_BUFFER];
    char cale_fisier[DIM_BUFFER];
    char nume_iesire[DIM_BUFFER];
    char barem_memorie[40];
    char optiune[10];

    printf("===========================================\n");
    printf("      SISTEM OMR - CLIENT EVALUARE\n");
    printf("===========================================\n");

    while (1) {
        printf("\n--- MENIU PRINCIPAL ---\n");
        printf("1. Evalueaza poza (Trimitere test catre server)\n");
        printf("0. Anuleaza aplicatia (Iesire)\n");
        printf("-- Alege o optiune: ");
        fflush(stdout);

        if (fgets(optiune, sizeof(optiune), stdin) == NULL) break;
        elimina_newline(optiune);

        if (strcmp(optiune, "0") == 0) {
            printf("[Client] Aplicatie anulata / oprita cu succes.\n");
            break;
        } else if (strcmp(optiune, "1") == 0) {
            // 1. CERE BAREMUL
            printf("\nIntrodu calea catre fisierul barem (ex: barem.txt): ");
            fflush(stdout);
            if (fgets(cale_barem, DIM_BUFFER, stdin) == NULL) break;
            elimina_newline(cale_barem);

            if (incarca_barem_din_fisier(cale_barem, barem_memorie) < 0) {
                printf("[ERR] Fisierul '%s' nu exista sau nu contine 40 de raspunsuri valide!\n", cale_barem);
                continue;
            }

            // 2. CERE POZA NECORECTATĂ
            printf("Introdu calea imaginii OMR (ex: photo.png): ");
            fflush(stdout);
            if (fgets(cale_fisier, DIM_BUFFER, stdin) == NULL) break;
            elimina_newline(cale_fisier);

            int file_fd = open(cale_fisier, O_RDONLY);
            if (file_fd < 0) {
                printf("[ERR] Nu am putut gasi imaginea '%s'.\n", cale_fisier);
                continue;
            }

            struct stat stat_buf;
            if (fstat(file_fd, &stat_buf) < 0) {
                perror("[ERR] Eroare citire dimensiune imagine");
                close(file_fd); continue;
            }

            // 3. CERE NUMELE REZULTATULUI
            printf("Introdu numele pentru poza corectata care se va salva (ex: rez_popescu.png): ");
            fflush(stdout);
            if (fgets(nume_iesire, DIM_BUFFER, stdin) == NULL) { close(file_fd); break; }
            elimina_newline(nume_iesire);

            // Incepe comunicarea cu serverul
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) { perror("[ERR] socket"); close(file_fd); continue; }

            struct sockaddr_in srv_addr;
            memset(&srv_addr, 0, sizeof(srv_addr));
            srv_addr.sin_family = AF_INET;
            srv_addr.sin_port   = htons(PORT_SERVER);
            inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

            printf("[Client] Ma conectez la serverul 127.0.0.1:%d...\n", PORT_SERVER);
            if (connect(sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
                perror("[ERR] Conexiune esuata. Verifica daca serverul este pornit.");
                close(sock); close(file_fd); continue;
            }

            // Pregătirea pachetului care va fi trimis spre server
            app_header_t header;
            strncpy(header.magic, "OMR", 4);
            header.file_size = htonl((uint32_t)stat_buf.st_size);
            memcpy(header.barem, barem_memorie, 40); 

            printf("[Client] Trimit pachetul (Barem + Imagine %u octeti)...\n", (unsigned int)stat_buf.st_size);
            send(sock, &header, sizeof(app_header_t), 0);
            
            off_t offset = 0;
            sendfile(sock, file_fd, &offset, (size_t)stat_buf.st_size);
            close(file_fd);

            printf("[Client] Fisier trimis complet! Astept sincron corectarea de la server...\n");

            //Primirea rezultatelor
            char raspuns_text[256];
            memset(raspuns_text, 0, 256);
            ssize_t rres = recv(sock, raspuns_text, 256, MSG_WAITALL);
            
            if (rres <= 0) {
                printf("[ERR] Serverul a inchis conexiunea neasteptat.\n");
            } else {
                raspuns_text[255] = '\0';
                printf("\n========================================\n");
                printf("[REZULTAT PRIMIT] %s\n", raspuns_text);
                printf("========================================\n");

                uint32_t net_size = 0;
                if (recv(sock, &net_size, sizeof(net_size), MSG_WAITALL) == sizeof(net_size)) {
                    uint32_t image_size = ntohl(net_size);
                    if (image_size > 0) {
                        printf("[Client] Primesc dovada de corectare (%u octeti)...\n", image_size);
                        int out_fd = open(nume_iesire, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                        if (out_fd >= 0) {
                            uint32_t ramasi = image_size;
                            char buf[DIM_BUFFER];
                            while (ramasi > 0) {
                                size_t de_citit = (ramasi > DIM_BUFFER) ? DIM_BUFFER : ramasi;
                                ssize_t cititi = recv(sock, buf, de_citit, 0);
                                if (cititi <= 0) break;
                                write(out_fd, buf, cititi);
                                ramasi -= cititi;
                            }
                            close(out_fd);
                            printf("[Client] Poza a fost salvata sub numele '%s'!\n", nume_iesire);
                        }
                    }
                }
            }
            close(sock);
        } else {
            printf("[Client] Optiune invalida. Incearca din nou.\n");
        }
    }
    return STATUS_SUCCESS;
}
