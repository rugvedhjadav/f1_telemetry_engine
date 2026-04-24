#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define SERVER_IP "127.0.0.1"

typedef struct {
    int car_id;
    char driver_name[32];
    float speed;
    float throttle;
    int current_lap;
} TelemetryData;

int main(int argc, char const *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <car_id> <name> <csv_path>\n", argv[0]);
        return 1;
    }

    int car_id = atoi(argv[1]);
    int sock = 0;
    struct sockaddr_in serv_addr;
    TelemetryData car_data;

    car_data.car_id = car_id;
    strncpy(car_data.driver_name, argv[2], 31);
    car_data.driver_name[31] = '\0';
    car_data.current_lap = 1;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) return -1;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) return -1;

    FILE *file = fopen(argv[3], "r");
    if (!file) return 1;

    char line[256];
    fgets(line, sizeof(line), file); // Discard header

    // Abu Dhabi 2021 Track Info
    const float TRACK_LENGTH_KM = 5.281;
    float distance_covered = 0.0;

    while (fgets(line, sizeof(line), file)) {
        float speed = 0.0, throttle = 0.0;
        if (sscanf(line, "%f,%f", &speed, &throttle) == 2) {
            car_data.speed = speed;
            car_data.throttle = throttle;

            if (send(sock, &car_data, sizeof(car_data), 0) <= 0) break;

            // Physics Engine adjusted for Abu Dhabi data length
            distance_covered += (car_data.speed * (0.155 / 3600.0));
            if (distance_covered >= TRACK_LENGTH_KM) {
                car_data.current_lap++;
                distance_covered -= TRACK_LENGTH_KM;
            }
        }
        usleep(8000);  // increse this for making the  time for 5 - 6 mins make it 8000
    }

    fclose(file);
    close(sock);
    return 0;
}