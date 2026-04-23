#include <stdio.h>      /* printf, perror */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strcmp, strncmp, strlen */
#include <unistd.h>     /* close, pipe, read, write */
#include <sys/types.h>  /* pid_t - necesar explicit pentru clang-tidy */
#include <sys/socket.h> /* socket, bind, listen, accept, send, recv, SOL_SOCKET, SO_REUSEADDR */
#include <sys/wait.h>   /* waitpid */
#include <netinet/in.h> /* struct sockaddr_in, htons, htonl, INADDR_ANY */
#include <arpa/inet.h>  /* inet_ntop, INET_ADDRSTRLEN */

enum ConfigSrv {
    PORT_DEFAULT    = 9090,
    BUFFER_SIZE     = 4096,
    NR_INTREBARI    = 40,
    BACKLOG_LISTEN  = 5,
    PREFIX_LEN      = 6,
    NOTA_MAX_INT    = 10,
    BYE_MSG_LEN     = 12
};

static const float FACTOR_ZECIMALA = 10.0F;

extern int incarca_imagine_opencv(const char* cale);
extern int proceseaza_intrebare_opencv(int nr_intrebare);

// Formatarea mesajului cu nota 
void formateaza_raspuns_nota(char *buffer, float nota) {
    int parte_intreaga = (int)nota;
    int zecimala = (int)((nota - (float)parte_intreaga) * FACTOR_ZECIMALA);
    
    const char *prefix_nota = "Nota: ";
    size_t pos = 0;
    
    while (prefix_nota[pos] != '\0' && pos < (size_t)BUFFER_SIZE - 1) {
        buffer[pos] = prefix_nota[pos];
        pos++;
    }
    
    if (parte_intreaga == NOTA_MAX_INT) {
        buffer[pos++] = '1';
        buffer[pos++] = '0';
    } else {
        buffer[pos++] = (char)(parte_intreaga + '0');
    }
    
    buffer[pos++] = '.';
    buffer[pos++] = (char)(zecimala + '0');
    
    const char *suffix_nota = " / 10";
    size_t idx_suffix = 0;
    while (suffix_nota[idx_suffix] != '\0' && pos < (size_t)BUFFER_SIZE - 1) {
        buffer[pos] = suffix_nota[idx_suffix];
        pos++;
        idx_suffix++;
    }
    buffer[pos] = '\0';
}

// Eroare pentru fisier invalid
void formateaza_eroare_imagine(char *buffer) {
    const char *msg_err = "Eroare: Imagine invalida!";
    size_t contor = 0;
    while (msg_err[contor] != '\0' && contor < (size_t)BUFFER_SIZE - 1) {
        buffer[contor] = msg_err[contor];
        contor++;
    }
    buffer[contor] = '\0';
}

float evalueaza_test(const char *cale_imagine) {
    (void)printf("\n[Server] Incepe evaluarea: %s\n", cale_imagine);

    if (incarca_imagine_opencv(cale_imagine) == 0) {
        (void)printf("[ERR] Nu am putut incarca imaginea: %s\n", cale_imagine);
        return -1.0F;
    }

    int pipe_fd[NR_INTREBARI][2]; 
    pid_t pids[NR_INTREBARI];      
    
    // Crearea fii pentru fiecare intrebare
    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        if (pipe(pipe_fd[contor]) < 0) { continue; }
        pid_t pid_fiu = fork(); 
        if (pid_fiu == 0) { 
            // Simuleaza corectarea unei intrebari, trimite rezultatul si se inchide imediat dupa
            (void)close(pipe_fd[contor][0]); 
            int este_corect = proceseaza_intrebare_opencv(contor + 1);
            (void)write(pipe_fd[contor][1], &este_corect, sizeof(int));
            (void)close(pipe_fd[contor][1]); 
            _exit(0);
        }
        (void)close(pipe_fd[contor][1]); 
        pids[contor] = pid_fiu;         
    }

    int total_corecte = 0; 
    // Adunarea totalului primind de la fiecare fiu punctele
    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        int rezultat_fiu = 0;
        (void)read(pipe_fd[contor][0], &rezultat_fiu, sizeof(int)); 
        (void)close(pipe_fd[contor][0]); 
        total_corecte += rezultat_fiu; 
        // Asteptam fiecare fiu sa se termine 
        (void)waitpid(pids[contor], NULL, 0); 
    }
    return ((float)total_corecte / (float)NR_INTREBARI) * (float)NOTA_MAX_INT;
}

void proceseaza_client(int descriptor_client, struct sockaddr_in *adresa_clt) {
    char buffer_citire[BUFFER_SIZE] = {0};  
    char buffer_raspuns[BUFFER_SIZE] = {0}; 
    char ip_client[INET_ADDRSTRLEN] = {0};

    // Transformarea adresei intr-un format text
    (void)inet_ntop(AF_INET, &(adresa_clt->sin_addr), ip_client, INET_ADDRSTRLEN);
    (void)printf("[Server] Client conectat: %s\n", ip_client);

    while (1) {
        for(size_t idx = 0; idx < (size_t)BUFFER_SIZE; idx++) { buffer_citire[idx] = '\0'; }
        ssize_t octeti_primiti = recv(descriptor_client, buffer_citire, (size_t)BUFFER_SIZE - 1, 0); 
        if (octeti_primiti <= 0) { break; }
        buffer_citire[octeti_primiti] = '\0'; 

        if (strncmp(buffer_citire, "grade:", (size_t)PREFIX_LEN) == 0) {
            float nota = evalueaza_test(buffer_citire + PREFIX_LEN); 
            for(size_t idx = 0; idx < (size_t)BUFFER_SIZE; idx++) { buffer_raspuns[idx] = '\0'; }

            if (nota < 0.0F) {
                formateaza_eroare_imagine(buffer_raspuns);
            } else {
                formateaza_raspuns_nota(buffer_raspuns, nota);
            }
            (void)send(descriptor_client, buffer_raspuns, strlen(buffer_raspuns), 0); 
        } else if (strcmp(buffer_citire, "quit") == 0) {
            (void)send(descriptor_client, "La revedere!", (size_t)BYE_MSG_LEN, 0);
            break;
        }
    }
    (void)close(descriptor_client); 
}

int main(void) {
    int descriptor_server = socket(AF_INET, SOCK_STREAM, 0); 
    if (descriptor_server < 0) { return 1; }

    int opt = 1;
    // Blocare port pentru acces imediat
    (void)setsockopt(descriptor_server, SOL_SOCKET, SO_REUSEADDR, &opt,(socklen_t)sizeof(opt)); // NOLINT(misc-include-cleaner)
    struct sockaddr_in adresa_srv = {0};
    adresa_srv.sin_family = AF_INET;               
    adresa_srv.sin_port = htons(PORT_DEFAULT);     
    // Ascultarea pe toate retelele disponibile
    adresa_srv.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(descriptor_server, (const struct sockaddr *)&adresa_srv, (socklen_t)sizeof(adresa_srv)) < 0) {
        (void)close(descriptor_server); 
        return 1;
    }
    // Creare coada pentru server
    if (listen(descriptor_server, BACKLOG_LISTEN) < 0) { 
        (void)close(descriptor_server); 
        return 1;
    }

    while (1) {
        struct sockaddr_in adresa_clt = {0};
        socklen_t lungime = (socklen_t)sizeof(adresa_clt);
        int descriptor_comunicare = accept(descriptor_server, (struct sockaddr *)&adresa_clt, &lungime); 
        if (descriptor_comunicare >= 0) {
            proceseaza_client(descriptor_comunicare, &adresa_clt); 
        }
    }
}
