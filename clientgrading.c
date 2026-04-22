#include <stdio.h>      /* printf, perror, fgets, fflush */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strcmp, strlen, strcspn */
#include <unistd.h>     /* close */
#include <sys/socket.h> /* socket, connect, send, recv */
#include <netinet/in.h> /* struct sockaddr_in, htons */
#include <arpa/inet.h>  /* inet_addr */

enum Configurare {
    PORT_SERVER = 9090,
    DIM_BUFFER  = 4096,
    DIM_SELECTIE = 16
};

void afiseaza_meniu(void) {
    (void)printf("\n=== MENIU ===\n");
    (void)printf("1. Corecteaza test (introduce calea imaginii)\n");
    (void)printf("2. Iesire\n");
    (void)printf("Selectie: ");
    (void)fflush(stdout); 
}

void proceseaza_optiunea_unu(int socket_comunicare) {
    /* Initializarea cu {0} inlocuieste memset si este safe pentru linter */
    char cale_imagine[DIM_BUFFER] = {0};
    char mesaj_server[DIM_BUFFER] = {0};
    char raspuns_server[DIM_BUFFER] = {0};

    (void)printf("Calea imaginii: ");
    (void)fflush(stdout);

    if (fgets(cale_imagine, DIM_BUFFER, stdin) != NULL) {
        cale_imagine[strcspn(cale_imagine, "\n")] = '\0';

        if (strlen(cale_imagine) == 0) {
            (void)printf("[ERR] Calea nu poate fi goala.\n");
            return;
        }

/* Construim mesajul manual, caracter cu caracter */
        size_t index_mesaj = 0;
        const char *prefix_comanda = "grade:";

        /* Copiem prefixul */
        while (prefix_comanda[index_mesaj] != '\0' && index_mesaj < (size_t)DIM_BUFFER - 1) {
            mesaj_server[index_mesaj] = prefix_comanda[index_mesaj];
            index_mesaj++;
        }

        /* Copiem calea imaginii */
        size_t index_cale = 0;
        while (cale_imagine[index_cale] != '\0' && index_mesaj < (size_t)DIM_BUFFER - 1) {
            mesaj_server[index_mesaj] = cale_imagine[index_cale];
            index_mesaj++;
            index_cale++;
        }
        mesaj_server[index_mesaj] = '\0';
        
        (void)printf("[Client] Trimit catre server: '%s'\n", mesaj_server);
        (void)send(socket_comunicare, mesaj_server, strlen(mesaj_server), 0);
        
        ssize_t octeti_primiti = recv(socket_comunicare, raspuns_server, (size_t)DIM_BUFFER - 1, 0);
        
        if (octeti_primiti <= 0) {
            (void)printf("[ERR] Serverul s-a deconectat.\n");
        } else {
            (void)printf("\n[Rezultat] %s\n", raspuns_server);
        }
    }
}

int main(void) {
    int descriptor_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor_socket < 0) {
        perror("[ERR] socket");
        return 1;
    }

    /* Initializare directa fara memset */
    struct sockaddr_in adresa_server = {0};
    
    adresa_server.sin_family = AF_INET;
    adresa_server.sin_port = htons(PORT_SERVER);
    adresa_server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(descriptor_socket, (const struct sockaddr *)&adresa_server, (socklen_t)sizeof(adresa_server)) < 0) {
        perror("[ERR] connect - serverul nu e pornit?");
        (void)close(descriptor_socket);
        return 1;
    }

    (void)printf("[GradingApp] Conectat la server pe portul %d\n", PORT_SERVER);

    char buffer_selectie[DIM_SELECTIE] = {0};

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
