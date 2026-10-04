#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9461
#define BUFFER_SIZE 1024

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_addr_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    /* Create the TCP socket. */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    /*
     * Allow the listening address to be reused after restarting
     * the Agent during development.
     */
    int reuse = 1;

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse,
                   sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(AGENT_PORT);

    /* Bind the socket to the personalised Agent port. */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /* Start listening for incoming Controller connections. */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("       RemoteOps Agent - Basic TCP\n");
    printf("========================================\n");
    printf("Registration Number : IT24610823\n");
    printf("Listening Port      : %d\n", AGENT_PORT);
    printf("Session ID          : SID:3280\n");
    printf("----------------------------------------\n");
    printf("Waiting for a Controller connection...\n");

    /*
     * Accept one Controller for this initial development stage.
     * Multi-client concurrency will be added in the next stage.
     */
    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_addr_len);

    if (client_fd < 0)
    {
        perror("accept");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("\nController connected from %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received =
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received < 0)
    {
        perror("recv");
    }
    else if (bytes_received == 0)
    {
        printf("Controller disconnected before sending data.\n");
    }
    else
    {
        buffer[bytes_received] = '\0';

        printf("Received from Controller: %s\n", buffer);

        const char *response =
            "OK BASIC_CONNECTION SID:3280\n";

        if (send(client_fd,
                 response,
                 strlen(response),
                 0) < 0)
        {
            perror("send");
        }
        else
        {
            printf("Basic TCP response sent successfully.\n");
        }
    }

    close(client_fd);
    close(server_fd);

    printf("Basic TCP test completed.\n");

    return EXIT_SUCCESS;
}
