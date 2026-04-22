#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT          9090
#define BUF_SIZE      4096
#define NR_INTREBARI  40

// --- Puntea catre OpenCV ---
extern int incarca_imagine_opencv(const char* cale);
extern int proceseaza_intrebare_opencv(int nr_intrebare);

// ── evalueaza_test ────────────────────────────────────────────────────────────
float evalueaza_test(const char *cale_imagine) {
    printf("\n[Server] Incepe evaluarea imaginii: %s\n", cale_imagine);

    // Citim si procesam imaginea o singura data (in parinte)
    if (incarca_imagine_opencv(cale_imagine) == 0) {
        printf("[ERR] Nu am putut incarca imaginea cu OpenCV: %s\n", cale_imagine);
        return -1.0f;
    }

    int   pipefd[NR_INTREBARI][2]; 
    pid_t pids[NR_INTREBARI];      

    // ── PORNIM FIII (Cate unul pentru fiecare intrebare) ──
    for (int i=0; i < NR_INTREBARI; i++) {
        if (pipe(pipefd[i]) < 0) continue; 

        pid_t pid = fork(); 

        if (pid == 0) { // SUNTEM IN FIU
            close(pipefd[i][0]); // Inchidem capatul de citire
            
            // Verificam daca intrebarea alocata acestui fiu e corecta
            int este_corect = proceseaza_intrebare_opencv(i + 1);

            write(pipefd[i][1], &este_corect, sizeof(int));
            close(pipefd[i][1]); 
            exit(0); // Fiul a terminat
        }

        // SUNTEM IN PARINTE
        close(pipefd[i][1]); 
        pids[i] = pid;         
    }

    // ── COLECTAM REZULTATELE ──
    int total_corecte = 0; 
    for (int i=0; i < NR_INTREBARI; i++) {
        int rezultat = 0;
        read(pipefd[i][0], &rezultat, sizeof(int)); 
        close(pipefd[i][0]); 
        total_corecte += rezultat; 
        waitpid(pids[i], NULL, 0); 
    }

    printf("[Server] Evaluare completa. Intrebari corecte: %d / %d\n", total_corecte, NR_INTREBARI);

    // Returnam nota din 10
    return ((float)total_corecte / NR_INTREBARI) * 10.0f;
}

// ── proceseaza_client ─────────────────────────────────────────────────────────
void proceseaza_client(int connfd, struct sockaddr_in *client_addr) {
    char buf[BUF_SIZE];  
    char resp[BUF_SIZE]; 

    printf("[Server] Client conectat: %s\n", inet_ntoa(client_addr->sin_addr));

    while (1) {
        memset(buf, 0, sizeof(buf)); 
        int n = recv(connfd, buf, BUF_SIZE-1, 0); 
        if (n <= 0) break;
        
        buf[n] = '\0'; 

        if (strncmp(buf, "grade:", 6) == 0) {
            const char *cale_imagine = buf + 6; 
            float nota = evalueaza_test(cale_imagine); 

            memset(resp, 0, sizeof(resp));
            if (nota < 0.0f) {
                strcpy(resp, "Eroare: Imaginea nu a putut fi procesata!");
            } else {
                // Formam manual string-ul pentru a nu folosi sprintf (C pur)
                const char *prefix = "Nota: ";
                int i=0, j=0;
                while (prefix[j] != '\0') { resp[i++]=prefix[j++]; } 

                int nota_int = (int)nota;
                resp[i++] = (char)('0' + nota_int/10);
                if (nota_int == 10) resp[i++] = '0'; // Cazul in care ia 10 curat
                else resp[i++] = (char)('0' + nota_int%10);
                
                resp[i++] = '.';
                int nota_zec = (int)((nota - nota_int) * 10);
                resp[i++] = (char)('0' + nota_zec);
                resp[i++]=' '; resp[i++]='/'; resp[i++]=' ';
                resp[i++]='1'; resp[i++]='0';
                resp[i]='\0';
            }

            printf("[Server] Trimit raspuns la client: %s\n", resp);
            send(connfd, resp, strlen(resp), 0); 

        } else if (strcmp(buf, "quit") == 0) {
            send(connfd, "La revedere!", 12, 0);
            break;
        } else {
            send(connfd, "Comenzi valide: grade:<cale_imagine> | quit", 43, 0);
        }
    }
    close(connfd); 
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(void) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd < 0) { perror("[ERR] socket"); return 1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); 
    server_addr.sin_family = AF_INET;               
    server_addr.sin_port = htons(PORT);             
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERR] bind"); close(sockfd); return 1;
    }

    if (listen(sockfd, 5) < 0) { 
        perror("[ERR] listen"); close(sockfd); return 1;
    }

    printf("[Server] PORNIT pe portul %d. Gata de scanare OMR!\n\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int connfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len); 
        if (connfd < 0) continue; 
        
        proceseaza_client(connfd, &client_addr); 
    }
    close(sockfd);
    return 0;
}
