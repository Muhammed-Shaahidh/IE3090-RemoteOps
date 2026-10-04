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
    int sock_fd;

    struct sockaddr_in agent_addr;

    char buffer[BUFFER_SIZE];

    /* Create the Controller TCP socket. */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&agent_addr, 0, sizeof(agent_addr));

    agent_addr.sin_family = AF_INET;
    agent_addr.sin_port = htons(AGENT_PORT);

    /*
     * For local development both programs run on the same
     * machine, therefore the Agent address is 127.0.0.1.
     */
    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &agent_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid Agent address.\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("     RemoteOps Controller - Basic TCP\n");
    printf("========================================\n");
    printf("Connecting to Agent at 127.0.0.1:%d...\n",
           AGENT_PORT);

    /* Establish the TCP connection with the Agent. */
    if (connect(sock_fd,
                (struct sockaddr *)&agent_addr,
                sizeof(agent_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Connected to RemoteOps Agent.\n");

    /*
     * HELLO is used only to verify the initial TCP connection.
     * It will be replaced by the assignment's AUTH protocol.
     */
    const char *message = "HELLO\n";

    if (send(sock_fd,
             message,
             strlen(message),
             0) < 0)
    {
        perror("send");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Sent to Agent: HELLO\n");

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received =
        recv(sock_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received < 0)
    {
        perror("recv");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (bytes_received == 0)
    {
        printf("Agent closed the connection.\n");
    }
    else
    {
        buffer[bytes_received] = '\0';

        printf("Agent response: %s", buffer);
    }

    close(sock_fd);

    printf("Basic TCP connection test completed.\n");

    return EXIT_SUCCESS;
}
