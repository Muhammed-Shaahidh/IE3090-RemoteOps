#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9461
#define BUFFER_SIZE 1024

#define SID "SID:3280"
#define AUTH_TOKEN "OPS-0823"

typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;
} client_info_t;


/*
 * Send all bytes in the supplied buffer.
 *
 * send() is not guaranteed to transmit the complete buffer
 * in one call, so this function continues until all bytes
 * have been sent or an error occurs.
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
 * Receive one newline-terminated protocol line.
 *
 * The newline is removed before returning the command
 * to the caller.
 *
 * Return values:
 *   > 0 : number of characters received
 *     0 : peer disconnected
 *    -1 : socket error
 *    -2 : line exceeded buffer capacity
 */
ssize_t recv_line(int socket_fd, char *buffer, size_t buffer_size)
{
    size_t position = 0;

    if (buffer_size == 0)
    {
        return -2;
    }

    while (position < buffer_size - 1)
    {
        char character;

        ssize_t received =
            recv(socket_fd, &character, 1, 0);

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (received == 0)
        {
            if (position == 0)
            {
                return 0;
            }

            break;
        }

        if (character == '\n')
        {
            buffer[position] = '\0';

            /*
             * Remove an optional carriage return so that
             * both "\n" and "\r\n" input can be handled.
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

    /*
     * If the buffer became full before receiving '\n',
     * consume the remainder of the oversized line.
     */
    if (position == buffer_size - 1)
    {
        char character;

        while (1)
        {
            ssize_t received =
                recv(socket_fd, &character, 1, 0);

            if (received < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                return -1;
            }

            if (received == 0 || character == '\n')
            {
                break;
            }
        }

        return -2;
    }

    return (ssize_t)position;
}


int send_response(int client_fd, const char *response)
{
    if (send_all(client_fd,
                 response,
                 strlen(response)) < 0)
    {
        perror("send");
        return -1;
    }

    return 0;
}


/*
 * Handle one Controller session.
 */
void *handle_controller(void *arg)
{
    client_info_t *client_info =
        (client_info_t *)arg;

    int client_fd = client_info->client_fd;
    struct sockaddr_in client_addr =
        client_info->client_addr;

    free(client_info);

    printf("[THREAD %lu] Controller connected from %s:%d\n",
           (unsigned long)pthread_self(),
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    char line[BUFFER_SIZE];

    int authenticated = 0;

    /*
     * Keep the Controller session open so that additional
     * RemoteOps commands can be added in later stages.
     */
    while (1)
    {
        ssize_t result =
            recv_line(client_fd,
                      line,
                      sizeof(line));

        if (result == 0)
        {
            printf("[THREAD %lu] Controller disconnected.\n",
                   (unsigned long)pthread_self());
            break;
        }

        if (result == -1)
        {
            perror("recv");
            break;
        }

        if (result == -2)
        {
            const char *response =
                "ERR 003 LINE_TOO_LONG SID:3280\n";

            if (send_response(client_fd, response) < 0)
            {
                break;
            }

            continue;
        }

        printf("[THREAD %lu] Received command: %s\n",
               (unsigned long)pthread_self(),
               line);

        /*
         * AUTH must succeed before any other RemoteOps
         * command is accepted.
         */
        if (!authenticated)
        {
            if (strncmp(line, "AUTH ", 5) == 0)
            {
                const char *token = line + 5;

                if (strcmp(token, AUTH_TOKEN) == 0)
                {
                    const char *response =
                        "OK AUTHENTICATED SID:3280\n";

                    if (send_response(client_fd,
                                      response) < 0)
                    {
                        break;
                    }

                    authenticated = 1;

                    printf("[THREAD %lu] Authentication successful.\n",
                           (unsigned long)pthread_self());
                }
                else
                {
                    const char *response =
                        "ERR 001 AUTH_FAILED SID:3280\n";

                    if (send_response(client_fd,
                                      response) < 0)
                    {
                        break;
                    }

                    printf("[THREAD %lu] Authentication failed.\n",
                           (unsigned long)pthread_self());
                }
            }
            else
            {
                const char *response =
                    "ERR 001 AUTH_FAILED SID:3280\n";

                if (send_response(client_fd,
                                  response) < 0)
                {
                    break;
                }

                printf("[THREAD %lu] Command rejected before authentication.\n",
                       (unsigned long)pthread_self());
            }

            continue;
        }

        /*
         * Other authenticated RemoteOps commands will be
         * implemented in the following development stages.
         */
        const char *response =
            "ERR 003 UNKNOWN_COMMAND SID:3280\n";

        if (send_response(client_fd, response) < 0)
        {
            break;
        }
    }

    close(client_fd);

    printf("[THREAD %lu] Controller session closed.\n",
           (unsigned long)pthread_self());

    return NULL;
}


int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;

    server_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

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
    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);
    server_addr.sin_port =
        htons(AGENT_PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("        RemoteOps Agent\n");
    printf("========================================\n");
    printf("Registration Number : IT24610823\n");
    printf("Listening Port      : %d\n", AGENT_PORT);
    printf("Session ID          : %s\n", SID);
    printf("Concurrency Model   : POSIX threads\n");
    printf("----------------------------------------\n");
    printf("Waiting for Controller connections...\n\n");

    while (1)
    {
        client_info_t *client_info =
            malloc(sizeof(client_info_t));

        if (client_info == NULL)
        {
            perror("malloc");
            continue;
        }

        socklen_t client_addr_len =
            sizeof(client_info->client_addr);

        client_info->client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_info->client_addr,
                   &client_addr_len);

        if (client_info->client_fd < 0)
        {
            perror("accept");
            free(client_info);
            continue;
        }

        pthread_t thread_id;

        int result =
            pthread_create(&thread_id,
                           NULL,
                           handle_controller,
                           client_info);

        if (result != 0)
        {
            fprintf(stderr,
                    "pthread_create failed: %s\n",
                    strerror(result));

            close(client_info->client_fd);
            free(client_info);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
