

#include <stdio.h>      //folosit pentru printf, perror
#include <stdlib.h>     //folosit pentru exit
#include <string.h>     //folosit pentru memset, strlen, strcmp
#include <unistd.h>     //folosit pentru fork, pipe, read, write, close
#include <sys/types.h>  //folosit pentru pid_t si tipurile socket
#include <sys/socket.h> //folosit pentru socket, bind, listen, accept, send, recv
#include <sys/wait.h>   //folosit pentru waitpid - asteptam procesele fii
#include <netinet/in.h> //folosit pentru struct sockaddr_in, htons, htonl
#include <arpa/inet.h>  //folosit pentru inet_ntoa - afisarea IP clientului

#define PORT          9090 //portul pe care asculta serverul
#define BUF_SIZE      4096 //dimensiunea bufferului pentru mesaje
#define NR_INTREBARI  10   //numarul de intrebari din test (stub - va fi citit din imagine)

// ── proceseaza_intrebare ──────────────────────────────────────────────────────
// Ruleaza in procesul fiu. Primeste numarul intrebarii, "proceseaza"
// (stub - aici Membrul 1/2 va pune logica OpenCV + detectie).
// Trimite rezultatul (0 sau 1) parintelui prin pipe.

void proceseaza_intrebare(int nr_intrebare, int pipe_write) {
    printf("[Fiu PID=%d] Procesez intrebarea %d...\n", getpid(), nr_intrebare);

    // ── STUB: aici Membrul 1 va pune detectia din imagine ────────────────
    // ex: int corect = detecteaza_raspuns(imagine, nr_intrebare);
    int corect = (nr_intrebare % 3 != 0) ? 1 : 0; //simulam: Q3, Q6, Q9 gresite

    write(pipe_write, &corect, sizeof(int)); //trimitem rezultatul (0/1) catre parinte
    close(pipe_write); //inchidem capatul de scriere dupa trimitere
    exit(0); //procesul fiu termina dupa procesare
}

// ── evalueaza_test ────────────────────────────────────────────────────────────
// Lanseaza cate un proces fiu pentru fiecare intrebare via fork().
// Colecteaza rezultatele prin pipe() si calculeaza nota finala.
// Returneaza nota pe scala 0-10.

float evalueaza_test(const char *cale_imagine) {
    printf("[Server] Incepe evaluarea imaginii: %s\n", cale_imagine);

    int     pipefd[NR_INTREBARI][2]; //cate un pipe pentru fiecare intrebare
    pid_t   pids[NR_INTREBARI];      //pid-urile proceselor fii

    // ── Pasul 1: pornim un proces fiu pentru fiecare intrebare ───────────
    for (int i=0; i < NR_INTREBARI; i++) {
        if (pipe(pipefd[i]) < 0) { //cream pipe-ul de comunicare pentru intrebarea i
            perror("[ERR] pipe");
            continue;
        }

        pid_t pid=fork(); //cream procesul fiu
        if (pid < 0) {
            perror("[ERR] fork");
            continue;
        }

        if (pid == 0) { //suntem in procesul fiu
            close(pipefd[i][0]); //fiul nu citeste din pipe, inchidem capatul de citire
            proceseaza_intrebare(i+1, pipefd[i][1]); //procesam intrebarea (nu se intoarce)
        }

        //suntem in procesul parinte
        close(pipefd[i][1]); //parintele nu scrie in pipe, inchidem capatul de scriere
        pids[i]=pid;         //retinem pid-ul fiului pentru waitpid
        printf("[Server] Procesez intrebarea %d in procesul PID=%d\n", i+1, pid);
    }

    // ── Pasul 2: asteptam toti fiii si colectam rezultatele ──────────────
    int total_corecte=0; //numarul total de intrebari corecte
    for (int i=0; i < NR_INTREBARI; i++) {
        int rezultat=0;
        read(pipefd[i][0], &rezultat, sizeof(int)); //citim rezultatul (0/1) din pipe
        close(pipefd[i][0]); //inchidem capatul de citire dupa ce am citit
        total_corecte+=rezultat; //acumulam raspunsurile corecte
        waitpid(pids[i], NULL, 0); //asteptam terminarea procesului fiu i
    }

    printf("[Server] Intrebari corecte: %d / %d\n", total_corecte, NR_INTREBARI);

    // ── Pasul 3: calculam nota pe scala 0-10 ─────────────────────────────
    float nota=((float)total_corecte / NR_INTREBARI) * 10.0f;
    return nota; //returnam nota calculata
}

// ── proceseaza_client ─────────────────────────────────────────────────────────
// Gestioneaza comunicarea cu un client conectat.
// Asteapta comanda "grade:<cale_imagine>" si declanseaza evaluarea.

void proceseaza_client(int connfd, struct sockaddr_in *client_addr) {
    char buf[BUF_SIZE];  //buffer pentru mesajele primite de la client
    char resp[BUF_SIZE]; //buffer pentru raspunsul de trimis

    printf("[Server] Client conectat: %s\n", inet_ntoa(client_addr->sin_addr));

    while (1) {
        memset(buf, 0, sizeof(buf)); //curatam buffer-ul inainte de citire
        int n=recv(connfd, buf, BUF_SIZE-1, 0); //asteptam mesajul de la client
        if (n <= 0) { //n=0 = deconectat, n<0 = eroare
            printf("[Server] Client deconectat.\n");
            break;
        }
        buf[n]='\0'; //terminatorul de sir

        printf("[Server] Primit: '%s'\n", buf);

        if (strncmp(buf, "grade:", 6) == 0) {
            // ── cerere de corectare: grade:<cale_imagine> ─────────────────
            const char *cale_imagine=buf+6; //sarim peste prefixul "grade:"

            float nota=evalueaza_test(cale_imagine); //evaluam testul (fork/wait per Q)

            //construim raspunsul cu nota
            memset(resp, 0, sizeof(resp));
            const char *prefix="Nota: ";
            int i=0, j=0;
            while (prefix[j] != '\0') { resp[i++]=prefix[j++]; } //copiem prefixul

            //convertim nota la string fara sprintf
            int nota_int=(int)nota;
            resp[i++]=(char)('0' + nota_int/10);
            resp[i++]=(char)('0' + nota_int%10);
            resp[i++]='.';
            int nota_zec=(int)((nota - nota_int) * 10);
            resp[i++]=(char)('0' + nota_zec);
            resp[i++]=' '; resp[i++]='/'; resp[i++]=' ';
            resp[i++]='1'; resp[i++]='0';
            resp[i]='\0';

            printf("[Server] Nota finala: %s\n", resp);
            send(connfd, resp, strlen(resp), 0); //trimitem nota catre client

        } else if (strcmp(buf, "quit") == 0) {
            // ── clientul vrea sa inchida conexiunea ───────────────────────
            send(connfd, "La revedere!", 12, 0);
            printf("[Server] Conexiune inchisa la cererea clientului.\n");
            break;

        } else {
            // ── comanda necunoscuta ────────────────────────────────────────
            const char *err="Comenzi valide: grade:<cale_imagine> | quit";
            send(connfd, err, strlen(err), 0);
        }
    }

    close(connfd); //inchidem conexiunea cu clientul
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(void) {
    int sockfd=socket(AF_INET, SOCK_STREAM, 0); //cream socket TCP
    if (sockfd < 0) { perror("[ERR] socket"); return 1; }

    int opt=1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //reutilizare port

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); //initializare cu 0
    server_addr.sin_family=AF_INET;               //familia IPv4
    server_addr.sin_port=htons(PORT);             //portul in network byte order
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY); //acceptam pe orice interfata

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERR] bind"); close(sockfd); return 1;
    }

    if (listen(sockfd, 5) < 0) { //ascultam cu backlog 5
        perror("[ERR] listen"); close(sockfd); return 1;
    }

    printf("[Server] Pornit pe portul %d. Astept clienti...\n\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len=sizeof(client_addr);

        int connfd=accept(sockfd, (struct sockaddr *)&client_addr, &client_len); //acceptam conexiunea
        if (connfd < 0) { perror("[ERR] accept"); continue; }

        proceseaza_client(connfd, &client_addr); //procesam cererile clientului
    }

    close(sockfd);
    return 0;
}
