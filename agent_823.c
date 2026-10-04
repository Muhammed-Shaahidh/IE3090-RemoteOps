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
 * send() is not guaranteed to transmit the complete
 * buffer in one call, so continue until all bytes
 * have been transmitted or an error occurs.
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
 * Receive one newline-terminated RemoteOps
 * protocol line.
 *
 * The newline is removed before returning
 * the command to the caller.
 *
 * Return values:
 *
 *   > 0 : number of characters received
 *     0 : peer disconnected
 *    -1 : socket error
 *    -2 : line exceeded buffer capacity
 */
ssize_t recv_line(int socket_fd,
                  char *buffer,
                  size_t buffer_size)
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
             * Support both LF and CRLF line endings.
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
     * If the line is too long, consume the
     * remaining characters until the newline.
     */
    if (position == buffer_size - 1)
    {
        char character;

        while (1)
        {
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

            if (received == 0 ||
                character == '\n')
            {
                break;
            }
        }

        return -2;
    }

    return (ssize_t)position;
}


/*
 * Send a complete RemoteOps protocol response.
 */
int send_response(int client_fd,
                  const char *response)
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
 * Read the one-minute system load average
 * from /proc/loadavg.
 */
int get_cpu_load(double *cpu_load)
{
    FILE *file =
        fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        perror("fopen /proc/loadavg");
        return -1;
    }

    if (fscanf(file, "%lf", cpu_load) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    return 0;
}


/*
 * Determine currently used memory in MB.
 *
 * Linux provides memory statistics through
 * /proc/meminfo.
 *
 * Used memory:
 *
 *     MemTotal - MemAvailable
 */
int get_memory_used_mb(long *memory_used_mb)
{
    FILE *file =
        fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        perror("fopen /proc/meminfo");
        return -1;
    }

    char line[256];

    long mem_total_kb = -1;
    long mem_available_kb = -1;

    while (fgets(line,
                 sizeof(line),
                 file) != NULL)
    {
        if (sscanf(line,
                   "MemTotal: %ld kB",
                   &mem_total_kb) == 1)
        {
            continue;
        }

        if (sscanf(line,
                   "MemAvailable: %ld kB",
                   &mem_available_kb) == 1)
        {
            continue;
        }

        if (mem_total_kb >= 0 &&
            mem_available_kb >= 0)
        {
            break;
        }
    }

    fclose(file);

    if (mem_total_kb < 0 ||
        mem_available_kb < 0)
    {
        return -1;
    }

    /*
     * Values in /proc/meminfo are expressed in kB.
     * Convert the calculated used value to MB.
     */
    *memory_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

    return 0;
}


/*
 * Read system uptime from /proc/uptime.
 */
int get_uptime_seconds(long *uptime_seconds)
{
    FILE *file =
        fopen("/proc/uptime", "r");

    if (file == NULL)
    {
        perror("fopen /proc/uptime");
        return -1;
    }

    double uptime;

    if (fscanf(file, "%lf", &uptime) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    /*
     * The protocol requires uptime in seconds.
     */
    *uptime_seconds = (long)uptime;

    return 0;
}


/*
 * Handle an authenticated SYSINFO command.
 *
 * Response format:
 *
 * OK SYSINFO <cpu_load> <mem_used_mb>
 *            <uptime_sec> SID:3280
 */
int handle_sysinfo(int client_fd)
{
    double cpu_load;
    long memory_used_mb;
    long uptime_seconds;

    /*
     * Retrieve all required Linux system
     * information.
     */
    if (get_cpu_load(&cpu_load) < 0 ||
        get_memory_used_mb(&memory_used_mb) < 0 ||
        get_uptime_seconds(&uptime_seconds) < 0)
    {
        return send_response(
            client_fd,
            "ERR 004 INTERNAL_ERROR SID:3280\n");
    }

    char response[BUFFER_SIZE];

    int written =
        snprintf(response,
                 sizeof(response),
                 "OK SYSINFO %.2f %ld %ld SID:3280\n",
                 cpu_load,
                 memory_used_mb,
                 uptime_seconds);

    if (written < 0 ||
        (size_t)written >= sizeof(response))
    {
        return send_response(
            client_fd,
            "ERR 004 INTERNAL_ERROR SID:3280\n");
    }

    return send_response(client_fd,
                         response);
}


/*
 * Handle one Controller session.
 *
 * Each connected Controller is processed by
 * its own POSIX worker thread.
 */
void *handle_controller(void *arg)
{
    client_info_t *client_info =
        (client_info_t *)arg;

    int client_fd =
        client_info->client_fd;

    struct sockaddr_in client_addr =
        client_info->client_addr;

    /*
     * The worker thread now owns the copied
     * Controller information.
     */
    free(client_info);

    printf(
        "[THREAD %lu] Controller connected from %s:%d\n",
        (unsigned long)pthread_self(),
        inet_ntoa(client_addr.sin_addr),
        ntohs(client_addr.sin_port));

    char line[BUFFER_SIZE];

    /*
     * Authentication state is maintained
     * independently for this Controller session.
     */
    int authenticated = 0;

    /*
     * Keep the connection open so multiple
     * RemoteOps commands can be processed during
     * the same authenticated session.
     */
    while (1)
    {
        ssize_t result =
            recv_line(client_fd,
                      line,
                      sizeof(line));

        /*
         * Controller closed the connection.
         */
        if (result == 0)
        {
            printf(
                "[THREAD %lu] Controller disconnected.\n",
                (unsigned long)pthread_self());

            break;
        }

        /*
         * Socket receive error.
         */
        if (result == -1)
        {
            perror("recv");
            break;
        }

        /*
         * Protocol command exceeded the
         * supported line length.
         */
        if (result == -2)
        {
            if (send_response(
                    client_fd,
                    "ERR 003 LINE_TOO_LONG SID:3280\n") < 0)
            {
                break;
            }

            continue;
        }

        printf(
            "[THREAD %lu] Received command: %s\n",
            (unsigned long)pthread_self(),
            line);

        /*
         * ------------------------------------------------
         * AUTHENTICATION
         * ------------------------------------------------
         *
         * AUTH must succeed before any other
         * RemoteOps command is accepted.
         */
        if (!authenticated)
        {
            if (strncmp(line,
                        "AUTH ",
                        5) == 0)
            {
                const char *token =
                    line + 5;

                if (strcmp(token,
                           AUTH_TOKEN) == 0)
                {
                    if (send_response(
                            client_fd,
                            "OK AUTHENTICATED SID:3280\n") < 0)
                    {
                        break;
                    }

                    authenticated = 1;

                    printf(
                        "[THREAD %lu] Authentication successful.\n",
                        (unsigned long)pthread_self());
                }
                else
                {
                    if (send_response(
                            client_fd,
                            "ERR 001 AUTH_FAILED SID:3280\n") < 0)
                    {
                        break;
                    }

                    printf(
                        "[THREAD %lu] Authentication failed.\n",
                        (unsigned long)pthread_self());
                }
            }
            else
            {
                /*
                 * No RemoteOps command is allowed
                 * before successful AUTH.
                 */
                if (send_response(
                        client_fd,
                        "ERR 001 AUTH_FAILED SID:3280\n") < 0)
                {
                    break;
                }

                printf(
                    "[THREAD %lu] Command rejected before authentication.\n",
                    (unsigned long)pthread_self());
            }

            continue;
        }


        /*
         * ------------------------------------------------
         * SYSINFO
         * ------------------------------------------------
         *
         * Return CPU load, used memory and uptime.
         */
        if (strcmp(line,
                   "SYSINFO") == 0)
        {
            if (handle_sysinfo(client_fd) < 0)
            {
                break;
            }

            printf(
                "[THREAD %lu] SYSINFO response sent.\n",
                (unsigned long)pthread_self());

            continue;
        }


        /*
         * ------------------------------------------------
         * QUIT
         * ------------------------------------------------
         *
         * Gracefully terminate this Controller
         * session.
         */
        if (strcmp(line,
                   "QUIT") == 0)
        {
            if (send_response(
                    client_fd,
                    "OK BYE SID:3280\n") < 0)
            {
                break;
            }

            printf(
                "[THREAD %lu] Controller requested QUIT.\n",
                (unsigned long)pthread_self());

            break;
        }


        /*
         * Any authenticated command that is not
         * currently implemented is rejected.
         */
        if (send_response(
                client_fd,
                "ERR 003 UNKNOWN_COMMAND SID:3280\n") < 0)
        {
            break;
        }
    }

    close(client_fd);

    printf(
        "[THREAD %lu] Controller session closed.\n",
        (unsigned long)pthread_self());

    return NULL;
}


int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;

    /*
     * Create the Agent IPv4 TCP listening socket.
     */
    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    /*
     * Allow the listening address to be reused
     * during repeated development/testing.
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

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);

    server_addr.sin_port =
        htons(AGENT_PORT);

    /*
     * Bind the Agent to personalised TCP
     * port 9461.
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
     * Begin listening for Controller connections.
     *
     * A backlog of 10 is sufficient for the
     * assignment requirement of at least five
     * simultaneous Controllers.
     */
    if (listen(server_fd,
               10) < 0)
    {
        perror("listen");

        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("        RemoteOps Agent\n");
    printf("========================================\n");
    printf("Registration Number : IT24610823\n");
    printf("Listening Port      : %d\n",
           AGENT_PORT);
    printf("Session ID          : %s\n",
           SID);
    printf("Concurrency Model   : POSIX threads\n");
    printf("----------------------------------------\n");
    printf("Waiting for Controller connections...\n\n");

    /*
     * Main Agent acceptance loop.
     *
     * The main thread remains responsible for
     * accepting connections while worker threads
     * process individual Controller sessions.
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
            accept(
                server_fd,
                (struct sockaddr *)&client_info->client_addr,
                &client_addr_len);

        if (client_info->client_fd < 0)
        {
            perror("accept");

            free(client_info);
            continue;
        }

        pthread_t thread_id;

        /*
         * Create an independent worker thread for
         * the newly connected Controller.
         */
        int result =
            pthread_create(
                &thread_id,
                NULL,
                handle_controller,
                client_info);

        if (result != 0)
        {
            fprintf(
                stderr,
                "pthread_create failed: %s\n",
                strerror(result));

            close(client_info->client_fd);
            free(client_info);

            continue;
        }

        /*
         * No pthread_join() is required for this
         * worker thread.
         */
        pthread_detach(thread_id);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
