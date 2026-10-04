#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9461
#define BUFFER_SIZE 1024

#define AUTH_TOKEN "OPS-0823"
#define SID "SID:3280"


/*
 * Sends the complete buffer through the TCP socket.
 *
 * A single send() call is not guaranteed to transmit
 * all bytes, so this function continues until the
 * complete message has been sent.
 */
ssize_t send_all(int socket_fd, const void *buffer, size_t length)
{
    size_t total_sent = 0;
    const char *data = (const char *)buffer;

    while (total_sent < length)
    {
        ssize_t sent =
            send(socket_fd,
                 data + total_sent,
                 length - total_sent,
                 0);

        if (sent < 0)
        {
            /*
             * If send() was interrupted by a signal,
             * try again.
             */
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (sent == 0)
        {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
}


/*
 * Receives one newline-terminated protocol message.
 *
 * The '\n' character is removed before the message
 * is returned to the caller.
 */
ssize_t recv_line(int socket_fd, char *buffer, size_t buffer_size)
{
    size_t position = 0;

    if (buffer_size == 0)
    {
        return -1;
    }

    while (position < buffer_size - 1)
    {
        char character;

        ssize_t received =
            recv(socket_fd,
                 &character,
                 1,
                 0);

        if (received < 0)
        {
            /*
             * Retry recv() if interrupted by a signal.
             */
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        /*
         * recv() returning 0 means that the Agent
         * closed the TCP connection.
         */
        if (received == 0)
        {
            if (position == 0)
            {
                return 0;
            }

            break;
        }

        /*
         * A newline marks the end of one
         * RemoteOps protocol message.
         */
        if (character == '\n')
        {
            buffer[position] = '\0';

            /*
             * Also support CRLF (\r\n).
             */
            if (position > 0 &&
                buffer[position - 1] == '\r')
            {
                buffer[position - 1] = '\0';
                position--;
            }

            return (ssize_t)position;
        }

        buffer[position++] = character;
    }

    buffer[position] = '\0';

    return (ssize_t)position;
}


int main(void)
{
    int sock_fd;

    struct sockaddr_in agent_addr;

    char buffer[BUFFER_SIZE];

    /*
     * Create an IPv4 TCP socket.
     */
    sock_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    /*
     * Prepare the Agent address structure.
     */
    memset(&agent_addr, 0, sizeof(agent_addr));

    agent_addr.sin_family = AF_INET;
    agent_addr.sin_port = htons(AGENT_PORT);

    /*
     * During local development, both the Agent and
     * Controller run on the same machine.
     *
     * Therefore, the loopback address 127.0.0.1
     * is used.
     */
    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &agent_addr.sin_addr) <= 0)
    {
        fprintf(stderr,
                "Invalid Agent address.\n");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("        RemoteOps Controller\n");
    printf("========================================\n");
    printf("Registration Number : IT24610823\n");
    printf("Agent Address       : 127.0.0.1\n");
    printf("Agent Port          : %d\n", AGENT_PORT);
    printf("Session ID          : %s\n", SID);
    printf("----------------------------------------\n");

    printf("Connecting to Agent at 127.0.0.1:%d...\n",
           AGENT_PORT);

    /*
     * Establish the TCP connection with the Agent.
     */
    if (connect(sock_fd,
                (struct sockaddr *)&agent_addr,
                sizeof(agent_addr)) < 0)
    {
        perror("connect");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Connected to RemoteOps Agent.\n");
    printf("----------------------------------------\n");

    /*
     * AUTH must be the first RemoteOps protocol
     * command sent after establishing a connection.
     */
    const char *auth_command =
        "AUTH OPS-0823\n";

    /*
     * Send the authentication command.
     */
    if (send_all(sock_fd,
                 auth_command,
                 strlen(auth_command)) < 0)
    {
        perror("send");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Sent: AUTH %s\n", AUTH_TOKEN);

    /*
     * Wait for the Agent authentication response.
     */
    ssize_t result =
        recv_line(sock_fd,
                  buffer,
                  sizeof(buffer));

    if (result < 0)
    {
        perror("recv");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (result == 0)
    {
        printf("Agent disconnected unexpectedly.\n");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Agent: %s\n", buffer);

    /*
     * Verify the expected successful
     * authentication response.
     */
    if (strcmp(buffer,
               "OK AUTHENTICATED SID:3280") == 0)
    {
        printf("----------------------------------------\n");
        printf("Authentication successful.\n");
        printf("RemoteOps session authenticated.\n");
    }
    else
    {
        printf("----------------------------------------\n");
        printf("Authentication failed.\n");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    /*
     * Additional interactive RemoteOps commands
     * such as SYSINFO, LISTPROC, EXEC, PUT, GET,
     * MONITOR and QUIT will be added in the
     * following development stages.
     */

    close(sock_fd);

    printf("----------------------------------------\n");
    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
