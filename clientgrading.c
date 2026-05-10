#include <stdio.h>      /* printf, perror, fgets, fflush */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strcmp, strlen, strcspn */
#include <unistd.h>     /* close */
#include <sys/socket.h> /* socket, connect, send, recv */
#include <netinet/in.h> /* struct sockaddr_in, htons */
#include <arpa/inet.h>  /* inet_pton */

enum Configurare {
    PORT_SERVER        = 9090,
    DIM_BUFFER         = 4096,
    DIM_SELECTIE       = 16,
    QUIT_MSG_LEN       = 4,
    EXIT_STATUS_OK     = 0,
    EXIT_STATUS_ERR    = 1,
    INVALID_DESCRIPTOR = -1
};

static void safe_close(int descriptor) {
    if (descriptor != INVALID_DESCRIPTOR) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (close(descriptor) == INVALID_DESCRIPTOR) {
            perror("[ERR] close");
        }
    }
}

void afiseaza_meniu(void) {
    (void)printf("\n=== MENIU ===\n");
    (void)printf("1. Corecteaza test (introduce calea imaginii)\n");
    (void)printf("2. Iesire\n");
    (void)printf("Selectie: ");
    (void)fflush(stdout); 
}

void proceseaza_optiunea_unu(int socket_comunicare) {
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

        size_t index_mesaj = 0;
        const char *prefix_comanda = "grade:";

        while (prefix_comanda[index_mesaj] != '\0' && index_mesaj < (size_t)(DIM_BUFFER - 1)) {
            mesaj_server[index_mesaj] = prefix_comanda[index_mesaj];
            index_mesaj++;
        }

        size_t index_cale = 0;
        while (cale_imagine[index_cale] != '\0' && index_mesaj < (size_t)(DIM_BUFFER - 1)) {
            mesaj_server[index_mesaj] = cale_imagine[index_cale];
            index_mesaj++;
            index_cale++;
        }
        mesaj_server[index_mesaj] = '\0';
        
        (void)printf("[Client] Trimit catre server: '%s'\n", mesaj_server);
        
        ssize_t octeti_trimisi = send(socket_comunicare, mesaj_server, strlen(mesaj_server), 0);
        if (octeti_trimisi < 0) {
            perror("[ERR] send");
            return;
        }
        
        ssize_t octeti_primiti = recv(socket_comunicare, raspuns_server, (size_t)(DIM_BUFFER - 1), 0);
        
        if (octeti_primiti <= 0) {
            (void)printf("[ERR] Serverul s-a deconectat.\n");
        } else {
            raspuns_server[octeti_primiti] = '\0';
            (void)printf("\n[Rezultat] %s\n", raspuns_server);
        }
    }
}

int main(void) {
    int descriptor_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor_socket < 0) {
        perror("[ERR] socket");
        return EXIT_STATUS_ERR;
    }

    struct sockaddr_in adresa_server = {0};
    adresa_server.sin_family = AF_INET;
    adresa_server.sin_port   = htons(PORT_SERVER);

    // Folosim inet_pton in loc de inet_addr pentru siguranta maxima si conformitate POSIX
    if (inet_pton(AF_INET, "127.0.0.1", &adresa_server.sin_addr) <= 0) {
        perror("[ERR] inet_pton");
        safe_close(descriptor_socket);
        return EXIT_STATUS_ERR;
    }

    if (connect(descriptor_socket, (const struct sockaddr *)&adresa_server, (socklen_t)sizeof(adresa_server)) < 0) {
        perror("[ERR] connect - serverul nu e pornit?");
        safe_close(descriptor_socket);
        return EXIT_STATUS_ERR;
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
            ssize_t res_send = send(descriptor_socket, "quit", (size_t)QUIT_MSG_LEN, 0);
            if (res_send < 0) {
                perror("[ERR] send quit");
            }
            (void)printf("[Client] Conexiune inchisa.\n");
            break;
        } else {
            (void)printf("[ERR] Selectie invalida. Alege 1 sau 2.\n");
        }
    }
    
    safe_close(descriptor_socket);
    return EXIT_STATUS_OK;
}
