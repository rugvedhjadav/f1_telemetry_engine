#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT 8080
#define MAX_CARS 20

typedef struct {
    int car_id;
    char driver_name[32];
    float speed;
    float throttle;
    int current_lap;
} TelemetryData;

typedef struct {
    TelemetryData grid[MAX_CARS];
    int race_active;
    int server_running;
    int user_role;
    pthread_mutex_t track_mutex;
} Track_State;

Track_State *shared_track;
sem_t *grid_semaphore;
pid_t analytics_pid;

int compare_speeds(const void *a, const void *b) {
    TelemetryData *carA = (TelemetryData *)a;
    TelemetryData *carB = (TelemetryData *)b;
    if (carB->speed > carA->speed) return 1;
    if (carB->speed < carA->speed) return -1;
    return 0;
}

void *auto_shutdown_thread(void *arg) {
    sleep(180);
    pthread_mutex_lock(&shared_track->track_mutex);
    if (shared_track->server_running) {
        shared_track->server_running = 0;
        printf("\n[RACE CONTROL] 3 MINUTES REACHED. FAILSAFE SHUTDOWN INITIATED.\n\n");
    }
    pthread_mutex_unlock(&shared_track->track_mutex);

    kill(analytics_pid, SIGTERM);
    sem_close(grid_semaphore);
    sem_unlink("/f1_grid");
    exit(0);
    return NULL;
}

void *logger_thread(void *arg) {
    int fd = open("race_archive.dat", O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (fd < 0) return NULL;
    struct flock lock;
    char buffer[256];

    while (shared_track->server_running) {
        sleep(2);
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        fcntl(fd, F_SETLKW, &lock);

        pthread_mutex_lock(&shared_track->track_mutex);
        if (shared_track->race_active && shared_track->server_running) {
            write(fd, "--- SYSTEM UPDATE ---\n", 22);
            for (int i = 0; i < MAX_CARS; i++) {
                if (shared_track->grid[i].speed > 0) {
                    int len = snprintf(buffer, sizeof(buffer), "LOG: Car %d (%s) Lap %d Speed: %.1f\n",
                                       shared_track->grid[i].car_id,
                                       shared_track->grid[i].driver_name,
                                       shared_track->grid[i].current_lap,
                                       shared_track->grid[i].speed);
                    write(fd, buffer, len);
                }
            }
        }
        pthread_mutex_unlock(&shared_track->track_mutex);

        lock.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &lock);
    }
    close(fd);
    return NULL;
}

void *handle_car_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    TelemetryData incoming_data;
    sem_wait(grid_semaphore);

    while (shared_track->server_running) {
        int total_received = 0;
        char *ptr = (char*)&incoming_data;
        while (total_received < sizeof(TelemetryData)) {
            int bytes_read = recv(sock, ptr + total_received, sizeof(TelemetryData) - total_received, 0);
            if (bytes_read <= 0) goto disconnect;
            total_received += bytes_read;
        }

        if (incoming_data.car_id < 1 || incoming_data.car_id > MAX_CARS) continue;

        pthread_mutex_lock(&shared_track->track_mutex);
        if (shared_track->race_active) {
            shared_track->grid[incoming_data.car_id - 1] = incoming_data;
        }
        pthread_mutex_unlock(&shared_track->track_mutex);
    }

disconnect:
    sem_post(grid_semaphore);
    close(sock);
    return NULL;
}

void *admin_console(void *arg) {
    char command[50];
    printf("\n[COMMAND LINK ACTIVE] Available commands:\n");
    printf(" - green    (Admin)\n");
    printf(" - red      (Admin)\n");
    printf(" - shut (Admin)\n");
    printf(" - status   (Admin, Engineer)\n");
    printf(" - fastest  (Admin, Engineer)\n");
    printf(" - watch    (All Roles)\n\n");

    while(shared_track->server_running) {
        if (scanf("%49s", command) == 1) {
            int role = shared_track->user_role;

            if (strcmp(command, "shut") == 0) {
                if (role == 1) {
                    pthread_mutex_lock(&shared_track->track_mutex);
                    shared_track->server_running = 0;
                    printf("\n[SYSTEM] Manual shutdown initiated...\n\n");
                    pthread_mutex_unlock(&shared_track->track_mutex);
                    kill(analytics_pid, SIGTERM);
                    sem_close(grid_semaphore);
                    sem_unlink("/f1_grid");
                    exit(0);
                } else {
                    printf("[DENIED] Only Admin can use 'shutdown'.\n");
                }
            }
            else if (strcmp(command, "green") == 0) {
                if (role == 1) {
                    pthread_mutex_lock(&shared_track->track_mutex);
                    shared_track->race_active = 1;
                    printf("\n[RACE CONTROL] Green flag! Telemetry ingestion active.\n");
                    pthread_mutex_unlock(&shared_track->track_mutex);
                } else {
                    printf("[DENIED] Only Admin can use 'green'.\n");
                }
            }
            else if (strcmp(command, "red") == 0) {
                if (role == 1) {
                    pthread_mutex_lock(&shared_track->track_mutex);
                    shared_track->race_active = 0;
                    printf("\n[RACE CONTROL] Red flag! Telemetry ingestion suspended.\n");
                    pthread_mutex_unlock(&shared_track->track_mutex);
                } else {
                    printf("[DENIED] Only Admin can use 'red'.\n");
                }
            }
            else if (strcmp(command, "status") == 0) {
                if (role <= 2) {
                    printf("\n[STATUS] Server: %s | Track: %s | Your Role ID: %d\n",
                           shared_track->server_running ? "ONLINE" : "OFFLINE",
                           shared_track->race_active ? "GREEN" : "RED", role);
                } else {
                    printf("[DENIED] Guests cannot use 'status'.\n");
                }
            }
            else if (strcmp(command, "fastest") == 0) {
                if (role <= 2) {
                    float max_s = 0.0;
                    char best_name[32] = "None";
                    int best_car = 0;
                    pthread_mutex_lock(&shared_track->track_mutex);
                    for(int i = 0; i < MAX_CARS; i++) {
                        if(shared_track->grid[i].speed > max_s) {
                            max_s = shared_track->grid[i].speed;
                            best_car = shared_track->grid[i].car_id;
                            strcpy(best_name, shared_track->grid[i].driver_name);
                        }
                    }
                    pthread_mutex_unlock(&shared_track->track_mutex);
                    printf("\n[FASTEST] Car %d (%s) is currently pushing %.1f km/h\n", best_car, best_name, max_s);
                } else {
                    printf("[DENIED] Guests cannot use 'fastest'.\n");
                }
            }
            else if (strcmp(command, "watch") == 0) {
                printf("\n--- [MANUAL SNAPSHOT] ---\n");
                pthread_mutex_lock(&shared_track->track_mutex);
                for(int i = 0; i < MAX_CARS; i++) {
                    if(shared_track->grid[i].speed > 0) {
                        printf("Car %d (%s) - Lap %d - %.1f km/h\n",
                               shared_track->grid[i].car_id, shared_track->grid[i].driver_name,
                               shared_track->grid[i].current_lap, shared_track->grid[i].speed);
                    }
                }
                pthread_mutex_unlock(&shared_track->track_mutex);
                printf("-------------------------\n");
            } else {
                printf("[ERROR] Unknown command.\n");
            }
        }
    }
    return NULL;
}

int main() {
    shared_track = mmap(NULL, sizeof(Track_State), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(shared_track, 0, sizeof(Track_State));
    shared_track->race_active = 1;
    shared_track->server_running = 1;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_track->track_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    sem_unlink("/f1_grid");
    grid_semaphore = sem_open("/f1_grid", O_CREAT, 0644, MAX_CARS);

    printf("Welcome to the Abu Dhabi 2021 Pit Wall\n");
    printf("1. Admin (Race Director)\n");
    printf("2. Engineer (Pit Wall)\n");
    printf("3. Guest (Spectator)\n");
    printf("Enter Role [1-3]: ");

    int choice;
    if (scanf("%d", &choice) != 1) choice = 3;
    if (choice < 1 || choice > 3) choice = 3;
    shared_track->user_role = choice;

    analytics_pid = fork();
    if (analytics_pid == 0) {
        int last_printed_lap = 0;

        while(shared_track->server_running) {
            TelemetryData local_grid[MAX_CARS];
            pthread_mutex_lock(&shared_track->track_mutex);
            memcpy(local_grid, shared_track->grid, sizeof(TelemetryData) * MAX_CARS);
            pthread_mutex_unlock(&shared_track->track_mutex);

            qsort(local_grid, MAX_CARS, sizeof(TelemetryData), compare_speeds);

            int current_leader_lap = 0;
            for (int i = 0; i < MAX_CARS; i++) {
                if (local_grid[i].current_lap > current_leader_lap) {
                    current_leader_lap = local_grid[i].current_lap;
                }
            }

            if (current_leader_lap > last_printed_lap && current_leader_lap <= 58) {
                printf("\n--- [LIVE LEADERBOARD] - LAP %d/58 ---\n", current_leader_lap);
                int position = 1;
                for (int i = 0; i < MAX_CARS; i++) {
                    if (local_grid[i].speed > 0) {
                        printf("P%d: Car %d (%s) - %.1f km/h\n",
                               position, local_grid[i].car_id, local_grid[i].driver_name, local_grid[i].speed);
                        position++;
                    }
                }
                printf("--------------------------------------\n");
                last_printed_lap = current_leader_lap;
            }

            if (current_leader_lap > 58) {
                pthread_mutex_lock(&shared_track->track_mutex);
                shared_track->server_running = 0;
                printf("\n[CHECKERED FLAG] VERSTAPPEN WINS THE WORLD CHAMPIONSHIP! AUTO-SHUTTING DOWN SERVER...\n\n");
                pthread_mutex_unlock(&shared_track->track_mutex);

                kill(getppid(), SIGTERM);
                exit(0);
            }
            usleep(10000);
        }
        exit(0);
    }

    int server_fd, client_sock;
    struct sockaddr_in server, client;
    socklen_t c = sizeof(struct sockaddr_in);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&server, sizeof(server));
    listen(server_fd, MAX_CARS + 5);

    pthread_t log_tid, admin_tid, timer_tid;
    pthread_create(&log_tid, NULL, logger_thread, NULL);
    pthread_create(&admin_tid, NULL, admin_console, NULL);
    pthread_create(&timer_tid, NULL, auto_shutdown_thread, NULL);

    while(shared_track->server_running && (client_sock = accept(server_fd, (struct sockaddr *)&client, &c)) ) {
        pthread_t sn_thread;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;
        pthread_create(&sn_thread, NULL, handle_car_client, (void*)new_sock);
        pthread_detach(sn_thread);
    }
    return 0;
}