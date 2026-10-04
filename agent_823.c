#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9461
#define BUFFER_SIZE 1024
#define SID "SID:3280"

/*
 * Information passed from the main Agent thread
 * to an individual Controller worker thread.
 */
typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;
} client_info_t;


/*
 * Worker thread used to handle one connected Controller.
 *
 * The complete RemoteOps command-processing loop will be
 * added in later development stages.
 */
void *handle_controller(void *arg)
{
    client_info_t *client_info = (client_info_t *)arg;

    int client_fd = client_info->client_fd;
    struct sockaddr_in client_addr = client_info->client_addr;

    /*
     * The structure was dynamically allocated by the main
     * thread. The worker now owns the copied information,
     * so the original allocation can be released.
     */
    free(client_info);

    printf("[THREAD %lu] Controller connected from %s:%d\n",
           (unsigned long)pthread_self(),
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    char buffer[BUFFER_SIZE];

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received =
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received < 0)
    {
        perror("recv");
    }
    else if (bytes_received == 0)
    {
        printf("[THREAD %lu] Controller disconnected without sending data.\n",
               (unsigned long)pthread_self());
    }
    else
    {
        buffer[bytes_received] = '\0';

        printf("[THREAD %lu] Received: %s",
               (unsigned long)pthread_self(),
               buffer);

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
            printf("[THREAD %lu] Response sent successfully.\n",
                   (unsigned long)pthread_self());
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

    /*
     * Create the Agent TCP listening socket.
     */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

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
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(AGENT_PORT);

    /*
     * Bind the Agent to the personalised TCP port.
     */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /*
     * Use a backlog larger than the assignment's minimum
     * five simultaneous Controller requirement.
     */
    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("      RemoteOps Agent - Concurrent\n");
    printf("========================================\n");
    printf("Registration Number : IT24610823\n");
    printf("Listening Port      : %d\n", AGENT_PORT);
    printf("Session ID          : %s\n", SID);
    printf("Concurrency Model   : POSIX threads\n");
    printf("----------------------------------------\n");
    printf("Waiting for Controller connections...\n\n");

    /*
     * Continue accepting Controllers instead of terminating
     * after the first connection.
     */
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

        /*
         * The main Agent does not need to pthread_join()
         * completed Controller threads.
         */
        pthread_detach(thread_id);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
