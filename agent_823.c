#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9461
#define BUFFER_SIZE 1024

#define SID "SID:3280"
#define AUTH_TOKEN "OPS-0823"

#define STORAGE_DIR "./agentfiles/IT24610823"
#define MAX_FILE_SIZE (10 * 1024 * 1024)


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
 * Receive exactly the requested number of raw bytes.
 *
 * This is used for file transfer because TCP does
 * not preserve application message boundaries.
 */
ssize_t recv_all(int socket_fd,
                 void *buffer,
                 size_t length)
{
    size_t total_received = 0;
    char *data = (char *)buffer;

    while (total_received < length)
    {
        ssize_t received =
            recv(socket_fd,
                 data + total_received,
                 length - total_received,
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
            return 0;
        }

        total_received += (size_t)received;
    }

    return (ssize_t)total_received;
}


/*
 * Receive one newline-terminated RemoteOps line.
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
 * SYSINFO
 * ------------------------------------------------
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

    *uptime_seconds = (long)uptime;

    return 0;
}


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

    return send_response(client_fd,
                         response);
}


/*
 * ------------------------------------------------
 * LISTPROC
 * ------------------------------------------------
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
        snprintf(response,
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

    size_t used = (size_t)written;
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

        size_t required =
            (size_t)entry_length +
            strlen(" SID:3280\n") +
            1;

        if (used + required >
            sizeof(response))
        {
            break;
        }

        memcpy(response + used,
               process_entry,
               (size_t)entry_length);

        used += (size_t)entry_length;

        response[used] = '\0';

        first_process = 0;
    }

    if (pclose(process_pipe) == -1)
    {
        perror("pclose");
    }

    int final_length =
        snprintf(response + used,
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

    return send_response(client_fd,
                         response);
}


/*
 * ------------------------------------------------
 * EXEC
 * ------------------------------------------------
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

    if (write_position > 0 &&
        output[write_position - 1] == ' ')
    {
        write_position--;
    }

    output[write_position] = '\0';
}


int handle_exec(int client_fd,
                const char *command_name)
{
    const char *linux_command = NULL;

    if (strcmp(command_name,
               "DATE") == 0)
    {
        linux_command = "date";
    }
    else if (strcmp(command_name,
                    "UPTIME") == 0)
    {
        linux_command = "uptime";
    }
    else if (strcmp(command_name,
                    "DISKFREE") == 0)
    {
        linux_command = "df -h /";
    }
    else if (strcmp(command_name,
                    "HOSTNAME") == 0)
    {
        linux_command = "hostname";
    }
    else if (strcmp(command_name,
                    "WHOAMI") == 0)
    {
        linux_command = "whoami";
    }
    else
    {
        return send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:3280\n");
    }

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

        used = strlen(output);
    }

    if (pclose(command_pipe) == -1)
    {
        perror("pclose");

        return send_response(
            client_fd,
            "ERR 006 EXEC_FAILED SID:3280\n");
    }

    normalize_command_output(output);

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

    return send_response(client_fd,
                         response);
}


/*
 * ------------------------------------------------
 * PUT FILE UPLOAD
 * ------------------------------------------------
 */


/*
 * Only allow a simple filename.
 *
 * This prevents a Controller from using values
 * such as ../../file to escape the personalised
 * storage directory.
 */
int is_safe_filename(const char *filename)
{
    if (filename == NULL ||
        filename[0] == '\0')
    {
        return 0;
    }

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        return 0;
    }

    return 1;
}


/*
 * Receive exactly filesize bytes and save them
 * under the personalised storage directory.
 */
int handle_put(int client_fd,
               const char *filename,
               long long filesize)
{
    if (!is_safe_filename(filename) ||
        filesize < 0)
    {
        return send_response(
            client_fd,
            "ERR 007 INVALID_FILE_REQUEST SID:3280\n");
    }

    /*
     * Assignment-defined oversized file error.
     *
     * This implementation chooses 10 MB as the
     * maximum accepted upload size.
     */
    if (filesize > MAX_FILE_SIZE)
    {
        return send_response(
            client_fd,
            "ERR 004 FILE_TOO_LARGE SID:3280\n");
    }

    /*
     * Ensure personalised storage directory exists.
     */
    if (mkdir("./agentfiles", 0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir ./agentfiles");

        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    if (mkdir(STORAGE_DIR, 0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir storage");

        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    char file_path[512];

    int path_length =
        snprintf(file_path,
                 sizeof(file_path),
                 "%s/%s",
                 STORAGE_DIR,
                 filename);

    if (path_length < 0 ||
        (size_t)path_length >=
            sizeof(file_path))
    {
        return send_response(
            client_fd,
            "ERR 007 INVALID_FILE_REQUEST SID:3280\n");
    }

    FILE *file =
        fopen(file_path, "wb");

    if (file == NULL)
    {
        perror("fopen upload");

        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    /*
     * Receive the raw file body in chunks.
     *
     * Do not use recv_line() here because file data
     * may contain any byte, including newline bytes.
     */
    char file_buffer[4096];

    long long remaining = filesize;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > (long long)sizeof(file_buffer)
                ? sizeof(file_buffer)
                : (size_t)remaining;

        ssize_t received =
            recv_all(client_fd,
                     file_buffer,
                     chunk_size);

        if (received <= 0)
        {
            fclose(file);
            remove(file_path);

            printf(
                "[THREAD %lu] PUT interrupted while receiving %s\n",
                (unsigned long)pthread_self(),
                filename);

            return -1;
        }

        size_t written =
            fwrite(file_buffer,
                   1,
                   (size_t)received,
                   file);

        if (written != (size_t)received)
        {
            perror("fwrite");

            fclose(file);
            remove(file_path);

            return send_response(
                client_fd,
                "ERR 006 FILE_WRITE_FAILED SID:3280\n");
        }

        remaining -= received;
    }

    if (fclose(file) != 0)
    {
        perror("fclose upload");

        remove(file_path);

        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    char response[BUFFER_SIZE];

    int written =
        snprintf(
            response,
            sizeof(response),
            "OK FILE_RECEIVED %s SID:3280\n",
            filename);

    if (written < 0 ||
        (size_t)written >= sizeof(response))
    {
        return -1;
    }

    return send_response(client_fd,
                         response);
}


/*
 * ------------------------------------------------
 * CONTROLLER SESSION
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

    int authenticated = 0;

    while (1)
    {
        ssize_t result =
            recv_line(client_fd,
                      line,
                      sizeof(line));

        if (result == 0)
        {
            printf(
                "[THREAD %lu] Controller disconnected.\n",
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
         * AUTH
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
            }

            continue;
        }


        /*
         * SYSINFO
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
         * LISTPROC
         */
        if (strcmp(line,
                   "LISTPROC") == 0)
        {
            if (handle_listproc(client_fd) < 0)
            {
                break;
            }

            printf(
                "[THREAD %lu] LISTPROC response sent.\n",
                (unsigned long)pthread_self());

            continue;
        }


        /*
         * EXEC
         */
        if (strncmp(line,
                    "EXEC ",
                    5) == 0)
        {
            const char *command_name =
                line + 5;

            if (handle_exec(client_fd,
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
         * PUT <filename> <filesize>
         */
        if (strncmp(line,
                    "PUT ",
                    4) == 0)
        {
            char filename[256];
            long long filesize;

            char extra[2];

            int fields =
                sscanf(line,
                       "PUT %255s %lld %1s",
                       filename,
                       &filesize,
                       extra);

            if (fields != 2)
            {
                if (send_response(
                        client_fd,
                        "ERR 007 INVALID_FILE_REQUEST SID:3280\n") < 0)
                {
                    break;
                }

                continue;
            }

            /*
             * Important:
             *
             * After parsing the PUT line, the next
             * bytes on the same TCP stream are the
             * raw file body.
             */
            if (handle_put(client_fd,
                           filename,
                           filesize) < 0)
            {
                break;
            }

            printf(
                "[THREAD %lu] PUT completed: %s (%lld bytes)\n",
                (unsigned long)pthread_self(),
                filename,
                filesize);

            continue;
        }


        /*
         * QUIT
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

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

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

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

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
    printf("Storage Directory   : %s\n",
           STORAGE_DIR);
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

        pthread_detach(thread_id);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
