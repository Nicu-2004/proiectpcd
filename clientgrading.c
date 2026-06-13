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

typedef struct {
    char magic[4];
    uint32_t file_size;
} app_header_t;

enum ConfigClient {
    PORT_SERVER        = 9090,
    DIM_BUFFER         = 4096,
    INVALID_DESCRIPTOR = -1,
    STATUS_SUCCESS     = 0,
    STATUS_FAILURE     = 1
};

int main(void) {
    char cale_fisier[DIM_BUFFER];
    int file_fd = INVALID_DESCRIPTOR;

    while (1) {
        printf("Introdu calea imaginii OMR pentru corectare (ex: photo.png): ");
        fflush(stdout);

        if (fgets(cale_fisier, DIM_BUFFER, stdin) == NULL) return STATUS_FAILURE;
        cale_fisier[strcspn(cale_fisier, "\n")] = '\0'; 
        
        if (strcmp(cale_fisier, "anuleaza") == 0) {
            printf("[Client] Operare anulata cu succes. Programul se inchide.\n");
            return STATUS_SUCCESS;
        }

        file_fd = open(cale_fisier, O_RDONLY);
        if (file_fd < 0) {
            printf("[ERR] Nu am putut gasi fisierul '%s'. Incearca din nou (sau scrie 'anuleaza').\n\n", cale_fisier);
            continue; 
        }
        break; 
    }

    struct stat stat_buf;
    if (fstat(file_fd, &stat_buf) < 0) {
        perror("[ERR] Eroare la citirea dimensiunii");
        close(file_fd); return STATUS_FAILURE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[ERR] socket");
        close(file_fd); return STATUS_FAILURE;
    }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(PORT_SERVER);
    if (inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr) <= 0) {
        perror("[ERR] inet_pton");
        close(sock); close(file_fd); return STATUS_FAILURE;
    }

    printf("[Client] Ma conectez la serverul 127.0.0.1:%d...\n", PORT_SERVER);
    if (connect(sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("[ERR] Conexiune esuata. Verifica daca serverul este pornit.");
        close(sock); close(file_fd); return STATUS_FAILURE;
    }

    app_header_t header;
    strncpy(header.magic, "OMR", 4);
    header.file_size = htonl((uint32_t)stat_buf.st_size);

    printf("[Client] Trimit imaginea de %u octeti catre server...\n", (unsigned int)stat_buf.st_size);
    send(sock, &header, sizeof(app_header_t), 0);
    
    off_t offset = 0;
    sendfile(sock, file_fd, &offset, (size_t)stat_buf.st_size);
    close(file_fd);

    printf("[Client] Fisier trimis complet! Astept sincron corectarea de la server...\n");

    // =========================================================
    // 1. RECEPTIA TEXTULUI (NOTA)
    // =========================================================
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

        // =========================================================
        // 2. RECEPTIA FISIERULUI BINAR (POZA CORECTATA)
        // =========================================================
        uint32_t net_size = 0;
        if (recv(sock, &net_size, sizeof(net_size), MSG_WAITALL) == sizeof(net_size)) {
            uint32_t image_size = ntohl(net_size);
            if (image_size > 0) {
                printf("[Client] Primesc dovada corectarii (%u octeti)...\n", image_size);
                int out_fd = open("rezultat_corectat.png", O_WRONLY | O_CREAT | O_TRUNC, 0600);
                
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
                    printf("[Client] 📸 Poza a fost salvata in folderul curent sub numele 'rezultat_corectat.png'!\n");
                }
            }
        }
    }

    close(sock);
    printf("[Client] Interactiune incheiata cu succes.\n");
    return STATUS_SUCCESS;
}
