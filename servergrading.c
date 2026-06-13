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
#include <sys/inotify.h>

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

static const char* UNIX_SOCKET_PATH = "/tmp/omr_admin.sock";

struct Mat_t;
extern struct Mat_t* incarca_imagine_opencv(const char* cale_fisier);
extern int proceseaza_intrebare_opencv(struct Mat_t* imagine, int index_intrebare, char raspuns_corect);
extern void pCvMatDelete(struct Mat_t* wrapper); 

static char barem[40];

typedef struct {
    unsigned int job_id;
    char file_path[BUFFER_CAPACITY];
    int client_socket_fd;
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

static void adauga_in_istoric(const char* inreg) {
    pthread_mutex_lock(&g_metrics.mutex);
    int idx = g_metrics.history_count % MAX_HISTORY_RECORDS;
    strncpy(g_metrics.history[idx], inreg, BUFFER_CAPACITY - 1);
    g_metrics.history[idx][BUFFER_CAPACITY - 1] = '\0';
    g_metrics.history_count++;
    pthread_mutex_unlock(&g_metrics.mutex);
}

static void init_queue(void) {
    memset(&g_queue, 0, sizeof(job_queue_t));
    g_queue.next_job_id = 100;
    pthread_mutex_init(&g_queue.mutex, NULL);
    pthread_cond_init(&g_queue.cond, NULL);
}

static void enqueue_job(const char* cale, int clt_fd) {
    pthread_mutex_lock(&g_queue.mutex);
    if (g_queue.count >= MAX_QUEUE_JOBS) {
        char *err = "ERR: Coada serverului este plina!";
        send(clt_fd, err, strlen(err), 0);
        if (clt_fd != INVALID_DESCRIPTOR) close(clt_fd);
        pthread_mutex_unlock(&g_queue.mutex);
        return;
    }

    int t_idx = g_queue.tail;
    g_queue.jobs[t_idx].job_id = g_queue.next_job_id++;
    strncpy(g_queue.jobs[t_idx].file_path, cale, BUFFER_CAPACITY - 1);
    g_queue.jobs[t_idx].client_socket_fd = clt_fd;

    g_queue.tail = (g_queue.tail + 1) % MAX_QUEUE_JOBS;
    g_queue.count++;

    pthread_cond_signal(&g_queue.cond);
    pthread_mutex_unlock(&g_queue.mutex);
}

static int incarca_barem_din_fisier(const char* cale_fisier) {
    FILE* f = fopen(cale_fisier, "r");
    if (!f) {
        perror("[ERR] Nu am putut deschide fisierul de barem");
        return -1;
    }

    char linie[64];
    int gasite = 0;
    
    while (fgets(linie, sizeof(linie), f)) {
        int nr_intrebare;
        char raspuns_corect;
        if (sscanf(linie, "%d: %c", &nr_intrebare, &raspuns_corect) == 2) {
            if (nr_intrebare >= 1 && nr_intrebare <= NR_INTREBARI_TEST) {
                barem[nr_intrebare - 1] = raspuns_corect; 
                gasite++;
            }
        }
    }
    fclose(f);

    if (gasite != NR_INTREBARI_TEST) {
        printf("[ERR] Baremul '%s' este invalid sau incomplet! Am gasit doar %d raspunsuri din %d.\n", 
               cale_fisier, gasite, NR_INTREBARI_TEST);
        return -1;
    }
    
    printf("[Server] Barem incarcat cu succes din '%s'.\n", cale_fisier);
    return 0;
}

static float executa_evaluare_test(const char *cale_imagine) {
    struct Mat_t* imagine = incarca_imagine_opencv(cale_imagine);
    if (imagine == NULL) return -1.0F; 
    
    int total_corecte = 0;
    for (int i = 0; i < NR_INTREBARI_TEST; i++) {
        total_corecte += proceseaza_intrebare_opencv(imagine, i, barem[i]);
    }
    
    pCvMatDelete(imagine);
    return ((float)total_corecte / (float)NR_INTREBARI_TEST) * 10.0F;
}

static void* worker_thread_func(void* arg) {
    (void)arg;
    struct timespec start_t, end_t;
    char raspuns[256];
    char path_copie[BUFFER_CAPACITY];

    while (g_server_running) {
        pthread_mutex_lock(&g_queue.mutex);
        while (g_queue.count == 0 && g_server_running) {
            pthread_cond_wait(&g_queue.cond, &g_queue.mutex);
        }
        if (!g_server_running) { pthread_mutex_unlock(&g_queue.mutex); break; }

        int cur_idx = g_queue.head;
        strncpy(path_copie, g_queue.jobs[cur_idx].file_path, BUFFER_CAPACITY - 1);
        int clt_sock = g_queue.jobs[cur_idx].client_socket_fd;
        unsigned int jid = g_queue.jobs[cur_idx].job_id;

        g_queue.head = (g_queue.head + 1) % MAX_QUEUE_JOBS;
        g_queue.count--;
        pthread_mutex_unlock(&g_queue.mutex);

        pthread_mutex_lock(&g_metrics.mutex);
        strncpy(g_metrics.current_processing_file, path_copie, BUFFER_CAPACITY - 1);
        pthread_mutex_unlock(&g_metrics.mutex);

        clock_gettime(CLOCK_MONOTONIC, &start_t);
        float nota = executa_evaluare_test(path_copie);
        clock_gettime(CLOCK_MONOTONIC, &end_t);

        double d_ms = (double)(end_t.tv_sec - start_t.tv_sec) * 1000.0 +
                      (double)(end_t.tv_nsec - start_t.tv_nsec) / 1000000.0;

        memset(raspuns, 0, 256);
        if (nota < 0.0F) {
            snprintf(raspuns, 256, "Eroare: Imaginea nu a putut fi procesata optic.");
        } else {
            snprintf(raspuns, 256, "Succes! Nota acordata: %.2f / 10", nota);
        }

        if (clt_sock != INVALID_DESCRIPTOR) {
            // 1. Trimitem textul pe un buffer fix de 256 octeți (Protocol)
            char text_buffer[256];
            memset(text_buffer, 0, 256);
            strncpy(text_buffer, raspuns, 255);
            send(clt_sock, text_buffer, 256, 0);

            // 2. Verificăm dacă avem poza rezultată pe disc
            int img_fd = open("/tmp/debug_calibrare.png", O_RDONLY);
            uint32_t file_size = 0;
            struct stat st;
            
            if (img_fd >= 0 && fstat(img_fd, &st) == 0) {
                file_size = st.st_size;
            }

            // 3. Trimitem dimensiunea pozei (sau 0 dacă a picat evaluarea)
            uint32_t net_size = htonl(file_size);
            send(clt_sock, &net_size, sizeof(net_size), 0);

            // 4. Dacă avem poză, o trimitem binar și curățăm serverul
            if (file_size > 0) {
                char buf[BUFFER_CAPACITY];
                ssize_t bytes_cititi;
                while ((bytes_cititi = read(img_fd, buf, BUFFER_CAPACITY)) > 0) {
                    send(clt_sock, buf, bytes_cititi, 0);
                }
                close(img_fd);
                unlink("/tmp/debug_calibrare.png"); // Ștergem dovada de pe server!
            }

            close(clt_sock);
            
            pthread_mutex_lock(&g_metrics.mutex);
            g_metrics.active_inet_clients--;
            pthread_mutex_unlock(&g_metrics.mutex);
        }

        unlink(path_copie);

        char inreg_ist[BUFFER_CAPACITY];
        snprintf(inreg_ist, BUFFER_CAPACITY, "JOB: %u | Timp: %.2f ms | %s", jid, d_ms, raspuns);
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

static void* inet_thread_func(void* arg) {
    (void)arg;
    int srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock < 0) return NULL; 

    int opt = 1;
    setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family      = AF_INET;
    srv_addr.sin_port        = htons(PORT_INET);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv_sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        close(srv_sock); return NULL;
    }
    if (listen(srv_sock, LISTEN_BACKLOG) < 0) { close(srv_sock); return NULL; }

    struct pollfd fds[MAX_CLIENTS_POLL];
    for (int i = 0; i < MAX_CLIENTS_POLL; i++) {
        fds[i].fd = INVALID_DESCRIPTOR;
        fds[i].events = 0;
    }
    fds[0].fd = srv_sock;
    fds[0].events = POLLIN;

    char buf[BUFFER_CAPACITY];

    while (g_server_running) {
        int p_cnt = poll(fds, MAX_CLIENTS_POLL, POLL_TIMEOUT_MS);
        if (p_cnt <= 0) continue; 

        if (fds[0].revents & POLLIN) {
            int new_sock = accept(srv_sock, NULL, NULL);
            if (new_sock >= 0) {
                for (int i = 1; i < MAX_CLIENTS_POLL; i++) {
                    if (fds[i].fd == INVALID_DESCRIPTOR) {
                        fds[i].fd = new_sock;
                        fds[i].events = POLLIN;
                        pthread_mutex_lock(&g_metrics.mutex);
                        g_metrics.active_inet_clients++;
                        pthread_mutex_unlock(&g_metrics.mutex);
                        break;
                    }
                }
            }
        }

        for (int i = 1; i < MAX_CLIENTS_POLL; i++) {
            if (fds[i].fd != INVALID_DESCRIPTOR && (fds[i].revents & POLLIN)) {
                int clt_fd = fds[i].fd;
                app_header_t header;
                ssize_t h_bytes = recv(clt_fd, &header, sizeof(app_header_t), MSG_WAITALL);
                
                if (h_bytes == sizeof(app_header_t) && strncmp(header.magic, "OMR", 3) == 0) {
                    uint32_t file_size = ntohl(header.file_size);
                    char temp_path[BUFFER_CAPACITY];
                    
                    snprintf(temp_path, BUFFER_CAPACITY, "/tmp/omr_recv_%d_%ld.png", clt_fd, (long)time(NULL));

                    int out_fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                    if (out_fd >= 0) {
                        uint32_t ramasi = file_size;
                        while (ramasi > 0) {
                            size_t de_citit = (ramasi > BUFFER_CAPACITY) ? BUFFER_CAPACITY : ramasi;
                            ssize_t cititi = recv(clt_fd, buf, de_citit, 0);
                            if (cititi <= 0) break; 
                            write(out_fd, buf, cititi);
                            ramasi -= cititi;
                        }
                        close(out_fd);
                        if (ramasi == 0) {
                            fds[i].fd = INVALID_DESCRIPTOR;
                            enqueue_job(temp_path, clt_fd);
                            continue;
                        }
                        unlink(temp_path);
                    }
                }
                if (clt_fd != INVALID_DESCRIPTOR) close(clt_fd);
                fds[i].fd = INVALID_DESCRIPTOR;
                pthread_mutex_lock(&g_metrics.mutex);
                g_metrics.active_inet_clients--;
                pthread_mutex_unlock(&g_metrics.mutex);
            }
        }
    }
    for (int i = 0; i < MAX_CLIENTS_POLL; i++) {
        if (fds[i].fd != INVALID_DESCRIPTOR) close(fds[i].fd);
    }
    return NULL;
}

static void* inotify_thread_func(void* arg) {
    (void)arg;
    int fd = inotify_init();
    if (fd < 0) { perror("[INotify] Eroare init"); return NULL; }

    int wd = inotify_add_watch(fd, "/tmp", IN_CREATE | IN_CLOSE_WRITE);
    if (wd < 0) { perror("[INotify] Eroare adaugare watch"); close(fd); return NULL; }

    char buffer[BUFFER_CAPACITY];
    printf("[INotify] Firul de monitorizare a kernelului pornit cu succes pentru /tmp...\n");

    while (g_server_running) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (ret <= 0) continue;

        ssize_t length = read(fd, buffer, sizeof(buffer));
        if (length < 0) break;

        int i = 0;
        while (i < length) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->len > 0) {
                if (strstr(event->name, "omr_recv_") != NULL) {
                    if (event->mask & IN_CREATE) {
                        printf("[INOTIFY EVENT] 📂 Clientul a inceput incarcarea: %s\n", event->name);
                    } else if (event->mask & IN_CLOSE_WRITE) {
                        printf("[INOTIFY EVENT] ✅ Incarcare finalizata pentru: %s\n", event->name);
                    }
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }

    inotify_rm_watch(fd, wd);
    close(fd);
    return NULL;
}

static void* admin_thread_func(void* arg) {
    (void)arg;
    int unix_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_sock < 0) { perror("[ERR] socket UNIX"); return NULL; }

    unlink(UNIX_SOCKET_PATH);
    struct sockaddr_un un_addr;
    memset(&un_addr, 0, sizeof(struct sockaddr_un));
    un_addr.sun_family = AF_UNIX;
    strncpy(un_addr.sun_path, UNIX_SOCKET_PATH, sizeof(un_addr.sun_path) - 1);

    if (bind(unix_sock, (struct sockaddr*)&un_addr, sizeof(un_addr)) < 0) {
        perror("[ERR] bind UNIX"); close(unix_sock); return NULL;
    }
    if (listen(unix_sock, 1) < 0) {
        perror("[ERR] listen UNIX"); close(unix_sock); return NULL;
    }

    int active_admin_fd = INVALID_DESCRIPTOR;
    time_t last_activity_time = 0;
    char buf[BUFFER_CAPACITY];
    char raport[BUFFER_CAPACITY * 4];

    struct pollfd pfd;
    pfd.events = POLLIN;

    while (g_server_running) {
        pfd.fd = (active_admin_fd == INVALID_DESCRIPTOR) ? unix_sock : active_admin_fd;
        int pcount = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (pcount < 0) { if (errno == EINTR) continue; break; }

        if (active_admin_fd != INVALID_DESCRIPTOR) {
            time_t now = time(NULL);
            if (now - last_activity_time > TIMEOUT_ADMIN_SEC) {
                const char* msg_to = "\n[Server] Deconectat pentru inactivitate.\n";
                send(active_admin_fd, msg_to, strlen(msg_to), 0);
                close(active_admin_fd);
                active_admin_fd = INVALID_DESCRIPTOR;
                printf("[Server Admin] Administrator deconectat automat.\n");
                continue;
            }
        }
        if (pcount == 0) continue; 

        if (active_admin_fd == INVALID_DESCRIPTOR && (pfd.revents & POLLIN)) {
            active_admin_fd = accept(unix_sock, NULL, NULL);
            if (active_admin_fd >= 0) {
                last_activity_time = time(NULL);
                printf("[Server Admin] Administrator conectat.\n");
            }
        } else if (active_admin_fd != INVALID_DESCRIPTOR && (pfd.revents & POLLIN)) {
            memset(buf, 0, BUFFER_CAPACITY);
            ssize_t bytes = recv(active_admin_fd, buf, BUFFER_CAPACITY - 1, 0);
            if (bytes <= 0) {
                close(active_admin_fd);
                active_admin_fd = INVALID_DESCRIPTOR;
                printf("[Server Admin] Administrator deconectat.\n");
                continue;
            }
            last_activity_time = time(NULL);
            buf[bytes] = '\0';
            buf[strcspn(buf, "\r\n")] = '\0';
            
            memset(raport, 0, sizeof(raport));

            pthread_mutex_lock(&g_metrics.mutex);
            if (strcmp(buf, "report:clients") == 0) {
                snprintf(raport, sizeof(raport), "Clienti INET activi: %d\n", g_metrics.active_inet_clients);
            } else if (strcmp(buf, "report:queue") == 0) {
                pthread_mutex_lock(&g_queue.mutex);
                snprintf(raport, sizeof(raport), "Sarcini in coada FIFO: %d / %d\n", g_queue.count, MAX_QUEUE_JOBS);
                pthread_mutex_unlock(&g_queue.mutex);
            } else if (strcmp(buf, "report:current") == 0) {
                snprintf(raport, sizeof(raport), "In executie: %s\n", 
                         strlen(g_metrics.current_processing_file) > 0 ? g_metrics.current_processing_file : "Nimic");
            } else if (strcmp(buf, "report:time") == 0) {
                double avg = (g_metrics.total_processed_jobs > 0) ?
                             (g_metrics.total_execution_time_ms / g_metrics.total_processed_jobs) : 0;
                snprintf(raport, sizeof(raport), "Durata medie: %.2f ms\n", avg);
            } else if (strcmp(buf, "report:history") == 0) {
                snprintf(raport, sizeof(raport), "--- Istoric Ultimelor Corectari ---\n");
                int start = (g_metrics.history_count > MAX_HISTORY_RECORDS) ?
                            g_metrics.history_count - MAX_HISTORY_RECORDS : 0;
                for (int i = start; i < g_metrics.history_count; i++) {
                    strncat(raport, g_metrics.history[i % MAX_HISTORY_RECORDS], sizeof(raport) - strlen(raport) - 1);
                    strncat(raport, "\n", sizeof(raport) - strlen(raport) - 1);
                }
            } else if (strcmp(buf, "report:success") == 0) {
                double r = (g_metrics.total_processed_jobs > 0) ?
                           ((double)g_metrics.total_successful_jobs / g_metrics.total_processed_jobs * 100) : 0;
                snprintf(raport, sizeof(raport), "Rata succes: %.2f%% (%d/%d)\n", 
                         r, g_metrics.total_successful_jobs, g_metrics.total_processed_jobs);
            } else if (strcmp(buf, "0") == 0) {
                snprintf(raport, sizeof(raport), "[Admin] Comanda 0 primita. Serverul se opreste de voie buna...\n");
                g_server_running = 0;
                
                pthread_mutex_lock(&g_queue.mutex);
                pthread_cond_broadcast(&g_queue.cond);
                pthread_mutex_unlock(&g_queue.mutex);
            } else {
                snprintf(raport, sizeof(raport), "Comanda admin necunoscuta.\n");
            }
            pthread_mutex_unlock(&g_metrics.mutex);
            send(active_admin_fd, raport, strlen(raport), 0);
        }
    }
    
    if (active_admin_fd != INVALID_DESCRIPTOR) close(active_admin_fd);
    close(unix_sock);
    unlink(UNIX_SOCKET_PATH);
    return NULL;
}

int main(void) {
    if (incarca_barem_din_fisier("barem.txt") < 0) {
        printf("[Server] EROARE CRITICA: Nu am gasit fisierul 'barem.txt'.\n");
        return EXIT_FAILURE;
    }

    init_queue();
    pthread_mutex_init(&g_metrics.mutex, NULL);
    printf("[Server] Pornit pe portul %d cu transport binar de fisiere BIDIRECTIONAL...\n", PORT_INET);

    pthread_t t_w, t_i, t_a, t_n; 
    pthread_create(&t_w, NULL, worker_thread_func, NULL);
    pthread_create(&t_i, NULL, inet_thread_func, NULL);
    pthread_create(&t_a, NULL, admin_thread_func, NULL);
    pthread_create(&t_n, NULL, inotify_thread_func, NULL); 

    while (g_server_running) { sleep(1); }
    
    printf("\n[Server] Incheiat. Toate firele de executie au fost eliberate.\n");
    return 0;
}
