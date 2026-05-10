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

enum ConfigSrvExtins {
    SOCKET_OPTION_ENABLE = 1,
    EXIT_STATUS_FAILURE  = 1,
    EXIT_STATUS_SUCCESS  = 0,
    INVALID_DESCRIPTOR   = -1
};

static const float FACTOR_ZECIMALA = 10.0F;

extern int incarca_imagine_opencv(const char* cale);
extern int proceseaza_intrebare_opencv(int nr_intrebare);

static void safe_close(int descriptor) {
    if (descriptor != INVALID_DESCRIPTOR) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (close(descriptor) == INVALID_DESCRIPTOR) {
            perror("[ERR] close");
        }
    }
}

void formateaza_raspuns_nota(char *buffer, float nota) {
    int parte_intreaga = (int)nota;
    int zecimala = (int)((nota - (float)parte_intreaga) * FACTOR_ZECIMALA);
    
    const char *prefix_nota = "Nota: ";
    size_t pos = 0;
    
    while (prefix_nota[pos] != '\0' && pos < (size_t)(BUFFER_SIZE - 1)) {
        buffer[pos] = prefix_nota[pos];
        pos++;
    }
    
    if (parte_intreaga == NOTA_MAX_INT) {
        if (pos < (size_t)(BUFFER_SIZE - 2)) {
            buffer[pos++] = '1';
            buffer[pos++] = '0';
        }
    } else {
        if (pos < (size_t)(BUFFER_SIZE - 1)) {
            buffer[pos++] = (char)(parte_intreaga + '0');
        }
    }
    
    if (pos < (size_t)(BUFFER_SIZE - 2)) {
        buffer[pos++] = '.';
        buffer[pos++] = (char)(zecimala + '0');
    }
    
    const char *suffix_nota = " / 10";
    size_t idx_suffix = 0;
    while (suffix_nota[idx_suffix] != '\0' && pos < (size_t)(BUFFER_SIZE - 1)) {
        buffer[pos] = suffix_nota[idx_suffix];
        pos++;
        idx_suffix++;
    }
    buffer[pos] = '\0';
}

void formateaza_eroare_imagine(char *buffer) {
    const char *msg_err = "Eroare: Imagine invalida!";
    size_t contor = 0;
    while (msg_err[contor] != '\0' && contor < (size_t)(BUFFER_SIZE - 1)) {
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
    
    // Initializare ferma pentru prevenirea citirilor din memorie neinitializata
    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        pipe_fd[contor][0] = INVALID_DESCRIPTOR;
        pipe_fd[contor][1] = INVALID_DESCRIPTOR;
        pids[contor]       = INVALID_DESCRIPTOR;
    }
    
    // Crearea proceselor fiu cu verificarea obligatorie a valorilor de retur
    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        if (pipe(pipe_fd[contor]) < 0) { 
            perror("[ERR] pipe");
            continue; 
        }
        
        pid_t pid_fiu = fork(); 
        if (pid_fiu < 0) {
            perror("[ERR] fork");
            safe_close(pipe_fd[contor][0]);
            safe_close(pipe_fd[contor][1]);
            pipe_fd[contor][0] = INVALID_DESCRIPTOR;
            pipe_fd[contor][1] = INVALID_DESCRIPTOR;
            continue;
        }
        
        if (pid_fiu == 0) { 
            safe_close(pipe_fd[contor][0]); 
            int este_corect = proceseaza_intrebare_opencv(contor + 1);
            ssize_t res_write = write(pipe_fd[contor][1], &este_corect, sizeof(int));
            if (res_write < 0) {
                perror("[ERR] write fiu");
            }
            safe_close(pipe_fd[contor][1]); 
            _exit(EXIT_STATUS_SUCCESS);
        }
        
        safe_close(pipe_fd[contor][1]); 
        pipe_fd[contor][1] = INVALID_DESCRIPTOR;
        pids[contor] = pid_fiu;         
    }

    int total_corecte = 0; 
    
    // Extragerea rezultatelor si asteptarea sincronizata
    for (int contor = 0; contor < NR_INTREBARI; contor++) {
        if (pipe_fd[contor][0] != INVALID_DESCRIPTOR) {
            int rezultat_fiu = 0;
            ssize_t res_read = read(pipe_fd[contor][0], &rezultat_fiu, sizeof(int)); 
            if (res_read == (ssize_t)sizeof(int)) {
                total_corecte += rezultat_fiu; 
            } else if (res_read < 0) {
                perror("[ERR] read parinte");
            }
            safe_close(pipe_fd[contor][0]); 
            pipe_fd[contor][0] = INVALID_DESCRIPTOR;
        }
        
        if (pids[contor] != INVALID_DESCRIPTOR) {
            (void)waitpid(pids[contor], NULL, 0); 
        }
    }
    return ((float)total_corecte / (float)NR_INTREBARI) * (float)NOTA_MAX_INT;
}

// Folosirea const pointer pentru argumentele imutabile partajate
void proceseaza_client(int descriptor_client, const struct sockaddr_in *adresa_clt) {
    char buffer_citire[BUFFER_SIZE]  = {0};  
    char buffer_raspuns[BUFFER_SIZE] = {0}; 
    char ip_client[INET_ADDRSTRLEN]  = {0};

    if (inet_ntop(AF_INET, &(adresa_clt->sin_addr), ip_client, (socklen_t)INET_ADDRSTRLEN) == NULL) {
        ip_client[0] = '?';
        ip_client[1] = '\0';
    }
    (void)printf("[Server] Client conectat: %s\n", ip_client);

    while (1) {
        for (size_t idx = 0; idx < (size_t)BUFFER_SIZE; idx++) { buffer_citire[idx] = '\0'; }
        ssize_t octeti_primiti = recv(descriptor_client, buffer_citire, (size_t)(BUFFER_SIZE - 1), 0); 
        if (octeti_primiti <= 0) { break; }
        buffer_citire[octeti_primiti] = '\0'; 

        if (strncmp(buffer_citire, "grade:", (size_t)PREFIX_LEN) == 0) {
            float nota = evalueaza_test(buffer_citire + PREFIX_LEN); 
            for (size_t idx = 0; idx < (size_t)BUFFER_SIZE; idx++) { buffer_raspuns[idx] = '\0'; }

            if (nota < 0.0F) {
                formateaza_eroare_imagine(buffer_raspuns);
            } else {
                formateaza_raspuns_nota(buffer_raspuns, nota);
            }
            ssize_t res_s = send(descriptor_client, buffer_raspuns, strlen(buffer_raspuns), 0); 
            if (res_s < 0) { perror("[ERR] send raspuns"); }
        } else if (strcmp(buffer_citire, "quit") == 0) {
            ssize_t res_s = send(descriptor_client, "La revedere!", (size_t)BYE_MSG_LEN, 0);
            if (res_s < 0) { perror("[ERR] send bye"); }
            break;
        }
    }
    safe_close(descriptor_client); 
}

int main(void) {
    int descriptor_server = socket(AF_INET, SOCK_STREAM, 0); 
    if (descriptor_server < 0) { 
        perror("[ERR] socket");
        return EXIT_STATUS_FAILURE; 
    }

    int opt = SOCKET_OPTION_ENABLE;
    // NOLINTNEXTLINE(misc-include-cleaner)
    if (setsockopt(descriptor_server, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t)sizeof(opt)) < 0) {
        perror("[ERR] setsockopt");
    }
    
    // Initializare la zero a intregii structuri pentru prevenirea uninitialized assign
    struct sockaddr_in adresa_srv = {0};
    adresa_srv.sin_family      = AF_INET;               
    adresa_srv.sin_port        = htons(PORT_DEFAULT);     
    adresa_srv.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(descriptor_server, (const struct sockaddr *)&adresa_srv, (socklen_t)sizeof(adresa_srv)) < 0) {
        perror("[ERR] bind");
        safe_close(descriptor_server); 
        return EXIT_STATUS_FAILURE;
    }
    
    if (listen(descriptor_server, BACKLOG_LISTEN) < 0) { 
        perror("[ERR] listen");
        safe_close(descriptor_server); 
        return EXIT_STATUS_FAILURE;
    }

    (void)printf("[Server] Pornit si asculta pe portul %d...\n", PORT_DEFAULT);

    while (1) {
        struct sockaddr_in adresa_clt = {0};
        socklen_t lungime = (socklen_t)sizeof(adresa_clt);
        int descriptor_comunicare = accept(descriptor_server, (struct sockaddr *)&adresa_clt, &lungime); 
        if (descriptor_comunicare >= 0) {
            proceseaza_client(descriptor_comunicare, &adresa_clt); 
        } else {
            perror("[ERR] accept");
        }
    }
    
    safe_close(descriptor_server);
    return EXIT_STATUS_SUCCESS;
}
