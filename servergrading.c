#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Headerul aplicatiei mapat identic ca in client */
typedef struct {
    char magic[4];
    uint32_t file_size;
} app_header_t;

enum ConfigServer {
    PORT_INET            = 9090,
    MAX_CLIENTS_POLL     = 64,
    BUFFER_CAPACITY      = 4096,
    MAX_QUEUE_JOBS       = 128,
    MAX_HISTORY_RECORDS  = 50,
    NR_INTREBARI_TEST    = 40,
    TIMEOUT_ADMIN_SEC    = 30,
    POLL_TIMEOUT_MS      = 1000,
    LISTEN_BACKLOG       = 10,
    INVALID_DESCRIPTOR   = -1,
    STATUS_SUCCESS       = 0,
    STATUS_FAILURE       = 1
};

static const float FACTOR_ZECIMALA = 10.0F;
static const char* UNIX_SOCKET_PATH = "/tmp/omr_admin.sock";

extern int incarca_imagine_opencv(const char* cale);
extern int proceseaza_intrebare_opencv(int nr_intrebare);

typedef struct {
    unsigned int job_id;
    char file_path[BUFFER_CAPACITY];
    int client_socket_fd; /* Retinem socket-ul pentru a returna raspunsul sincron */
} omr_job_t;

typedef struct {
    omr_job_t jobs[MAX_QUEUE_JOBS];
    int head;
    int tail;
    int count;
    unsigned int next_job_id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} job_queue_t;

typedef struct {
    int active_inet_clients;
    int total_processed_jobs;
    int total_successful_jobs;
    double total_execution_time_ms;
    char current_processing_file[BUFFER_CAPACITY];
    char history[MAX_HISTORY_RECORDS][BUFFER_CAPACITY];
    int history_count;
    pthread_mutex_t mutex;
} server_metrics_t;

static job_queue_t      g_queue;
static server_metrics_t g_metrics;
static volatile sig_atomic_t g_server_running = 1;

static void safe_close(int fd) {
    if (fd != INVALID_DESCRIPTOR) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (close(fd) == INVALID_DESCRIPTOR) { perror("[WARN] Eroare close"); }
    }
}

static void adauga_in_istoric(const char* inreg) {
    pthread_mutex_lock(&g_metrics.mutex);
    int idx = g_metrics.history_count % MAX_HISTORY_RECORDS;
    strncpy(g_metrics.history[idx], inreg, BUFFER_CAPACITY - 1);
    g_metrics.history[idx][BUFFER_CAPACITY - 1] = '\0';
    g_metrics.history_count++;
    pthread_mutex_unlock(&g_metrics.mutex);
}

/* ========================================================================== */
/* COADA FIFO                                                                 */
/* ========================================================================== */

static void init_queue(void) {
    memset(&g_queue, 0, sizeof(job_queue_t));
    g_queue.next_job_id = 100;
    pthread_mutex_init(&g_queue.mutex, NULL);
    pthread_cond_init(&g_queue.cond, NULL);
}

static void enqueue_job(const char* cale, int clt_fd) {
    pthread_mutex_lock(&g_queue.mutex);
    if (g_queue.count >= MAX_QUEUE_JOBS) {
        /* Daca coada e plina, respingem pe loc */
        char *err = "ERR: Coada serverului este plina!";
        (void)send(clt_fd, err, strlen(err), 0);
        safe_close(clt_fd);
        pthread_mutex_unlock(&g_queue.mutex);
        return;
    }

    int t_idx = g_queue.tail;
    g_queue.jobs[t_idx].job_id = g_queue.next_job_id++;
    strncpy(g_queue.jobs[t_idx].file_path, cale, BUFFER_CAPACITY - 1);
    g_queue.jobs[t_idx].file_path[BUFFER_CAPACITY - 1] = '\0';
    g_queue.jobs[t_idx].client_socket_fd = clt_fd;

    g_queue.tail = (g_queue.tail + 1) % MAX_QUEUE_JOBS;
    g_queue.count++;

    pthread_cond_signal(&g_queue.cond);
    pthread_mutex_unlock(&g_queue.mutex);
}

/* ========================================================================== */
/* FIRUL DE LUCRU (Worker) - Corecteaza si Răspunde Sincron                   */
/* ========================================================================== */

static float executa_evaluare_test(const char *cale_imagine) {
    if (incarca_imagine_opencv(cale_imagine) == 0) { return -1.0F; }
    
    int total_corecte = 0;
    for (int contor = 1; contor <= NR_INTREBARI_TEST; contor++) {
        total_corecte += proceseaza_intrebare_opencv(contor);
    }
    return ((float)total_corecte / (float)NR_INTREBARI_TEST) * 10.0F;
}

static void* worker_thread_func(void* arg) {
    (void)arg;
    struct timespec start_t, end_t;
    char raspuns[BUFFER_CAPACITY];

    while (g_server_running) {
        pthread_mutex_lock(&g_queue.mutex);
        while (g_queue.count == 0 && g_server_running) {
            pthread_cond_wait(&g_queue.cond, &g_queue.mutex);
        }
        if (!g_server_running) { pthread_mutex_unlock(&g_queue.mutex); break; }

        int cur_idx = g_queue.head;
        char path_copie[BUFFER_CAPACITY];
        strncpy(path_copie, g_queue.jobs[cur_idx].file_path, BUFFER_CAPACITY - 1);
        path_copie[BUFFER_CAPACITY - 1] = '\0';
        
        int clt_sock = g_queue.jobs[cur_idx].client_socket_fd;
        unsigned int jid = g_queue.jobs[cur_idx].job_id;

        g_queue.head = (g_queue.head + 1) % MAX_QUEUE_JOBS;
        g_queue.count--;
        pthread_mutex_unlock(&g_queue.mutex);

        pthread_mutex_lock(&g_metrics.mutex);
        strncpy(g_metrics.current_processing_file, path_copie, BUFFER_CAPACITY - 1);
        pthread_mutex_unlock(&g_metrics.mutex);

        /* Procesare intensiva OpenCV */
        clock_gettime(CLOCK_MONOTONIC, &start_t);
        float nota = executa_evaluare_test(path_copie);
        clock_gettime(CLOCK_MONOTONIC, &end_t);
        
        double d_ms = (double)(end_t.tv_sec - start_t.tv_sec) * 1000.0 + 
                      (double)(end_t.tv_nsec - start_t.tv_nsec) / 1000000.0;

        /* Returnam raspunsul sincron direct pe socket-ul asteptator */
        memset(raspuns, 0, BUFFER_CAPACITY);
        if (nota < 0.0F) {
            strncpy(raspuns, "Eroare: Imaginea nu a putut fi procesata optic.", BUFFER_CAPACITY - 1);
        } else {
            int p_int = (int)nota;
            int zec   = (int)((nota - (float)p_int) * FACTOR_ZECIMALA);
            // NOLINTNEXTLINE(stdio-snprintf, security-snprintf)
            snprintf(raspuns, BUFFER_CAPACITY, "Succes! Nota acordata: %d.%d / 10", p_int, zec);
        }

        if (clt_sock != INVALID_DESCRIPTOR) {
            (void)send(clt_sock, raspuns, strlen(raspuns), 0);
            safe_close(clt_sock); /* Final: inchidem conexiunea conform pasilor */
            
            pthread_mutex_lock(&g_metrics.mutex);
            g_metrics.active_inet_clients--;
            pthread_mutex_unlock(&g_metrics.mutex);
        }

        /* Curatam fisierul temporar de pe disc concomitent */
        (void)unlink(path_copie);

        char inreg_ist[BUFFER_CAPACITY];
        // NOLINTNEXTLINE(stdio-snprintf, security-snprintf)
        snprintf(inreg_ist, BUFFER_CAPACITY, "JOB: %u | Fişier: %s | Timp: %.2f ms | %s", 
                 jid, path_copie, d_ms, raspuns);
        adauga_in_istoric(inreg_ist);

        pthread_mutex_lock(&g_metrics.mutex);
        g_metrics.current_processing_file[0] = '\0';
        g_metrics.total_processed_jobs++;
        if (nota >= 0.0F) { g_metrics.total_successful_jobs++; }
        g_metrics.total_execution_time_ms += d_ms;
        pthread_mutex_unlock(&g_metrics.mutex);
    }
    return NULL;
}

/* ========================================================================== */
/* FIRUL INET CU POLL() - Receptie Header + Salvare Concomitenta              */
/* ========================================================================== */

static void* inet_thread_func(void* arg) {
    (void)arg;
    int srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock < 0) { return NULL; }

    int opt = 1;
    // NOLINTNEXTLINE(misc-include-cleaner)
    (void)setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t)sizeof(opt));

    struct sockaddr_in srv_addr = {0};
    srv_addr.sin_family      = AF_INET;
    srv_addr.sin_port        = htons(PORT_INET);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv_sock, (struct sockaddr*)&srv_addr, (socklen_t)sizeof(srv_addr)) < 0) {
        safe_close(srv_sock); return NULL;
    }
    if (listen(srv_sock, LISTEN_BACKLOG) < 0) { safe_close(srv_sock); return NULL; }

    struct pollfd fds[MAX_CLIENTS_POLL];
    for (int i = 0; i < MAX_CLIENTS_POLL; i++) { fds[i].fd = INVALID_DESCRIPTOR; fds[i].events = 0; }
    fds[0].fd = srv_sock; fds[0].events = POLLIN;

    char buf[BUFFER_CAPACITY];

    while (g_server_running) {
        int p_cnt = poll(fds, MAX_CLIENTS_POLL, POLL_TIMEOUT_MS);
        if (p_cnt <= 0) { continue; }

        /* Acceptam conexiuni noi */
        if ((fds[0].revents & POLLIN) != 0) {
            int new_sock = accept(srv_sock, NULL, NULL);
            if (new_sock >= 0) {
                for (int i = 1; i < MAX_CLIENTS_POLL; i++) {
                    if (fds[i].fd == INVALID_DESCRIPTOR) {
                        fds[i].fd = new_sock; fds[i].events = POLLIN;
                        pthread_mutex_lock(&g_metrics.mutex);
                        g_metrics.active_inet_clients++;
                        pthread_mutex_unlock(&g_metrics.mutex);
                        break;
                    }
                }
            }
        }

        /* Citim date de pe clientii existenti */
        for (int i = 1; i < MAX_CLIENTS_POLL; i++) {
            if (fds[i].fd != INVALID_DESCRIPTOR && ((fds[i].revents & POLLIN) != 0)) {
                int clt_fd = fds[i].fd;
                
                /* 1. Citim HEADER-UL aplicatiei (Mesajul 1) */
                app_header_t header;
                ssize_t h_bytes = recv(clt_fd, &header, sizeof(app_header_t), MSG_WAITALL);
                
                if (h_bytes == (ssize_t)sizeof(app_header_t) && strncmp(header.magic, "OMR", 3) == 0) {
                    uint32_t file_size = ntohl(header.file_size);
                    
                    /* 2. Deschidem un fisier temporar concomitent pe disc */
                    char temp_path[BUFFER_CAPACITY];
                    // NOLINTNEXTLINE(stdio-snprintf, security-snprintf)
                    snprintf(temp_path, BUFFER_CAPACITY, "/tmp/omr_recv_%d_%ld.png", clt_fd, time(NULL));
                    
                    int out_fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                    if (out_fd >= 0) {
                        /* 3. Citim octetii din socket in bucla si ii scriem pe disc */
                        uint32_t ramasi = file_size;
                        while (ramasi > 0) {
                            size_t de_citit = (ramasi > (uint32_t)BUFFER_CAPACITY) ? (size_t)BUFFER_CAPACITY : (size_t)ramasi;
                            ssize_t cititi = recv(clt_fd, buf, de_citit, 0);
                            if (cititi <= 0) { break; }
                            
                            (void)write(out_fd, buf, (size_t)cititi);
                            ramasi -= (uint32_t)cititi;
                        }
                        safe_close(out_fd);

                        if (ramasi == 0) {
                            /* Scoatem descriptorul din poll() pentru a nu declansa evenimente, 
                               dar IL LASAM DESCHIS si il pasam in coada FIFO! */
                            fds[i].fd = INVALID_DESCRIPTOR;
                            enqueue_job(temp_path, clt_fd);
                            continue; 
                        }
                        (void)unlink(temp_path); /* Corupt */
                    }
                }
                
                /* Daca protocolul a esuat sau clientul a dat deconectare */
                safe_close(clt_fd);
                fds[i].fd = INVALID_DESCRIPTOR;
                pthread_mutex_lock(&g_metrics.mutex);
                g_metrics.active_inet_clients--;
                pthread_mutex_unlock(&g_metrics.mutex);
            }
        }
    }
    for (int i = 0; i < MAX_CLIENTS_POLL; i++) { safe_close(fds[i].fd); }
    return NULL;
}

/* ========================================================================== */
/* FIRUL ADMIN (UNIX SOCKET) - Ramane identic si functional                   */
/* ========================================================================== */
/* (Inlocuieste cu implementarea admin_thread_func existenta din blocul anterior) */
static void* admin_thread_func(void* arg) {
    (void)arg;
    int unix_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_sock < 0) { perror("[ERR] socket UNIX"); return NULL; }

    (void)unlink(UNIX_SOCKET_PATH);
    struct sockaddr_un un_addr;
    memset(&un_addr, 0, sizeof(struct sockaddr_un));
    un_addr.sun_family = AF_UNIX;
    strncpy(un_addr.sun_path, UNIX_SOCKET_PATH, sizeof(un_addr.sun_path) - 1);

    if (bind(unix_sock, (struct sockaddr*)&un_addr, (socklen_t)sizeof(struct sockaddr_un)) < 0) {
        perror("[ERR] bind UNIX"); safe_close(unix_sock); return NULL;
    }

    if (listen(unix_sock, 1) < 0) {
        perror("[ERR] listen UNIX"); safe_close(unix_sock); return NULL;
    }

    int active_admin_fd = INVALID_DESCRIPTOR;
    time_t last_activity_time = 0;
    char buf[BUFFER_CAPACITY];
    char raport[BUFFER_CAPACITY * 4];

    struct pollfd pfd;
    pfd.events = POLLIN;

    while (g_server_running) {
        /* Daca nu avem admin, ascultam pe socketul principal, altfel pe socketul adminului curent */
        pfd.fd = (active_admin_fd == INVALID_DESCRIPTOR) ? unix_sock : active_admin_fd;
        int pcount = poll(&pfd, 1, POLL_TIMEOUT_MS);

        if (pcount < 0) { if (errno == EINTR) { continue; } break; }

        /* Gestiunea TIMEOUT-ului de inactivitate (Cerinta Nivel A) */
        if (active_admin_fd != INVALID_DESCRIPTOR) {
            time_t now = time(NULL);
            if (now - last_activity_time > TIMEOUT_ADMIN_SEC) {
                const char* msg_to = "\n[Server] Deconectat pentru inactivitate.\n";
                (void)send(active_admin_fd, msg_to, strlen(msg_to), 0);
                safe_close(active_admin_fd);
                active_admin_fd = INVALID_DESCRIPTOR;
                printf("[Server Admin] Administrator deconectat automat (Timeout %ds).\n", TIMEOUT_ADMIN_SEC);
                continue;
            }
        }

        if (pcount == 0) { continue; }

        /* Logică de ACCEPT (Conectare Admin Nou) */
        if (active_admin_fd == INVALID_DESCRIPTOR && ((pfd.revents & POLLIN) != 0)) {
            active_admin_fd = accept(unix_sock, NULL, NULL);
            if (active_admin_fd >= 0) {
                last_activity_time = time(NULL);
                printf("[Server Admin] Administrator nou conectat.\n");
            }
        } 
        /* Logică de COMENZI (Admin existent trimite raport) */
        else if (active_admin_fd != INVALID_DESCRIPTOR && ((pfd.revents & POLLIN) != 0)) {
            memset(buf, 0, BUFFER_CAPACITY);
            ssize_t bytes = recv(active_admin_fd, buf, BUFFER_CAPACITY - 1, 0);

            if (bytes <= 0) {
                safe_close(active_admin_fd);
                active_admin_fd = INVALID_DESCRIPTOR;
                printf("[Server Admin] Administrator deconectat.\n");
                continue;
            }

            last_activity_time = time(NULL);
            buf[bytes] = '\0';
            memset(raport, 0, sizeof(raport));

            /* Generare Rapoarte (Cele 6 categorii din barem) */
            pthread_mutex_lock(&g_metrics.mutex);
            if (strcmp(buf, "report:clients") == 0) {
                sprintf(raport, "Clienti INET activi: %d", g_metrics.active_inet_clients);
            } else if (strcmp(buf, "report:queue") == 0) {
                pthread_mutex_lock(&g_queue.mutex);
                sprintf(raport, "Sarcini in coada FIFO: %d / %d", g_queue.count, MAX_QUEUE_JOBS);
                pthread_mutex_unlock(&g_queue.mutex);
            } else if (strcmp(buf, "report:current") == 0) {
                sprintf(raport, "In executie: %s", (strlen(g_metrics.current_processing_file) > 0) ? g_metrics.current_processing_file : "Nimic");
            } else if (strcmp(buf, "report:time") == 0) {
                double avg = (g_metrics.total_processed_jobs > 0) ? (g_metrics.total_execution_time_ms / g_metrics.total_processed_jobs) : 0;
                sprintf(raport, "Durata medie: %.2f ms", avg);
            } else if (strcmp(buf, "report:history") == 0) {
                strcpy(raport, "--- Istoric Ultimelor Corectari ---\n");
                int start = (g_metrics.history_count > MAX_HISTORY_RECORDS) ? g_metrics.history_count - MAX_HISTORY_RECORDS : 0;
                for (int i = start; i < g_metrics.history_count; i++) {
                    strcat(raport, g_metrics.history[i % MAX_HISTORY_RECORDS]);
                    strcat(raport, "\n");
                }
            } else if (strcmp(buf, "report:success") == 0) {
                double r = (g_metrics.total_processed_jobs > 0) ? ((double)g_metrics.total_successful_jobs / g_metrics.total_processed_jobs * 100) : 0;
                sprintf(raport, "Rata succes: %.2f%% (%d/%d)", r, g_metrics.total_successful_jobs, g_metrics.total_processed_jobs);
            } else {
                strcpy(raport, "Comanda admin necunoscuta.");
            }
            pthread_mutex_unlock(&g_metrics.mutex);

            (void)send(active_admin_fd, raport, strlen(raport), 0);
        }
    }

    safe_close(unix_sock);
    unlink(UNIX_SOCKET_PATH);
    return NULL;
}
int main(void) {
    init_queue();
    pthread_mutex_init(&g_metrics.mutex, NULL);
    printf("[Server] Pornit pe portul %d cu transport binar de fisiere (Nivel A)...\n", PORT_INET);

    pthread_t t_w, t_i, t_a;
    pthread_create(&t_w, NULL, worker_thread_func, NULL);
    pthread_create(&t_i, NULL, inet_thread_func, NULL);
    pthread_create(&t_a, NULL, admin_thread_func, NULL);

    while (g_server_running) { sleep(10); } // NOLINT(concurrency-mt-unsafe)
    return 0;
}
