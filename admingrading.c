#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

// Variabile pentru a seta dimensiune sau pentru verificare status
enum ConfigAdmin {
    DIM_BUFFER         = 4096,
    DIM_SELECTIE       = 16,
    INVALID_DESCRIPTOR = -1,
    STATUS_SUCCESS     = 0,
    STATUS_FAILURE     = 1
};

// Adresa socket-ului local
static const char* UNIX_SOCKET_PATH = "/tmp/omr_admin.sock";

// Functie de safe close daca mai exista o conexiune
static void safe_close(int fd) {
    if (fd != INVALID_DESCRIPTOR) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (close(fd) == INVALID_DESCRIPTOR) {
            perror("[WARN] Eroare inchidere descriptor");
        }
    }
}

// Meniu de optiuni pentru administrator
static void afiseaza_meniu_admin(void) {
    printf("\n================ PANOU ADMINISTRATOR (Nivel A) ================\n");
    printf("1. Raport: Număr clienți INET conectați simultan\n");
    printf("2. Raport: Starea curentă a cozii de așteptare (FIFO)\n");
    printf("3. Raport: Sarcină aflată în execuție în fundal\n");
    printf("4. Raport: Timpul mediu de corectare per test (milisecunde)\n");
    printf("5. Raport: Istoricul complet al notelor acordate\n");
    printf("6. Raport: Rata globală de succes a citirii optice\n");
    printf("7. Deconectare voluntară\n");
    printf("===============================================================\n");
    printf("Selectie: ");
    fflush(stdout);
}

// Cerere raport catre server prin socket
static void cere_raport(int sock, const char* cod_raport) {
    ssize_t sres = send(sock, cod_raport, strlen(cod_raport), 0);
    if (sres < 0) { perror("[ERR] Trimitere esuata (timeout de inactivitate atins?)"); return; }

    char raspuns[DIM_BUFFER * 4];
    memset(raspuns, 0, sizeof(raspuns));
    ssize_t rres = recv(sock, raspuns, sizeof(raspuns) - 1, 0);
    
    if (rres <= 0) {
        printf("\n[ERR] Deconectat! Serverul a respins conexiunea sau ai atins limita de inactivitate (Timeout).\n");
        exit(STATUS_FAILURE);
    }
    
    raspuns[rres] = '\0';
    printf("\n[RAPORT PRIMIT]\n%s\n", raspuns);
}

int main(void) {
// Initializare socket unix in format TCP
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("[ERR] socket"); return STATUS_FAILURE; }

// Initializare structura unix pe variabila un_addr
    struct sockaddr_un un_addr;
    memset(&un_addr, 0, sizeof(struct sockaddr_un));
    // Setarea adresa locala a socketului
    un_addr.sun_family = AF_UNIX;
    strncpy(un_addr.sun_path, UNIX_SOCKET_PATH, sizeof(un_addr.sun_path) - 1);

    printf("[AdminApp] Se incearca blocarea exclusiva a serverului pe %s...\n", UNIX_SOCKET_PATH);
    if (connect(sock, (struct sockaddr*)&un_addr, (socklen_t)sizeof(struct sockaddr_un)) < 0) {
        printf("[ERR] Acces respins! Un alt administrator este conectat sau serverul este oprit.\n");
        safe_close(sock);
        return STATUS_FAILURE;
    }

    printf("[AdminApp] Conectat cu succes! Sesiunea detine control exclusiv.\n");
    char opt[DIM_SELECTIE];

    while (1) {
        afiseaza_meniu_admin();
        if (fgets(opt, DIM_SELECTIE, stdin) == NULL) { break; }
        opt[strcspn(opt, "\n")] = '\0';

        if      (strcmp(opt, "1") == 0) { cere_raport(sock, "report:clients"); }
        else if (strcmp(opt, "2") == 0) { cere_raport(sock, "report:queue"); }
        else if (strcmp(opt, "3") == 0) { cere_raport(sock, "report:current"); }
        else if (strcmp(opt, "4") == 0) { cere_raport(sock, "report:time"); }
        else if (strcmp(opt, "5") == 0) { cere_raport(sock, "report:history"); }
        else if (strcmp(opt, "6") == 0) { cere_raport(sock, "report:success"); }
        else if (strcmp(opt, "7") == 0) {
            ssize_t sres = send(sock, "quit", 4, 0);
            if (sres < 0) { perror("[WARN] send quit"); }
            printf("[AdminApp] Sesiune incheiata. Control exclusiv eliberat.\n");
            break;
        } else {
            printf("[ERR] Comanda invalida.\n");
        }
    }
    safe_close(sock);
    return STATUS_SUCCESS;
}
