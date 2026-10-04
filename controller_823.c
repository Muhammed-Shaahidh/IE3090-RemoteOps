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
 * Send all bytes in the supplied buffer.
 *
 * A single send() call is not guaranteed to transmit
 * the entire message, so continue until all bytes
 * have been sent or an error occurs.
 */
ssize_t send_all(int socket_fd,
                 const void *buffer,
                 size_t length)
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
 * Receive one newline-terminated protocol message.
 *
 * The newline character is removed before the
 * message is returned to the caller.
 */
ssize_t recv_line(int socket_fd,
                  char *buffer,
                  size_t buffer_size)
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
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        /*
         * recv() returning zero means that the
         * Agent has closed the TCP connection.
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
         * Newline marks the end of one RemoteOps
         * protocol message.
         */
        if (character == '\n')
        {
            buffer[position] = '\0';

            /*
             * Also support CRLF line endings.
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
     * ------------------------------------------------
     * CREATE CONTROLLER TCP SOCKET
     * ------------------------------------------------
     */
    sock_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sock_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }


    /*
     * ------------------------------------------------
     * CONFIGURE AGENT ADDRESS
     * ------------------------------------------------
     */
    memset(&agent_addr,
           0,
           sizeof(agent_addr));

    agent_addr.sin_family =
        AF_INET;

    agent_addr.sin_port =
        htons(AGENT_PORT);

    /*
     * During local development, both programs
     * execute on the same Linux machine.
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
    printf("Agent Port          : %d\n",
           AGENT_PORT);
    printf("Session ID          : %s\n",
           SID);
    printf("----------------------------------------\n");

    printf("Connecting to Agent at 127.0.0.1:%d...\n",
           AGENT_PORT);


    /*
     * ------------------------------------------------
     * CONNECT TO AGENT
     * ------------------------------------------------
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
     * ------------------------------------------------
     * AUTHENTICATION
     * ------------------------------------------------
     *
     * AUTH must be the first protocol command sent
     * after the TCP connection is established.
     */
    const char *auth_command =
        "AUTH OPS-0823\n";

    if (send_all(sock_fd,
                 auth_command,
                 strlen(auth_command)) < 0)
    {
        perror("send");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Sent: AUTH %s\n",
           AUTH_TOKEN);


    /*
     * Receive the authentication response.
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

    printf("Agent: %s\n",
           buffer);


    /*
     * Authentication must succeed before entering
     * the interactive RemoteOps command loop.
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
     * ------------------------------------------------
     * INTERACTIVE REMOTEOPS SESSION
     * ------------------------------------------------
     */
    printf("----------------------------------------\n");
    printf("Available Commands:\n");
    printf("  SYSINFO\n");
    printf("  QUIT\n");
    printf("----------------------------------------\n");


    /*
     * Keep the same TCP connection open so that
     * multiple commands can be issued after one
     * successful authentication.
     */
    while (1)
    {
        char command[BUFFER_SIZE];
        char protocol_message[BUFFER_SIZE];

        printf("RemoteOps> ");
        fflush(stdout);


        /*
         * Read one command from the user.
         */
        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL)
        {
            printf("\nInput closed.\n");
            break;
        }


        /*
         * Remove the newline inserted by fgets().
         */
        command[strcspn(command, "\n")] = '\0';


        /*
         * Ignore an empty command.
         */
        if (strlen(command) == 0)
        {
            continue;
        }


        /*
         * Convert the user's command into a
         * newline-terminated protocol message.
         */
        int written =
            snprintf(protocol_message,
                     sizeof(protocol_message),
                     "%s\n",
                     command);

        if (written < 0 ||
            (size_t)written >= sizeof(protocol_message))
        {
            fprintf(stderr,
                    "Command is too long.\n");

            continue;
        }


        /*
         * Send the command to the Agent.
         */
        if (send_all(sock_fd,
                     protocol_message,
                     strlen(protocol_message)) < 0)
        {
            perror("send");
            break;
        }


        /*
         * Wait for the Agent response.
         */
        ssize_t response_length =
            recv_line(sock_fd,
                      buffer,
                      sizeof(buffer));

        if (response_length < 0)
        {
            perror("recv");
            break;
        }

        if (response_length == 0)
        {
            printf("Agent disconnected.\n");
            break;
        }


        /*
         * Display the response returned by the Agent.
         */
        printf("Agent: %s\n",
               buffer);


        /*
         * QUIT causes the Agent to return
         * OK BYE SID:3280 and close this session.
         */
        if (strcmp(command,
                   "QUIT") == 0)
        {
            break;
        }
    }


    /*
     * ------------------------------------------------
     * CLEANUP
     * ------------------------------------------------
     */
    close(sock_fd);

    printf("----------------------------------------\n");
    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
