#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <jansson.h>

#define BUFFER_SIZE 1024
#define CHUNK_SIZE 1024  // Number of doubles to receive in one chunk
#define ARRAY_SIZE 1000000

typedef struct {
    char server_address[256];
    int server_port;
    double value_to_send;
} Config;

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    double value_to_send;
} ThreadData;

Config read_config(const char *filename) {
    Config config;
    json_t *root;
    json_error_t error;

    root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "Error reading JSON config: %s\n", error.text);
        exit(1);
    }

    json_t *address = json_object_get(root, "server_address");
    if (!json_is_string(address)) {
        fprintf(stderr, "Error: server_address is not a string\n");
        exit(1);
    }

    json_t *port = json_object_get(root, "server_port");
    if (!json_is_integer(port)) {
        fprintf(stderr, "Error: server_port is not an integer\n");
        exit(1);
    }

    json_t *value = json_object_get(root, "value_to_send");
    if (!json_is_real(value)) {
        fprintf(stderr, "Error: value_to_send is not a double\n");
        exit(1);
    }

    strcpy(config.server_address, json_string_value(address));
    config.server_port = json_integer_value(port);
    config.value_to_send = json_real_value(value);
    json_decref(root);
    return config;
}

int compare(const void *a, const void *b) {
    double diff = *(double *)b - *(double *)a;
    return (diff > 0) - (diff < 0);
}

void log_error(const char *error_message, int server_version) {
    FILE *log_file = fopen("client.log", "a");
    if (log_file) {
        fprintf(log_file, "Error: %s, Server Protocol Version: %d\n", error_message, server_version);
        fclose(log_file);
    }
}

void* send_value(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    double value_to_send = data->value_to_send;
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%d%f", 1, value_to_send); // 1 is protocol version
    ssize_t sent = sendto(data->sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&data->server_addr, sizeof(data->server_addr));
    if (sent < 0) {
        perror("sendto failed");
    }
    return NULL;
}

void* receive_data(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    double recv_data[ARRAY_SIZE];
    memset(recv_data, 0, sizeof(recv_data));
    int total_received = 0;

    while (total_received < ARRAY_SIZE) {
        double buffer[CHUNK_SIZE];
        ssize_t received = recvfrom(data->sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (received < 0) {
            perror("recvfrom failed");
            close(data->sockfd);
            pthread_exit(NULL);
        }

        if (received == 0) {
            log_error("Received empty response from server", 1);
            break;
        } else if (strncmp((char *)buffer, "Error: Protocol version outdated", 32) == 0) {
            int server_version = ((char *)buffer)[33] - '0'; // Assuming version is single digit
            log_error("Protocol version outdated", server_version);
            break;
        } else {
            int num_doubles = received / sizeof(double);
            memcpy(&recv_data[total_received], buffer, received);
            total_received += num_doubles;
        }
    }

    // Sort data if we have received enough
    if (total_received == ARRAY_SIZE) {
        qsort(recv_data, ARRAY_SIZE, sizeof(double), compare);

        // Write to binary file
        FILE *file = fopen("output.bin", "wb");
        if (file) {
            fwrite(recv_data, sizeof(double), ARRAY_SIZE, file);
            fclose(file);
        } else {
            perror("Failed to open file");
        }
    }

    close(data->sockfd);
    return NULL;
}

int main() {
    Config config = read_config("client_config.json");

    // Number of clients to emulate
    int num_clients = 5;
    pthread_t send_threads[num_clients];
    pthread_t receive_threads[num_clients];
    ThreadData thread_data[num_clients];

    for (int i = 0; i < num_clients; i++) {
        int sockfd;
        struct sockaddr_in server_addr;

        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(config.server_port);
        if (inet_pton(AF_INET, config.server_address, &server_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            exit(EXIT_FAILURE);
        }

        // Pause for 3 seconds
        sleep(3);

        thread_data[i] = (ThreadData){sockfd, server_addr, config.value_to_send};

        // Create send thread
        if (pthread_create(&send_threads[i], NULL, send_value, &thread_data[i]) != 0) {
            perror("Failed to create send thread");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // Create receive thread
        if (pthread_create(&receive_threads[i], NULL, receive_data, &thread_data[i]) != 0) {
            perror("Failed to create receive thread");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < num_clients; i++) {
        pthread_join(send_threads[i], NULL);
        pthread_join(receive_threads[i], NULL);
    }

    return 0;
}
