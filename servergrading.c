#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Enum pentru a scapa de magic numbers si macro-uri */
enum ConfigSrv {
    PORT_DEFAULT   = 9090,
    BUFFER_SIZE    = 4096,
    NR_INTREBARI   = 40,
    BACKLOG_LISTEN = 5,
    PREFIX_LEN     = 6,
    NOTA_MAX       = 10,
    BYE_MSG_LEN    = 12
};

/* --- Puntea catre OpenCV --- */
extern int incarca_imagine_opencv(const char* cale);
extern int proceseaza_intrebare_opencv(int nr_intrebare);

/* ── evalueaza_test ──────────────────────────────────────────────────────────── */
float evalueaza_test(const char *cale_imagine) {
    (void)printf("\n[Server] Incepe evaluarea: %s\n", cale_imagine);

    if (incarca_imagine_opencv(cale_imagine) == 0) {
        (void)printf("[ERR] Nu am putut incarca imaginea: %s\n", cale_imagine);
        return -1.0F; /* F mare pentru literal */
    }

    int pipe_fd[NR_INTREBARI][2]; 
    pid_t pids[NR_INTREBARI];      

    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        if (pipe(pipe_fd[contor]) < 0) {
            continue; 
        }

        pid_t pid_fiu = fork(); 

        if (pid_fiu == 0) { 
            (void)close(pipe_fd[contor][0]); 
            
            int este_corect = proceseaza_intrebare_opencv(contor + 1);

            (void)write(pipe_fd[contor][1], &este_corect, sizeof(int));
            (void)close(pipe_fd[contor][1]); 
            exit(0); 
        }

        (void)close(pipe_fd[contor][1]); 
        pids[contor] = pid_fiu;         
    }

    int total_corecte = 0; 
    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        int rezultat_fiu = 0;
        (void)read(pipe_fd[contor][0], &rezultat_fiu, sizeof(int)); 
        (void)close(pipe_fd[contor][0]); 
        total_corecte += rezultat_fiu; 
        (void)waitpid(pids[contor], NULL, 0); 
    }

    (void)printf("[Server] Rezultat: %d / %d\n", total_corecte, NR_INTREBARI);

    return ((float)total_corecte / (float)NR_INTREBARI) * (float)NOTA_MAX;
}

/* ── proceseaza_client ───────────────────────────────────────────────────────── */
void proceseaza_client(int descriptor_client, struct sockaddr_in *adresa_clt) {
    char buffer_citire[BUFFER_SIZE];  
    char buffer_raspuns[BUFFER_SIZE]; 

    (void)printf("[Server] Client conectat: %s\n", inet_ntoa(adresa_clt->sin_addr));

    while (1) {
        (void)memset(buffer_citire, 0, BUFFER_SIZE); // NOLINT
        ssize_t octeti_primiti = recv(descriptor_client, buffer_citire, (size_t)BUFFER_SIZE - 1, 0); 
        
        if (octeti_primiti <= 0) {
            break;
        }
        
        buffer_citire[octeti_primiti] = '\0'; 

        if (strncmp(buffer_citire, "grade:", (size_t)PREFIX_LEN) == 0) {
            const char *cale_img = buffer_citire + PREFIX_LEN; 
            float nota_obtinuta = evalueaza_test(cale_img); 

            (void)memset(buffer_raspuns, 0, BUFFER_SIZE); 
            if (nota_obtinuta < 0.0F) {
                (void)strncpy(buffer_raspuns, "Eroare: Imagine invalida!", (size_t)BUFFER_SIZE - 1); // NOLINT
            } else {
                /* Folosim snprintf pentru a evita constructia manuala greoaie */
                (void)snprintf(buffer_raspuns, BUFFER_SIZE, "Nota: %.1F / 10", nota_obtinuta); // NOLINT
            }

            (void)printf("[Server] Raspuns: %s\n", buffer_raspuns);
            (void)send(descriptor_client, buffer_raspuns, strlen(buffer_raspuns), 0); 

            } else if (strcmp(buffer_citire, "quit") == 0) {
            /* Am înlocuit 12 cu constanta definită în enum */
            (void)send(descriptor_client, "La revedere!", (size_t)BYE_MSG_LEN, 0);
            break;
        } else {
            const char *msg_err = "Comenzi: grade:<cale> | quit";
            (void)send(descriptor_client, msg_err, strlen(msg_err), 0);
        }
    }
    (void)close(descriptor_client); 
}

/* ── Main ────────────────────────────────────────────────────────────────────── */
int main(void) {
    int descriptor_server = socket(AF_INET, SOCK_STREAM, 0); 
    if (descriptor_server < 0) { 
        perror("[ERR] socket"); 
        return 1; 
    }

    int optiune_reutilizare = 1;
    (void)setsockopt(descriptor_server, SOL_SOCKET, SO_REUSEADDR, &optiune_reutilizare, (socklen_t)sizeof(optiune_reutilizare)); 

    struct sockaddr_in adresa_srv;
    (void)memset(&adresa_srv, 0, sizeof(adresa_srv)); 
    adresa_srv.sin_family = AF_INET;               
    adresa_srv.sin_port = htons(PORT_DEFAULT);             
    adresa_srv.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(descriptor_server, (struct sockaddr *)&adresa_srv, (socklen_t)sizeof(adresa_srv)) < 0) {
        perror("[ERR] bind"); 
        (void)close(descriptor_server); 
        return 1;
    }

    if (listen(descriptor_server, BACKLOG_LISTEN) < 0) { 
        perror("[ERR] listen"); 
        (void)close(descriptor_server); 
        return 1;
    }

    (void)printf("[Server] PORNIT pe portul %d.\n", PORT_DEFAULT);

    while (1) {
        struct sockaddr_in adresa_clt;
        socklen_t lungime_clt = (socklen_t)sizeof(adresa_clt);
        int descriptor_comunicare = accept(descriptor_server, (struct sockaddr *)&adresa_clt, &lungime_clt); 
        
        if (descriptor_comunicare < 0) {
            continue; 
        }
        
        proceseaza_client(descriptor_comunicare, &adresa_clt); 
    }

    (void)close(descriptor_server);
    return 0;
}
