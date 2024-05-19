#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <jansson.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define ARRAY_SIZE 1000000
#define CHUNK_SIZE 1024
#define LENGTH_OFFSET 4

typedef struct {
    int port;
    int supported_protocol_version;
} Config;

Config read_config(const char *filename) {
    Config config;
    json_t *root;
    json_error_t error;

    root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "Error reading JSON config: %s\n", error.text);
        exit(1);
    }

    json_t *port = json_object_get(root, "port");
    if (!json_is_integer(port)) {
        fprintf(stderr, "Error: port is not an integer\n");
        exit(1);
    }

    json_t *version = json_object_get(root, "supported_protocol_version");
    if (!json_is_integer(version)) {
        fprintf(stderr, "Error: supported_protocol_version is not an integer\n");
        exit(1);
    }

    config.port = json_integer_value(port);
    config.supported_protocol_version = json_integer_value(version);
    json_decref(root);
    return config;
}

void send_error(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, const char *error_message) {
    ssize_t sent = sendto(sockfd, error_message, strlen(error_message), 0, (struct sockaddr *)client_addr, addr_len);
    if (sent < 0) {
        perror("sendto failed");
    }
}

void handle_client_request(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len, int supported_version) {
    char buffer[BUFFER_SIZE];
    ssize_t received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)client_addr, &addr_len);
    if (received < 0) {
        perror("recvfrom failed");
        return;
    }

    buffer[received] = '\0';
    
    int client_version = buffer[0];
    if (client_version < supported_version) {
        send_error(sockfd, client_addr, addr_len, "Error: Protocol version outdated. Please update your client.");
        return;
    }

    double X = atof(buffer + 1);
    double data[ARRAY_SIZE];
    srand(time(NULL));
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        data[i] = ((double)rand() / RAND_MAX) * (2 * X) - X;
    }

    int total_chunks = ARRAY_SIZE / CHUNK_SIZE;
    if (ARRAY_SIZE % CHUNK_SIZE != 0) {
        total_chunks++;
    }

    // Prepare TLV(Type-Length-Value) formatted message
    char *response = (char *)malloc(1 + LENGTH_OFFSET + sizeof(data));
    response[0] = (char)supported_version;  // Protocol version
    *(int *)(response + 1) = ARRAY_SIZE * sizeof(double);  // Length of the data
    memcpy(response + 1 + LENGTH_OFFSET, data, sizeof(data));  // Data


    for (int i = 0; i < total_chunks; i++) {
        int offset = i * CHUNK_SIZE;
        int chunk_size = (ARRAY_SIZE - offset) < CHUNK_SIZE ? (ARRAY_SIZE - offset) : CHUNK_SIZE;
        ssize_t sent = sendto(sockfd, &data[offset], 1 + LENGTH_OFFSET + chunk_size * sizeof(double),
                              0, (struct sockaddr *)client_addr, addr_len);
        if (sent < 0) {
            perror("sendto failed");
        }
    }

    free(response);
}

int main() {
    Config config = read_config("server_config.json");

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config.port);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server is running on port %d\n", config.port);

    while (1) {
        handle_client_request(sockfd, &client_addr, addr_len, config.supported_protocol_version);
    }

    close(sockfd);
    return 0;
}
