/**
 * Miclau Nicolae
 * IR3 2026, subgrupa 7
 * Grading App - Client (schelet)
 *
 * Compilare: gcc -Wall -Wextra client_grading.c -o client_grading
 * Rulare:    ./client_grading
 *
 * Exemplu sesiune:
 *   [GradingApp] Conectat la server 127.0.0.1:9090
 *
 *   === MENIU ===
 *   1. Corecteaza test (introduce calea imaginii)
 *   2. Iesire
 *   Selectie: 1
 *   Calea imaginii: /home/student/test.png
 *
 *   [Server] Nota: 07.0 / 10
 *
 *   === MENIU ===
 *   1. Corecteaza test (introduce calea imaginii)
 *   2. Iesire
 *   Selectie: 2
 *   [Client] Conexiune inchisa.
 */

#include <stdio.h>      //folosit pentru printf, perror, fgets, fflush
#include <stdlib.h>     //folosit pentru exit
#include <string.h>     //folosit pentru strcmp, strlen, strcspn, memset
#include <unistd.h>     //folosit pentru close
#include <sys/types.h>  //folosit pentru tipurile de date socket
#include <sys/socket.h> //folosit pentru socket, connect, send, recv
#include <netinet/in.h> //folosit pentru struct sockaddr_in, htons
#include <arpa/inet.h>  //folosit pentru inet_addr

#define PORT      9090         //portul serverului
#define BUF_SIZE  4096         //dimensiunea bufferului pentru mesaje
#define SERVER_IP "127.0.0.1"  //adresa IP a serverului

// ── afiseaza_meniu ────────────────────────────────────────────────────────────

void afiseaza_meniu(void) {
    printf("\n=== MENIU ===\n");
    printf("1. Corecteaza test (introduce calea imaginii)\n");
    printf("2. Iesire\n");
    printf("Selectie: ");
    fflush(stdout); //fortam afisarea imediata a promptului
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(void) {
    int sockfd=socket(AF_INET, SOCK_STREAM, 0); //cream socket TCP
    if (sockfd < 0) { perror("[ERR] socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));     //initializare cu 0
    server_addr.sin_family=AF_INET;                    //familia IPv4
    server_addr.sin_port=htons(PORT);                  //portul in network byte order
    server_addr.sin_addr.s_addr=inet_addr(SERVER_IP); //adresa IP a serverului

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERR] connect - serverul nu e pornit?");
        close(sockfd);
        return 1;
    }

    printf("[GradingApp] Conectat la server %s:%d\n", SERVER_IP, PORT);

    char selectie[16]; //buffer pentru selectia din meniu
    char cale[BUF_SIZE]; //buffer pentru calea imaginii
    char msg[BUF_SIZE];  //buffer pentru mesajul de trimis serverului
    char resp[BUF_SIZE]; //buffer pentru raspunsul primit de la server

    while (1) {
        afiseaza_meniu(); //afisam optiunile disponibile

        if (fgets(selectie, sizeof(selectie), stdin) == NULL) break; //EOF, iesim
        selectie[strcspn(selectie, "\n")]='\0'; //eliminam newline-ul

        if (strcmp(selectie, "1") == 0) {
            // ── Optiunea 1: trimite imaginea pentru corectare ─────────────
            printf("Calea imaginii: ");
            fflush(stdout);

            if (fgets(cale, sizeof(cale), stdin) == NULL) break; //citim calea imaginii
            cale[strcspn(cale, "\n")]='\0'; //eliminam newline-ul

            if (strlen(cale) == 0) { //verificam ca nu e goala
                printf("[ERR] Calea nu poate fi goala.\n");
                continue;
            }

            //construim mesajul "grade:<cale>" caracter cu caracter
            int i=0, j=0;
            const char *prefix="grade:";
            while (prefix[j] != '\0') { msg[i++]=prefix[j++]; } //copiem "grade:"
            j=0;
            while (cale[j] != '\0' && i < BUF_SIZE-1) { msg[i++]=cale[j++]; } //copiem calea
            msg[i]='\0'; //terminatorul de sir

            printf("[Client] Trimit catre server: '%s'\n", msg);
            send(sockfd, msg, strlen(msg), 0); //trimitem cererea catre server

            memset(resp, 0, sizeof(resp));
            int n=recv(sockfd, resp, BUF_SIZE-1, 0); //asteptam raspunsul serverului
            if (n <= 0) { printf("[ERR] Serverul s-a deconectat.\n"); break; }
            resp[n]='\0';

            printf("\n[Rezultat] %s\n", resp); //afisam nota primita de la server

        } else if (strcmp(selectie, "2") == 0) {
            // ── Optiunea 2: iesire ────────────────────────────────────────
            send(sockfd, "quit", 4, 0); //trimitem comanda de iesire

            memset(resp, 0, sizeof(resp));
            int n=recv(sockfd, resp, BUF_SIZE-1, 0); //asteptam confirmarea
            if (n > 0) { resp[n]='\0'; printf("%s\n", resp); }

            printf("[Client] Conexiune inchisa.\n");
            break; //iesim din bucla meniului

        } else {
            printf("[ERR] Selectie invalida. Alege 1 sau 2.\n"); //selectie gresita
        }
    }

    close(sockfd); //inchidem socket-ul la final
    return 0;
}