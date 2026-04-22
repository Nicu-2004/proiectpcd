#include <stdio.h>      /* printf, perror, fgets, fflush */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strcmp, strlen, strcspn, memset */
#include <unistd.h>     /* close */
#include <sys/types.h>  /* tipuri socket */
#include <sys/socket.h> /* socket, connect, send, recv */
#include <netinet/in.h> /* struct sockaddr_in, htons */
#include <arpa/inet.h>  /* inet_addr */

/* Folosim enum pentru a evita erorile de tip 'magic number' si 'macro-to-enum' */
enum Configurare {
    PORT_SERVER = 9090,
    DIM_BUFFER  = 4096,
    DIM_SELECTIE = 16
};

/* ── afiseaza_meniu ────────────────────────────────────────────────────────── */

void afiseaza_meniu(void) {
    (void)printf("\n=== MENIU ===\n");
    (void)printf("1. Corecteaza test (introduce calea imaginii)\n");
    (void)printf("2. Iesire\n");
    (void)printf("Selectie: ");
    (void)fflush(stdout); 
}

/* ── trimite_imagine ────────────────────────────────────────────────────────── */

void proceseaza_optiunea_unu(int socket_comunicare) {
    char cale_imagine[DIM_BUFFER];
    char mesaj_server[DIM_BUFFER];
    char raspuns_server[DIM_BUFFER];

    (void)printf("Calea imaginii: ");
    (void)fflush(stdout);

    if (fgets(cale_imagine, DIM_BUFFER, stdin) != NULL) {
        cale_imagine[strcspn(cale_imagine, "\n")] = '\0';

        if (strlen(cale_imagine) == 0) {
            (void)printf("[ERR] Calea nu poate fi goala.\n");
            return;
        }

        /* Construim mesajul folosind snprintf (mai sigur si trece de clang-tidy) */
        (void)snprintf(mesaj_server, DIM_BUFFER, "grade:%s", cale_imagine); 

        (void)printf("[Client] Trimit catre server: '%s'\n", mesaj_server);
        (void)send(socket_comunicare, mesaj_server, strlen(mesaj_server), 0);

        (void)memset(raspuns_server, 0, DIM_BUFFER); 
        
        /* Folosim ssize_t pentru a evita narrowing conversion */
        ssize_t octeti_primiti = recv(socket_comunicare, raspuns_server, (size_t)DIM_BUFFER - 1, 0);
        
        if (octeti_primiti <= 0) {
            (void)printf("[ERR] Serverul s-a deconectat.\n");
        } else {
            (void)printf("\n[Rezultat] %s\n", raspuns_server);
        }
    }
}

/* ── Main ────────────────────────────────────────────────────────────────────── */

int main(void) {
    int descriptor_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor_socket < 0) {
        perror("[ERR] socket");
        return 1;
    }

    struct sockaddr_in adresa_server;
    (void)memset(&adresa_server, 0, sizeof(adresa_server)); 
    
    adresa_server.sin_family = AF_INET;
    adresa_server.sin_port = htons(PORT_SERVER);
    adresa_server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(descriptor_socket, (const struct sockaddr *)&adresa_server, (socklen_t)sizeof(adresa_server)) < 0) {
        perror("[ERR] connect - serverul nu e pornit?");
        (void)close(descriptor_socket);
        return 1;
    }

    (void)printf("[GradingApp] Conectat la server pe portul %d\n", PORT_SERVER);

    char buffer_selectie[DIM_SELECTIE];

    while (1) {
        afiseaza_meniu();

        if (fgets(buffer_selectie, DIM_SELECTIE, stdin) == NULL) {
            break; 
        }
        buffer_selectie[strcspn(buffer_selectie, "\n")] = '\0';

        if (strcmp(buffer_selectie, "1") == 0) {
            proceseaza_optiunea_unu(descriptor_socket);
        } else if (strcmp(buffer_selectie, "2") == 0) {
            (void)send(descriptor_socket, "quit", 4, 0);
            (void)printf("[Client] Conexiune inchisa.\n");
            break;
        } else {
            (void)printf("[ERR] Selectie invalida. Alege 1 sau 2.\n");
        }
    }

    (void)close(descriptor_socket);
    return 0;
}
