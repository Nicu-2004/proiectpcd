/**
 * servergrading.c - Server TCP pentru corectarea automata a testelor grila
 *
 * Descriere generala:
 *   Acesta este nucleul aplicatiei Grading App. Ruleaza continuu pe portul 9090
 *   si asteapta conexiuni TCP de la clienti. Pentru fiecare client conectat:
 *
 *   1. Asteapta un mesaj in formatul "grade:<cale_imagine>" sau "quit".
 *
 *   2. La primirea "grade:<cale>", apeleaza modulul OpenCV (opencv_punte.cpp)
 *      care incarca imaginea foii de raspuns, aplica filtre si detecteaza
 *      cercurile marcate de student, salvand rezultatele intr-un array global.
 *
 *   3. Creeaza 40 de procese fiu in paralel (fork), cate unul per intrebare.
 *      Fiecare fiu verifica daca raspunsul studentului la intrebarea sa
 *      corespunde baremului si trimite 1 (corect) sau 0 (gresit) prin pipe
 *      catre procesul parinte.
 *
 *   4. Parintele aduna rezultatele din toate pipe-urile, calculeaza nota
 *      (0.0 - 10.0) si o trimite inapoi clientului ca text: "Nota: X.Y / 10".
 *
 *   Serverul este secvential - proceseaza un singur client
 *   odata. Concurenta exista doar in interiorul evaluarii unei imagini
 *   (cele 40 de procese fiu pentru cele 40 de intrebari).
 */

#include <stdio.h>      /* printf, perror - afisare mesaje si erori */
#include <stdlib.h>     /* exit - terminare program */
#include <string.h>     /* strcmp, strncmp, strlen - operatii pe siruri */
#include <unistd.h>     /* close, pipe, read, write - apeluri sistem POSIX */
#include <sys/types.h>  /* pid_t - tipul pentru ID-ul unui proces, necesar explicit pentru clang-tidy */
#include <sys/socket.h> /* socket, bind, listen, accept, send, recv, SOL_SOCKET, SO_REUSEADDR */
#include <sys/wait.h>   /* waitpid - asteptarea terminarii proceselor fiu */
#include <netinet/in.h> /* struct sockaddr_in, htons, htonl, INADDR_ANY - adrese IPv4 */
#include <arpa/inet.h>  /* inet_ntop, INET_ADDRSTRLEN - conversia IP din binar in string */


enum ConfigSrv {
    PORT_DEFAULT   = 9090, /* portul TCP pe care asculta serverul */
    BUFFER_SIZE    = 4096, /* dimensiunea bufferelor de date (mesaje primite/trimise) */
    NR_INTREBARI   = 40,   /* numarul de intrebari din grila (si numarul de procese fiu) */
    BACKLOG_LISTEN = 5,    /* cate conexiuni pot astepta la coada inainte de accept() */
    PREFIX_LEN     = 6,    /* lungimea prefixului "grade:" (6 caractere) */
    NOTA_MAX_INT   = 10,   /* nota maxima posibila (10), folosita si pentru formatare */
    BYE_MSG_LEN    = 12    /* lungimea exacta a mesajului "La revedere!" (12 caractere) */
};

static const float FACTOR_ZECIMALA = 10.0F;

extern int incarca_imagine_opencv(const char* cale);       /* incarca imaginea si detecteaza bulele */
extern int proceseaza_intrebare_opencv(int nr_intrebare);  /* verifica daca intrebarea e corecta */


void formateaza_raspuns_nota(char *buffer, float nota) {
    int parte_intreaga = (int)nota; /* extragem partea intreaga: 7.5 -> 7 */
    /* extragem cifra zecimala: (7.5 - 7) * 10 = 5 */
    int zecimala = (int)((nota - (float)parte_intreaga) * FACTOR_ZECIMALA);

    const char *prefix_nota = "Nota: "; /* prefixul fix al raspunsului */
    size_t pos = 0;                     /* pozitia curenta de scriere in buffer */

    /* Copiem "Nota: " caracter cu caracter in buffer,
     * cu verificare sa nu depasim dimensiunea bufferului */
    while (prefix_nota[pos] != '\0' && pos < (size_t)BUFFER_SIZE - 1) {
        buffer[pos] = prefix_nota[pos]; /* copiem un caracter din prefix */
        pos++;                           /* avansam la urmatoarea pozitie */
    }

    /* Scriem partea intreaga a notei.
     * Cazul special: daca nota e 10, scriem "10" (doua cifre), nu "1" si "0". */
    if (parte_intreaga == NOTA_MAX_INT) {
        buffer[pos++] = '1'; /* prima cifra din "10" */
        buffer[pos++] = '0'; /* a doua cifra din "10" */
    } else {
        /* Pentru note 0-9: adaugam '0' la valoarea intreaga pentru a obtine
         * caracterul ASCII corespunzator. Ex: 7 + '0' = '7' (codul ASCII 55) */
        buffer[pos++] = (char)(parte_intreaga + '0');
    }

    buffer[pos++] = '.'; /* separatorul zecimal */

    /* Scriem cifra zecimala folosind acelasi truc cu '0' */
    buffer[pos++] = (char)(zecimala + '0'); /* ex: 5 + '0' = '5' */

    /* Copiem sufixul " / 10" caracter cu caracter */
    const char *suffix_nota = " / 10";
    size_t idx_suffix = 0; /* index separat pentru parcurgerea sufixului */
    while (suffix_nota[idx_suffix] != '\0' && pos < (size_t)BUFFER_SIZE - 1) {
        buffer[pos] = suffix_nota[idx_suffix]; /* copiem un caracter din sufix */
        pos++;
        idx_suffix++;
    }
    buffer[pos] = '\0'; /* terminam sirul: "Nota: 7.5 / 10\0" */
}


void formateaza_eroare_imagine(char *buffer) {
    const char *msg_err = "Eroare: Imagine invalida!"; /* mesajul fix de eroare */
    size_t contor = 0;                                  /* index de parcurgere */
    /* Copiem mesajul de eroare caracter cu caracter cu protectie de buffer */
    while (msg_err[contor] != '\0' && contor < (size_t)BUFFER_SIZE - 1) {
        buffer[contor] = msg_err[contor]; /* copiem un caracter */
        contor++;
    }
    buffer[contor] = '\0'; /* terminam sirul */
}


float evalueaza_test(const char *cale_imagine) {
    (void)printf("\n[Server] Incepe evaluarea: %s\n", cale_imagine); /* logam inceperea evaluarii */

    
    if (incarca_imagine_opencv(cale_imagine) == 0) {
        (void)printf("[ERR] Nu am putut incarca imaginea: %s\n", cale_imagine);
        return -1.0F; /* semnalam eroare cu valoare negativa */
    }

    /* Array bidimensional de pipe-uri: pipe_fd[i][0] = capatul de citire al pipe-ului i
     *                                  pipe_fd[i][1] = capatul de scriere al pipe-ului i
     * Cream cate un pipe pentru fiecare din cele NR_INTREBARI=40 intrebari.
     * Fiul scrie in pipe_fd[i][1], parintele citeste din pipe_fd[i][0]. */
    int pipe_fd[NR_INTREBARI][2];

    /* Array cu ID-urile proceselor fiu, necesar pentru waitpid() mai jos */
    pid_t pids[NR_INTREBARI];

     /* Pentru fiecare intrebare cream un pipe si un proces fiu. */
    for (int contor = 0; contor < NR_INTREBARI; contor++) {

        /* pipe() creeaza un canal de comunicare unidirectional intre procese.
         * pipe_fd[contor][0] = capatul de citire (parintele va citi de aici)
         * pipe_fd[contor][1] = capatul de scriere (fiul va scrie aici)
         * Returneaza -1 la eroare (ex: prea multe fisiere deschise). */
        if (pipe(pipe_fd[contor]) < 0) { continue; } /* la eroare sarim intrebarea */

        pid_t pid_fiu = fork();

        if (pid_fiu == 0) {
            /* ── COD EXECUTAT DOAR DE PROCESUL FIU ──────────────────────────
             * Fiul are responsabilitatea de a verifica O SINGURA intrebare
             * (cea corespunzatoare valorii curente a lui "contor"). */

            /* Inchidem capatul de CITIRE al pipe-ului in fiu:
             * fiul nu citeste, doar scrie rezultatul. */
            (void)close(pipe_fd[contor][0]);

            /* Apelam functia din OpenCV care verifica daca raspunsul studentului
             * la intrebarea (contor+1) corespunde cu baremul.
             * Returneaza 1 daca e corect, 0 daca e gresit sau fara raspuns. */
            int este_corect = proceseaza_intrebare_opencv(contor + 1);

            /* Scriem rezultatul (1 sau 0) in pipe catre parinte.
             * Scriem sizeof(int) bytes (de obicei 4 bytes) din variabila este_corect.
             * &este_corect = adresa de memorie unde e stocata valoarea. */
            (void)write(pipe_fd[contor][1], &este_corect, sizeof(int));

            /* Inchidem capatul de scriere dupa ce am terminat */
            (void)close(pipe_fd[contor][1]);

            /* _exit(0) termina procesul FIU imediat fara sa apeleze destructorii C++ sau sa flushuiasca bufferele stdio.*/
            _exit(0);
        }

        /* ── COD EXECUTAT DOAR DE PROCESUL PARINTE ───────────────────────────
         * Dupa fork(), parintele continua de aici. */

        /* Parintele inchide capatul de SCRIERE al pipe-ului:
         * parintele nu scrie, doar citeste rezultatele de la fii. */
        (void)close(pipe_fd[contor][1]);

        /* Salvam PID-ul fiului pentru a putea astepta terminarea lui mai jos */
        pids[contor] = pid_fiu;
    }

    /* ── Colectarea rezultatelor de la procesele fiu ─────────────────────────
     * Citim rezultatul fiecarui fiu din pipe-ul sau si adunam punctele. */
    int total_corecte = 0; /* numarul total de intrebari corecte (0..40) */

    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        int rezultat_fiu = 0; /* valoarea trimisa de fiu: 1 (corect) sau 0 (gresit) */

        /* read() citeste sizeof(int) bytes din pipe in variabila rezultat_fiu.
         * Blocheaza pana cand fiul scrie sau pana cand fiul inchide pipe-ul. */
        (void)read(pipe_fd[contor][0], &rezultat_fiu, sizeof(int));

        /* Inchidem capatul de citire dupa ce am primit rezultatul */
        (void)close(pipe_fd[contor][0]);

        /* Adunam rezultatul: 0 sau 1 */
        total_corecte += rezultat_fiu;

        /* waitpid() asteapta terminarea fiului cu PID-ul dat. */
        (void)waitpid(pids[contor], NULL, 0);
    }

    /* Calculam nota: (raspunsuri_corecte / total_intrebari) * nota_maxima
     * Cast explicit la float pentru a evita impartirea intreaga (integer division).
     * Ex: 30 corecte din 40 => (30.0 / 40.0) * 10.0 = 7.5 */
    return ((float)total_corecte / (float)NR_INTREBARI) * (float)NOTA_MAX_INT;
}


void proceseaza_client(int descriptor_client, struct sockaddr_in *adresa_clt) {
   
    char buffer_citire[BUFFER_SIZE]  = {0}; /* buffer pentru cererea primita de la client */
    char buffer_raspuns[BUFFER_SIZE] = {0}; /* buffer pentru raspunsul de trimis clientului */
    char ip_client[INET_ADDRSTRLEN]  = {0}; /* string pentru IP-ul clientului (max 16 chars pentru IPv4) */

    /* inet_ntop = "network to presentation": converteste adresa IP din format
     * binar (4 bytes in sin_addr) in string lizibil "192.168.1.5". */
    (void)inet_ntop(AF_INET, &(adresa_clt->sin_addr), ip_client, INET_ADDRSTRLEN);
    (void)printf("[Server] Client conectat: %s\n", ip_client); /* afisam IP-ul clientului */

    /* ── Bucla principala de procesare a cererilor ───────────────────────────
     * Ruleaza pana la deconectarea clientului sau la comanda "quit". */
    while (1) {
        /* Curatam buffer-ul de citire inainte de fiecare recv().
         * Facem asta manual cu un for in loc de memset pentru a evita
         * warning-ul de "insecure API" din clang-tidy. */
        for(size_t idx = 0; idx < (size_t)BUFFER_SIZE; idx++) { buffer_citire[idx] = '\0'; }

        /* recv() asteapta date de la client prin socket-ul TCP.
         * BUFFER_SIZE - 1 lasa loc pentru terminatorul '\0' pe care il adaugam manual.
         * Returneaza: >0 = numarul de bytes primiti
         *              0 = clientul a inchis conexiunea (FIN TCP)
         *             -1 = eroare de retea */
        ssize_t octeti_primiti = recv(descriptor_client, buffer_citire, (size_t)BUFFER_SIZE - 1, 0);

        /* Daca clientul s-a deconectat (0) sau a aparut o eroare (<0), iesim */
        if (octeti_primiti <= 0) { break; }

        /* Adaugam terminatorul de sir manual dupa bytes primiti.
         * recv() nu adauga '\0' automat - fara aceasta linie, functiile
         * de string (strcmp, strncmp) ar citi dincolo de datele primite. */
        buffer_citire[octeti_primiti] = '\0';

        /* ── Parsarea si procesarea comenzii primite ─────────────────────── */

        /* strncmp compara primele PREFIX_LEN=6 caractere din buffer_citire cu "grade:".
         * Daca sunt egale, clientul vrea sa corecteze o imagine. */
        if (strncmp(buffer_citire, "grade:", (size_t)PREFIX_LEN) == 0) {

            /* buffer_citire + PREFIX_LEN = pointer la caracterul de dupa "grade:".
             * Daca buffer_citire = "grade:photo.png", atunci
             * buffer_citire + 6  = "photo.png"
             * Trimitem direct acest pointer la evalueaza_test(). */
            float nota = evalueaza_test(buffer_citire + PREFIX_LEN);

            /* Curatam buffer-ul de raspuns inainte de a-l completa */
            for(size_t idx = 0; idx < (size_t)BUFFER_SIZE; idx++) { buffer_raspuns[idx] = '\0'; }

            /* Alegem raspunsul in functie de rezultatul evaluarii:
             * nota < 0 inseamna ca imaginea nu a putut fi procesata */
            if (nota < 0.0F) {
                formateaza_eroare_imagine(buffer_raspuns); /* "Eroare: Imagine invalida!" */
            } else {
                formateaza_raspuns_nota(buffer_raspuns, nota); /* "Nota: 7.5 / 10" */
            }

            /* Trimitem raspunsul catre client prin socket-ul TCP.
             * strlen(buffer_raspuns) = numarul de bytes de trimis (fara '\0').
             * Flagul 0 = comportament implicit. */
            (void)send(descriptor_client, buffer_raspuns, strlen(buffer_raspuns), 0);

        } else if (strcmp(buffer_citire, "quit") == 0) {
            /* Clientul vrea sa inchida conexiunea.
             * Trimitem mesajul de ramas-bun (exact BYE_MSG_LEN=12 bytes = "La revedere!")
             * apoi iesim din bucla. */
            (void)send(descriptor_client, "La revedere!", (size_t)BYE_MSG_LEN, 0);
            break; /* iesim din bucla, conexiunea se inchide mai jos cu close() */
        }
        /* Orice alta comanda este ignorata silentios (clientul nu primeste raspuns) */
    }

    /* close() inchide socket-ul clientului: trimite FIN TCP si elibereaza
     * descriptorul din tabela procesului. */
    (void)close(descriptor_client);
}

/* ── main ───────────────────────────────────────────────────────────────────
 */
int main(void) {
    /* socket() creeaza un endpoint TCP si returneaza un descriptor intreg.
     * AF_INET    = familia IPv4
     * SOCK_STREAM = TCP (conexiune fiabila, ordonata, bidirectionala)
     * 0          = protocolul implicit pentru SOCK_STREAM (TCP) */
    int descriptor_server = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor_server < 0) { return 1; } /* socket() returneaza -1 la eroare */

    /* SO_REUSEADDR: permite reutilizarea imediata a portului dupa oprirea serverului.*/
    int opt = 1;
    (void)setsockopt(descriptor_server, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t)sizeof(opt)); // NOLINT(misc-include-cleaner)

    /* Initializam structura adresei serverului cu toti bytes pe 0. */
    struct sockaddr_in adresa_srv = {0};
    adresa_srv.sin_family      = AF_INET;            /* familia IPv4 */
    adresa_srv.sin_port        = htons(PORT_DEFAULT); /* htons = host to network short: */
    adresa_srv.sin_addr.s_addr = htonl(INADDR_ANY);  /* INADDR_ANY = 0.0.0.0: acceptam conexiuni
                                                        * pe ORICE interfata de retea disponibila
                                                        * (localhost, ethernet, wifi simultan).
                                                        * htonl = host to network long (32 biti) */

    /* bind() leaga socket-ul la adresa si portul configurate mai sus. */
    if (bind(descriptor_server, (const struct sockaddr *)&adresa_srv, (socklen_t)sizeof(adresa_srv)) < 0) {
        (void)close(descriptor_server); /* eliberam resursa inainte de iesire */
        return 1;
    }

    /* listen() trece socket-ul din starea "inchis" in starea "ascultare".
     * BACKLOG_LISTEN=5 = cate conexiuni pot astepta in coada inainte de accept().
     * Daca mai mult de 5 clienti se conecteaza simultan, al 6-lea primeste eroare. */
    if (listen(descriptor_server, BACKLOG_LISTEN) < 0) {
        (void)close(descriptor_server);
        return 1;
    }

    /* ── Bucla infinita de acceptare a clientilor ────────────────────────────
     * Serverul ruleaza la nesfarsit, acceptand clienti unul dupa altul. */
    while (1) {
        /* Structura in care accept() va pune adresa clientului conectat */
        struct sockaddr_in adresa_clt = {0};

        /* socklen_t = tipul special pentru dimensiunile structurilor de adresa.
         * Trebuie initializat cu dimensiunea structurii INAINTE de accept(). */
        socklen_t lungime = (socklen_t)sizeof(adresa_clt);

        /* accept() blocheaza pana cand un client se conecteaza.
         * La conectare, creeaza un NOU socket dedicat acestei conexiuni si il returneaza ca descriptor_comunicare. */
        int descriptor_comunicare = accept(descriptor_server, (struct sockaddr *)&adresa_clt, &lungime);

        /* Verificam ca accept() a reusit (returneaza -1 la eroare) */
        if (descriptor_comunicare >= 0) {
            /* Procesam toate cererile acestui client. Aceasta apelare blocheaza
             * pana cand clientul se deconecteaza, deci serverul e secvential:
             * nu accepta alt client pana nu termina cu cel curent. */
            proceseaza_client(descriptor_comunicare, &adresa_clt);
        }
        /* Dupa ce proceseaza_client() returneaza, revenim la accept()
         * si asteptam urmatorul client. */
    }
    
}
