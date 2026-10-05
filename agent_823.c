#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>

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
 * ------------------------------------------------
 * TCP HELPER FUNCTIONS
 * ------------------------------------------------
 */


/*
 * Send all bytes in a buffer.
 */
ssize_t send_all(int socket_fd,
                 const void *buffer,
                 size_t length)
{
    size_t total_sent = 0;

    const char *data =
        (const char *)buffer;

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

        total_sent +=
            (size_t)sent;
    }

    return (ssize_t)total_sent;
}


/*
 * Receive one newline-terminated RemoteOps
 * protocol line.
 *
 * Return values:
 *
 *   > 0 : characters received
 *     0 : peer disconnected
 *    -1 : socket error
 *    -2 : line too long
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
             * Support CRLF.
             */
            if (position > 0 &&
                buffer[position - 1] == '\r')
            {
                buffer[position - 1] = '\0';
                position--;
            }

            return (ssize_t)position;
        }

        buffer[position++] =
            character;
    }

    buffer[position] = '\0';


    /*
     * Consume the remainder of an oversized
     * protocol line.
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
 * Send a complete RemoteOps response.
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
 * ------------------------------------------------
 * SYSINFO FUNCTIONS
 * ------------------------------------------------
 */


/*
 * Read one-minute load average from
 * /proc/loadavg.
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

    if (fscanf(file,
               "%lf",
               cpu_load) != 1)
    {
        fclose(file);

        return -1;
    }

    fclose(file);

    return 0;
}


/*
 * Calculate currently used memory.
 *
 * Used memory =
 * MemTotal - MemAvailable
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

    *memory_used_mb =
        (mem_total_kb -
         mem_available_kb) / 1024;

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

    if (fscanf(file,
               "%lf",
               &uptime) != 1)
    {
        fclose(file);

        return -1;
    }

    fclose(file);

    *uptime_seconds =
        (long)uptime;

    return 0;
}


/*
 * Handle SYSINFO.
 */
int handle_sysinfo(int client_fd)
{
    double cpu_load;

    long memory_used_mb;
    long uptime_seconds;

    if (get_cpu_load(&cpu_load) < 0 ||
        get_memory_used_mb(&memory_used_mb) < 0 ||
        get_uptime_seconds(&uptime_seconds) < 0)
    {
        return send_response(
            client_fd,
            "ERR 006 SYSINFO_FAILED SID:3280\n");
    }

    char response[BUFFER_SIZE];

    int written =
        snprintf(
            response,
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
            "ERR 006 SYSINFO_FAILED SID:3280\n");
    }

    return send_response(
        client_fd,
        response);
}


/*
 * ------------------------------------------------
 * LISTPROC
 * ------------------------------------------------
 */


/*
 * Obtain a process snapshot using ps.
 *
 * Protocol:
 *
 * OK PROCS <comma-separated process names/PIDs>
 * SID:3280
 */
int handle_listproc(int client_fd)
{
    FILE *process_pipe =
        popen("ps -eo pid=,comm=", "r");

    if (process_pipe == NULL)
    {
        perror("popen");

        return send_response(
            client_fd,
            "ERR 006 PROCESS_LIST_FAILED SID:3280\n");
    }

    char response[BUFFER_SIZE];

    int written =
        snprintf(
            response,
            sizeof(response),
            "OK PROCS ");

    if (written < 0 ||
        (size_t)written >= sizeof(response))
    {
        pclose(process_pipe);

        return send_response(
            client_fd,
            "ERR 006 PROCESS_LIST_FAILED SID:3280\n");
    }

    size_t used =
        (size_t)written;

    char line[256];

    int first_process = 1;

    while (fgets(line,
                 sizeof(line),
                 process_pipe) != NULL)
    {
        int pid;

        char process_name[128];

        if (sscanf(line,
                   "%d %127s",
                   &pid,
                   process_name) != 2)
        {
            continue;
        }

        char process_entry[160];

        int entry_length =
            snprintf(
                process_entry,
                sizeof(process_entry),
                "%s%d/%s",
                first_process ? "" : ",",
                pid,
                process_name);

        if (entry_length < 0 ||
            (size_t)entry_length >=
                sizeof(process_entry))
        {
            continue;
        }

        /*
         * Keep enough room for SID and newline.
         */
        size_t required =
            (size_t)entry_length +
            strlen(" SID:3280\n") +
            1;

        if (used + required >
            sizeof(response))
        {
            break;
        }

        memcpy(
            response + used,
            process_entry,
            (size_t)entry_length);

        used +=
            (size_t)entry_length;

        response[used] = '\0';

        first_process = 0;
    }

    if (pclose(process_pipe) == -1)
    {
        perror("pclose");
    }

    int final_length =
        snprintf(
            response + used,
            sizeof(response) - used,
            " SID:3280\n");

    if (final_length < 0 ||
        (size_t)final_length >=
            sizeof(response) - used)
    {
        return send_response(
            client_fd,
            "ERR 006 PROCESS_LIST_FAILED SID:3280\n");
    }

    return send_response(
        client_fd,
        response);
}


/*
 * ------------------------------------------------
 * EXEC
 * ------------------------------------------------
 */


/*
 * Convert command output into one protocol line.
 *
 * The RemoteOps protocol requires each text
 * response to be exactly one newline-terminated
 * line.
 *
 * Newlines, tabs and repeated whitespace are
 * therefore converted into single spaces.
 */
void normalize_command_output(char *output)
{
    size_t read_position = 0;
    size_t write_position = 0;

    int previous_was_space = 1;

    while (output[read_position] != '\0')
    {
        unsigned char character =
            (unsigned char)output[read_position];

        if (isspace(character))
        {
            if (!previous_was_space)
            {
                output[write_position++] = ' ';

                previous_was_space = 1;
            }
        }
        else
        {
            output[write_position++] =
                (char)character;

            previous_was_space = 0;
        }

        read_position++;
    }

    /*
     * Remove final whitespace.
     */
    if (write_position > 0 &&
        output[write_position - 1] == ' ')
    {
        write_position--;
    }

    output[write_position] = '\0';
}


/*
 * Handle the assignment-defined EXEC command.
 *
 * Only these names are allowed:
 *
 * DATE
 * UPTIME
 * DISKFREE
 * HOSTNAME
 * WHOAMI
 *
 * No arbitrary user-provided shell command is
 * executed.
 */
int handle_exec(int client_fd,
                const char *command_name)
{
    const char *linux_command = NULL;


    /*
     * Fixed whitelist.
     */
    if (strcmp(command_name,
               "DATE") == 0)
    {
        linux_command =
            "date";
    }
    else if (strcmp(command_name,
                    "UPTIME") == 0)
    {
        linux_command =
            "uptime";
    }
    else if (strcmp(command_name,
                    "DISKFREE") == 0)
    {
        /*
         * Report disk usage for the root
         * filesystem in human-readable form.
         */
        linux_command =
            "df -h /";
    }
    else if (strcmp(command_name,
                    "HOSTNAME") == 0)
    {
        linux_command =
            "hostname";
    }
    else if (strcmp(command_name,
                    "WHOAMI") == 0)
    {
        linux_command =
            "whoami";
    }
    else
    {
        /*
         * Required assignment error response.
         */
        return send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:3280\n");
    }


    /*
     * Execute only the fixed command selected
     * above.
     */
    FILE *command_pipe =
        popen(linux_command, "r");

    if (command_pipe == NULL)
    {
        perror("popen");

        return send_response(
            client_fd,
            "ERR 006 EXEC_FAILED SID:3280\n");
    }


    char output[BUFFER_SIZE];

    size_t used = 0;

    output[0] = '\0';


    /*
     * Read command output.
     */
    while (used < sizeof(output) - 1)
    {
        size_t available =
            sizeof(output) - used;

        if (fgets(output + used,
                  (int)available,
                  command_pipe) == NULL)
        {
            break;
        }

        used =
            strlen(output);
    }


    int close_status =
        pclose(command_pipe);

    if (close_status == -1)
    {
        perror("pclose");

        return send_response(
            client_fd,
            "ERR 006 EXEC_FAILED SID:3280\n");
    }


    /*
     * Preserve the one-line framing rule.
     */
    normalize_command_output(output);


    /*
     * Ensure there is some meaningful output.
     */
    if (strlen(output) == 0)
    {
        strcpy(output,
               "NO_OUTPUT");
    }


    char response[BUFFER_SIZE];

    int written =
        snprintf(
            response,
            sizeof(response),
            "OK EXEC_RESULT %s SID:3280\n",
            output);

    if (written < 0 ||
        (size_t)written >= sizeof(response))
    {
        return send_response(
            client_fd,
            "ERR 006 EXEC_FAILED SID:3280\n");
    }


    return send_response(
        client_fd,
        response);
}


/*
 * ------------------------------------------------
 * CONTROLLER SESSION HANDLER
 * ------------------------------------------------
 */
void *handle_controller(void *arg)
{
    client_info_t *client_info =
        (client_info_t *)arg;

    int client_fd =
        client_info->client_fd;

    struct sockaddr_in client_addr =
        client_info->client_addr;

    free(client_info);


    printf(
        "[THREAD %lu] Controller connected from %s:%d\n",
        (unsigned long)pthread_self(),
        inet_ntoa(client_addr.sin_addr),
        ntohs(client_addr.sin_port));


    char line[BUFFER_SIZE];

    /*
     * Authentication state belongs to this
     * individual Controller session.
     */
    int authenticated = 0;


    while (1)
    {
        ssize_t result =
            recv_line(
                client_fd,
                line,
                sizeof(line));


        /*
         * Controller disconnected.
         */
        if (result == 0)
        {
            printf(
                "[THREAD %lu] Controller disconnected.\n",
                (unsigned long)pthread_self());

            break;
        }


        /*
         * Socket error.
         */
        if (result == -1)
        {
            perror("recv");

            break;
        }


        /*
         * Oversized command.
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
         * AUTH
         * ------------------------------------------------
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
         */
        if (strcmp(line,
                   "SYSINFO") == 0)
        {
            if (handle_sysinfo(
                    client_fd) < 0)
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
         * LISTPROC
         * ------------------------------------------------
         */
        if (strcmp(line,
                   "LISTPROC") == 0)
        {
            if (handle_listproc(
                    client_fd) < 0)
            {
                break;
            }


            printf(
                "[THREAD %lu] LISTPROC response sent.\n",
                (unsigned long)pthread_self());


            continue;
        }


        /*
         * ------------------------------------------------
         * EXEC
         * ------------------------------------------------
         *
         * EXEC must contain a command name after
         * "EXEC ".
         */
        if (strncmp(line,
                    "EXEC ",
                    5) == 0)
        {
            const char *command_name =
                line + 5;


            if (handle_exec(
                    client_fd,
                    command_name) < 0)
            {
                break;
            }


            printf(
                "[THREAD %lu] EXEC request processed: %s\n",
                (unsigned long)pthread_self(),
                command_name);


            continue;
        }


        /*
         * EXEC without a command name is also
         * rejected.
         */
        if (strcmp(line,
                   "EXEC") == 0)
        {
            if (send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:3280\n") < 0)
            {
                break;
            }


            continue;
        }


        /*
         * ------------------------------------------------
         * QUIT
         * ------------------------------------------------
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
         * Authenticated but unknown command.
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


/*
 * ------------------------------------------------
 * AGENT MAIN
 * ------------------------------------------------
 */
int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;


    /*
     * Create IPv4 TCP listening socket.
     */
    server_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0);


    if (server_fd < 0)
    {
        perror("socket");

        return EXIT_FAILURE;
    }


    /*
     * Allow address reuse during development.
     */
    int reuse = 1;


    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) < 0)
    {
        perror("setsockopt");

        close(server_fd);

        return EXIT_FAILURE;
    }


    memset(
        &server_addr,
        0,
        sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;


    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);


    server_addr.sin_port =
        htons(AGENT_PORT);


    /*
     * Bind Agent to personalised port 9461.
     */
    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        return EXIT_FAILURE;
    }


    /*
     * Listen for incoming Controller
     * connections.
     */
    if (listen(
            server_fd,
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
     * Main Agent accept loop.
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
                (struct sockaddr *)
                    &client_info->client_addr,
                &client_addr_len);


        if (client_info->client_fd < 0)
        {
            perror("accept");

            free(client_info);

            continue;
        }


        pthread_t thread_id;


        /*
         * Create one independent worker thread
         * for the new Controller.
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


            close(
                client_info->client_fd);


            free(client_info);


            continue;
        }


        /*
         * Worker resources are automatically
         * released when the thread terminates.
         */
        pthread_detach(thread_id);
    }


    close(server_fd);


    return EXIT_SUCCESS;
}
