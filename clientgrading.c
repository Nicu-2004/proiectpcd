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

// Funcții sigure înlocuitoare
static void safe_memset(void *ptr, int value, size_t n) {
    unsigned char *p = (unsigned char*)ptr;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)value;
}

static void safe_strcpy(char *dest, size_t dest_size, const char *src) {
    if (dest_size == 0) return;
    size_t i;
    for (i = 0; i < dest_size - 1 && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static void safe_close(int fd) {
    if (fd != INVALID_DESCRIPTOR) {
        if (close(fd) == INVALID_DESCRIPTOR) {
            perror("[WARN] Eroare inchidere descriptor");
        }
    }
}

int main(void) {
    char cale_fisier[DIM_BUFFER];
    printf("Introdu calea imaginii OMR pentru corectare (ex: photo.png): ");
    fflush(stdout);

    if (fgets(cale_fisier, DIM_BUFFER, stdin) == NULL) {
        return STATUS_FAILURE;
    }
    cale_fisier[strcspn(cale_fisier, "\n")] = '\0';

    int file_fd = open(cale_fisier, O_RDONLY);
    if (file_fd < 0) {
        perror("[ERR] Nu am putut deschide fisierul specificat");
        return STATUS_FAILURE;
    }

    struct stat stat_buf;
    if (fstat(file_fd, &stat_buf) < 0) {
        perror("[ERR] Eroare la citirea dimensiunii fisierului");
        safe_close(file_fd);
        return STATUS_FAILURE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[ERR] socket");
        safe_close(file_fd);
        return STATUS_FAILURE;
    }

    struct sockaddr_in srv_addr;
    safe_memset(&srv_addr, 0, sizeof(struct sockaddr_in));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(PORT_SERVER);
    if (inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr) <= 0) {
        perror("[ERR] inet_pton");
        safe_close(sock);
        safe_close(file_fd);
        return STATUS_FAILURE;
    }

    printf("[Client] Ma conectez la serverul %s:%d...\n", "127.0.0.1", PORT_SERVER);
    if (connect(sock, (struct sockaddr*)&srv_addr, (socklen_t)sizeof(srv_addr)) < 0) {
        perror("[ERR] Conexiune esuata");
        safe_close(sock);
        safe_close(file_fd);
        return STATUS_FAILURE;
    }

    app_header_t header;
    safe_strcpy(header.magic, sizeof(header.magic), "OMR");
    header.file_size = htonl((uint32_t)stat_buf.st_size);

    printf("[Client] Trimit Header: %u octeti pregatiti...\n", (unsigned int)stat_buf.st_size);
    if (send(sock, &header, sizeof(app_header_t), 0) < 0) {
        perror("[ERR] Eroare trimitere header");
        safe_close(sock);
        safe_close(file_fd);
        return STATUS_FAILURE;
    }

    printf("[Client] Trimit continutul binar (zero-copy sendfile)...\n");
    off_t offset = 0;
    ssize_t trimisi = sendfile(sock, file_fd, &offset, (size_t)stat_buf.st_size);
    if (trimisi != (ssize_t)stat_buf.st_size) {
        perror("[ERR] Eroare la transferul sendfile");
        safe_close(sock);
        safe_close(file_fd);
        return STATUS_FAILURE;
    }
    safe_close(file_fd);

    printf("[Client] Fisier trimis complet! Astept sincron corectarea de la server...\n");
    char raspuns[DIM_BUFFER];
    safe_memset(raspuns, 0, DIM_BUFFER);

    ssize_t rres = recv(sock, raspuns, DIM_BUFFER - 1, 0);
    if (rres <= 0) {
        printf("[ERR] Serverul a inchis conexiunea neasteptat.\n");
    } else {
        raspuns[rres] = '\0';
        printf("\n========================================\n");
        printf("[REZULTAT PRIMIT] %s\n", raspuns);
        printf("========================================\n");
    }

    safe_close(sock);
    printf("[Client] Interactiune incheiata cu succes. Socket inchis.\n");
    return STATUS_SUCCESS;
}