/**
 * clientgrading.c - Client TCP pentru aplicatia de corectare automata a testelor grila
 *
 *   Acest program este interfata utilizatorului din sistemul Grading App.
 *   Se conecteaza prin TCP la serverul de corectare (127.0.0.1:9090) si ofera
 *   un meniu simplu cu doua optiuni:
 *
 *   1. Corecteaza test: utilizatorul introduce calea catre o imagine (fotografia
 *      foii de raspuns a studentului). Clientul impacheteaza calea in formatul
 *      "grade:<cale>" si o trimite serverului. Serverul proceseaza imaginea cu
 *      OpenCV, ruleaza 40 de procese fiu in paralel (cate unul per intrebare)
 *      si returneaza nota finala in formatul "Nota: X.Y / 10".
 *
 *   2. Iesire: trimite "quit" serverului si inchide conexiunea TCP.
 *
 *   Clientul nu face nicio procesare de imagine — toata logica de corectare
 *   are loc exclusiv pe server. Clientul este doar un terminal de control.
 */

#include <stdio.h>      /* printf, perror, fgets, fflush - intrare/iesire standard */
#include <stdlib.h>     /* exit - terminare program */
#include <string.h>     /* strcmp, strlen, strcspn - operatii pe siruri de caractere */
#include <unistd.h>     /* close - inchidere descriptori de socket */
#include <sys/socket.h> /* socket, connect, send, recv - API-ul de socketi BSD */
#include <netinet/in.h> /* struct sockaddr_in, htons - structuri adrese IPv4 */
#include <arpa/inet.h>  /* inet_addr - conversia IP din string in format binar */


enum Configurare {
    PORT_SERVER  = 9090, /* portul TCP pe care asculta serverul */
    DIM_BUFFER   = 4096, /* dimensiunea bufferelor de date (mesaje trimise/primite) */
    DIM_SELECTIE = 16    /* dimensiunea bufferului pentru selectia din meniu (e.g. "1\n\0") */
};

/* ── afiseaza_meniu ───────────────────────────────────────────────────────────
 * Afiseaza cele doua optiuni disponibile si promptul de selectie.
 * (void) in fata printf suprima warning-ul de "return value ignored" din linter.*/

void afiseaza_meniu(void) {
    (void)printf("\n=== MENIU ===\n");
    (void)printf("1. Corecteaza test (introduce calea imaginii)\n"); /* optiunea de corectare */
    (void)printf("2. Iesire\n");                                      /* optiunea de deconectare */
    (void)printf("Selectie: ");
    (void)fflush(stdout); /* fortam afisarea imediata a promptului inainte de a astepta input */
}

/* ── proceseaza_optiunea_unu ─────────────────────────────────────────────────
 * Gestioneaza fluxul complet al optiunii 1: citire cale imagine, construire
 * mesaj, trimitere catre server si afisare rezultat.
 *
 * Parametru:
 *   socket_comunicare - descriptorul socket-ului TCP deja conectat la server
 */
void proceseaza_optiunea_unu(int socket_comunicare) {
    /* Initializam toate bufferele cu {0} */
    char cale_imagine[DIM_BUFFER]   = {0}; /* calea introdusa de utilizator (ex: "photo.png") */
    char mesaj_server[DIM_BUFFER]   = {0}; /* mesajul final de trimis: "grade:<cale>" */
    char raspuns_server[DIM_BUFFER] = {0}; /* raspunsul primit de la server (ex: "Nota: 8.5 / 10") */

    (void)printf("Calea imaginii: ");
    (void)fflush(stdout); /* afisam promptul inainte sa blocam la citire */

    /* fgets citeste o linie de la stdin, inclusiv '\n', in cale_imagine.*/
    if (fgets(cale_imagine, DIM_BUFFER, stdin) != NULL) {

        /* strcspn returneaza pozitia primului caracter din setul dat ("\n").
         * Inlocuind acel caracter cu '\0', eliminam newline-ul pe care fgets
         * il include in buffer. Fara acest pas, calea ar fi "photo.png\n"
         * si serverul nu ar gasi fisierul. */
        cale_imagine[strcspn(cale_imagine, "\n")] = '\0';

        /* Verificam ca utilizatorul nu a apasat Enter fara sa scrie nimic */
        if (strlen(cale_imagine) == 0) {
            (void)printf("[ERR] Calea nu poate fi goala.\n");
            return; /* revenim in bucla principala fara sa trimitem nimic */
        }

        /* ── Construim mesajul "grade:<cale>" caracter cu caracter ──────────
         * Copiem manual. Mai intai prefixul "grade:", apoi calea imaginii, 
         * cu verificare de depasire a bufferului la fiecare pas. */
        size_t index_mesaj = 0;               /* pozitia curenta de scriere in mesaj_server */
        const char *prefix_comanda = "grade:"; /* protocolul cere acest prefix fix */

        /* Copiem prefixul "grade:" in mesaj_server caracter cu caracter */
        while (prefix_comanda[index_mesaj] != '\0' && index_mesaj < (size_t)DIM_BUFFER - 1) {
            mesaj_server[index_mesaj] = prefix_comanda[index_mesaj]; /* copiem un caracter */
            index_mesaj++;                                            /* avansam pozitia */
        }

        /* Copiem calea imaginii imediat dupa prefix, fara spatiu sau alt separator */
        size_t index_cale = 0; /* index separat pentru parcurgerea cale_imagine */
        while (cale_imagine[index_cale] != '\0' && index_mesaj < (size_t)DIM_BUFFER - 1) {
            mesaj_server[index_mesaj] = cale_imagine[index_cale]; /* copiem un caracter din cale */
            index_mesaj++;
            index_cale++;
        }
        mesaj_server[index_mesaj] = '\0'; /* terminam sirul: mesaj_server = "grade:photo.png" */

        /* Afisam ce trimitem, util pentru debug */
        (void)printf("[Client] Trimit catre server: '%s'\n", mesaj_server);

        /* send() trimite datele prin socket-ul TCP catre server.
         * Al treilea argument este lungimea in bytes (fara '\0').
         * Flagul 0 = comportament implicit (blocat pana la trimitere). */
        (void)send(socket_comunicare, mesaj_server, strlen(mesaj_server), 0);

        /* recv() blocheaza pana cand serverul trimite raspunsul.
         * Returneaza numarul de bytes primiti, 0 la deconectare, -1 la eroare.
         * DIM_BUFFER - 1 lasa loc pentru terminatorul '\0' pe care il adaugam manual. */
        ssize_t octeti_primiti = recv(socket_comunicare, raspuns_server, (size_t)DIM_BUFFER - 1, 0);

        if (octeti_primiti <= 0) {
            /* 0 = serverul a inchis conexiunea; negativ = eroare de retea */
            (void)printf("[ERR] Serverul s-a deconectat.\n");
        } else {
            /* Afisam nota primita: ex "Nota: 7.5 / 10" sau "Eroare: Imagine invalida!" */
            (void)printf("\n[Rezultat] %s\n", raspuns_server);
        }
    }
}

/* ── main ──────────────────────────────────────────────────────────────────── */
int main(void) {
    /* socket() creeaza un endpoint de comunicare si returneaza un descriptor.
     * AF_INET   = familia IPv4
     * SOCK_STREAM = TCP (flux de date bidirectional, ordonat, fiabil)
     * 0         = protocolul implicit pentru SOCK_STREAM (adica TCP) */
    int descriptor_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor_socket < 0) {
        perror("[ERR] socket"); /* afiseaza eroarea din errno (ex: "Too many open files") */
        return 1;
    }

    /* Initializam structura adresei serverului direct cu {0} in loc de memset.
     * Aceasta umple toti bytes cu 0, ceea ce e echivalent si evita warning-ul linter. */
    struct sockaddr_in adresa_server = {0};

    adresa_server.sin_family      = AF_INET;           /* familia IPv4 */
    adresa_server.sin_port        = htons(PORT_SERVER); /* htons = host to network short:
                                                          * converteste portul din little-endian
                                                          * (x86) in big-endian (network order) */
    adresa_server.sin_addr.s_addr = inet_addr("127.0.0.1"); /* convertim IP-ul din string
                                                               * "127.0.0.1" in format binar
                                                               * (loopback = acelasi calculator) */

    /* connect() initializeaza handshake-ul TCP cu serverul (SYN -> SYN-ACK -> ACK).
     * Blocheaza pana la stabilirea conexiunii sau pana la timeout/eroare.
     * Cast la (const struct sockaddr *) necesar deoarece API-ul accepta orice tip de adresa. */
    if (connect(descriptor_socket, (const struct sockaddr *)&adresa_server, (socklen_t)sizeof(adresa_server)) < 0) {
        perror("[ERR] connect - serverul nu e pornit?");
        (void)close(descriptor_socket); /* eliberam resursa inainte de iesire */
        return 1;
    }

    (void)printf("[GradingApp] Conectat la server pe portul %d\n", PORT_SERVER);

    /* Buffer mic pentru selectia din meniu: "1\n\0" sau "2\n\0" ocupa 3 bytes,
     * DIM_SELECTIE=16 este mai mult decat suficient si protejeaza impotriva
     * inputului prea lung care ar putea depasi un buffer mai mic. */
    char buffer_selectie[DIM_SELECTIE] = {0};

    
    while (1) {
        afiseaza_meniu(); /* afisam optiunile si promptul */

        /* Citim selectia utilizatorului; fgets returneaza NULL la EOF sau eroare */
        if (fgets(buffer_selectie, DIM_SELECTIE, stdin) == NULL) {
            break; /* EOF (Ctrl+D): iesim din bucla fara sa mai trimitem "quit" */
        }

        /* Eliminam '\n' de la sfarsitul selectiei pentru a putea face strcmp corect.
         * Fara acest pas, "1\n" != "1" si niciun if nu ar fi adevarat. */
        buffer_selectie[strcspn(buffer_selectie, "\n")] = '\0';

        if (strcmp(buffer_selectie, "1") == 0) {
            /* Optiunea 1: corectare test - delegam toata logica functiei dedicate */
            proceseaza_optiunea_unu(descriptor_socket);

        } else if (strcmp(buffer_selectie, "2") == 0) {
            /* Optiunea 2: iesire curata
             * Trimitem "quit" (exact 4 bytes) catre server ca sa stie sa inchida
             * conexiunea din partea lui (raspunde cu "La revedere!" si face close). */
            (void)send(descriptor_socket, "quit", 4, 0);
            (void)printf("[Client] Conexiune inchisa.\n");
            break; /* iesim din bucla, mergem la close() de mai jos */

        } else {
            /* Orice altceva decat "1" sau "2": afisam eroare si continuam bucla */
            (void)printf("[ERR] Selectie invalida. Alege 1 sau 2.\n");
        }
    }

    
    (void)close(descriptor_socket);
    return 0; /* program terminat cu succes */
}
