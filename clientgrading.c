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

// Headerul aplicatiei noastre pentru organizarea chunk-urilor TCP
typedef struct {
    char magic[4];      // Identificator de protocol: "OMR\0" 
    uint32_t file_size; // Marimea fisierului in octeti
} app_header_t;

enum ConfigClient {
    PORT_SERVER        = 9090,
    DIM_BUFFER         = 4096,
    INVALID_DESCRIPTOR = -1,
    STATUS_SUCCESS     = 0,
    STATUS_FAILURE     = 1
};

// Functie de safeclose
static void safe_close(int fd) {
    if (fd != INVALID_DESCRIPTOR) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (close(fd) == INVALID_DESCRIPTOR) {
            perror("[WARN] Eroare inchidere descriptor");
        }
    }
}

int main(void) {
    // Afisam mesaj la inceput
    char cale_fisier[DIM_BUFFER];
    printf("Introdu calea imaginii OMR pentru corectare (ex: photo.png): ");
    fflush(stdout);

    // Preluarea mesajului de la tastatura si transformand \n in \0
    if (fgets(cale_fisier, DIM_BUFFER, stdin) == NULL) { return STATUS_FAILURE; }
    cale_fisier[strcspn(cale_fisier, "\n")] = '\0';

    // Setare file descriptor al fisierului si afisare eroare daca nu sa putut deschide fisierul
    int file_fd = open(cale_fisier, O_RDONLY);
    if (file_fd < 0) {
        perror("[ERR] Nu am putut deschide fisierul specificat");
        return STATUS_FAILURE;
    }

    // Salvarea in stat_buf datele fisierului in format stat
    struct stat stat_buf;
    if (fstat(file_fd, &stat_buf) < 0) {
        perror("[ERR] Eroare la citirea dimensiunii fisierului");
        safe_close(file_fd);
        return STATUS_FAILURE;
    }

    // Creare socket INET cu format TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("[ERR] socket"); safe_close(file_fd); return STATUS_FAILURE; }

    // Setare adresa si port, htons pentru format standard short
    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(struct sockaddr_in));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(PORT_SERVER);
    if (inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr) <= 0) {
        perror("[ERR] inet_pton"); safe_close(sock); safe_close(file_fd); return STATUS_FAILURE;
    }

    // 3 way handshake intre client si server
    printf("[Client] Ma conectez la serverul %s:%d...\n", "127.0.0.1", PORT_SERVER);
    if (connect(sock, (struct sockaddr*)&srv_addr, (socklen_t)sizeof(srv_addr)) < 0) {
        perror("[ERR] Conexiune esuata"); safe_close(sock); safe_close(file_fd); return STATUS_FAILURE;
    }

    // Definim si trimitem HEADER-UL aplicatiei, htonl  pentru format standard long
    app_header_t header;
    strncpy(header.magic, "OMR", 4);
    header.file_size = htonl((uint32_t)stat_buf.st_size); 

    // Trimitere header spre server
    printf("[Client] Trimit Header: %u octeti pregatiti...\n", (unsigned int)stat_buf.st_size);
    if (send(sock, &header, sizeof(app_header_t), 0) < 0) {
        perror("[ERR] Eroare trimitere header"); safe_close(sock); safe_close(file_fd); return STATUS_FAILURE;
    }

    // Trimitem fisierul serverului
    printf("[Client] Trimit continutul binar (zero-copy sendfile)...\n");
    off_t offset = 0;
    ssize_t trimisi = sendfile(sock, file_fd, &offset, (size_t)stat_buf.st_size);
    
    if (trimisi != (ssize_t)stat_buf.st_size) {
        perror("[ERR] Eroare la transferul sendfile"); safe_close(sock); safe_close(file_fd); return STATUS_FAILURE;
    }
    safe_close(file_fd); // Fisierul sursa nu mai e necesar

    // Raspunsul de la server cu raspunsul folosind comanda recv
    printf("[Client] Fisier trimis complet! Astept sincron corectarea de la server...\n");
    char raspuns[DIM_BUFFER];
    memset(raspuns, 0, DIM_BUFFER) 

    ssize_t rres = recv(sock, raspuns, DIM_BUFFER - 1, 0);
    if (rres <= 0) {
        printf("[ERR] Serverul a inchis conexiunea neasteptat.\n");
    } else {
        raspuns[rres] = '\0';
        printf("\n========================================\n");
        printf("[REZULTAT PRIMIT] %s\n", raspuns);
        printf("========================================\n");
    }

    // Inchidem socketul cand am terminat
    safe_close(sock);
    printf("[Client] Interactiune incheiata cu succes. Socket inchis.\n");
    return STATUS_SUCCESS;
}
