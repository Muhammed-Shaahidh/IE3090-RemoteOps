#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>

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

#define MONITOR_INTERVAL 5

#define LOG_FILE "remoteops_IT24610823.log"


typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;
} client_info_t;


typedef struct
{
    pthread_mutex_t lock;
    pthread_cond_t cond;

    int active;
    int stop_requested;

    struct sockaddr_in destination;
} monitor_context_t;


/*
 * ------------------------------------------------
 * GLOBAL LOGGING LOCK
 * ------------------------------------------------
 *
 * Multiple Controller threads can write to the
 * same log file. This mutex prevents their log
 * entries from being mixed together.
 */

static pthread_mutex_t log_mutex =
    PTHREAD_MUTEX_INITIALIZER;


/*
 * ------------------------------------------------
 * LOGGING
 * ------------------------------------------------
 */

void write_log(const char *format, ...)
{
    pthread_mutex_lock(&log_mutex);

    FILE *log_file =
        fopen(LOG_FILE, "a");

    if (log_file == NULL)
    {
        perror("fopen log");
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t current_time =
        time(NULL);

    struct tm local_time;

    if (localtime_r(&current_time,
                    &local_time) == NULL)
    {
        fclose(log_file);
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    char timestamp[64];

    if (strftime(timestamp,
                 sizeof(timestamp),
                 "%Y-%m-%d %H:%M:%S",
                 &local_time) == 0)
    {
        fclose(log_file);
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    fprintf(log_file,
            "[%s] ",
            timestamp);

    va_list arguments;

    va_start(arguments, format);

    vfprintf(log_file,
             format,
             arguments);

    va_end(arguments);

    fprintf(log_file, "\n");

    fflush(log_file);
    fclose(log_file);

    pthread_mutex_unlock(&log_mutex);
}


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

        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
}


ssize_t recv_all(int socket_fd,
                 void *buffer,
                 size_t length)
{
    size_t total_received = 0;

    char *data =
        (char *)buffer;

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

        total_received +=
            (size_t)received;
    }

    return (ssize_t)total_received;
}


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

        buffer[position++] =
            character;
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
 * SYSTEM INFORMATION HELPERS
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


int get_memory_used_mb(
    long *memory_used_mb)
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


int get_uptime_seconds(
    long *uptime_seconds)
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
 * ------------------------------------------------
 * SYSINFO
 * ------------------------------------------------
 */

int handle_sysinfo(int client_fd)
{
    double cpu_load;
    long memory_used_mb;
    long uptime_seconds;

    if (get_cpu_load(&cpu_load) < 0 ||
        get_memory_used_mb(
            &memory_used_mb) < 0 ||
        get_uptime_seconds(
            &uptime_seconds) < 0)
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
        (size_t)written >=
            sizeof(response))
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
        (size_t)written >=
            sizeof(response))
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

void normalize_command_output(
    char *output)
{
    size_t read_position = 0;
    size_t write_position = 0;

    int previous_was_space = 1;

    while (output[read_position] != '\0')
    {
        unsigned char character =
            (unsigned char)
                output[read_position];

        if (isspace(character))
        {
            if (!previous_was_space)
            {
                output[write_position++] =
                    ' ';

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

    while (used <
           sizeof(output) - 1)
    {
        size_t available =
            sizeof(output) - used;

        if (fgets(
                output + used,
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
        (size_t)written >=
            sizeof(response))
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
 * FILE HELPERS
 * ------------------------------------------------
 */

int is_safe_filename(
    const char *filename)
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


int ensure_storage_directory(void)
{
    if (mkdir("./agentfiles",
              0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir ./agentfiles");
        return -1;
    }

    if (mkdir(STORAGE_DIR,
              0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir storage");
        return -1;
    }

    return 0;
}


/*
 * ------------------------------------------------
 * PUT
 * ------------------------------------------------
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

    if (filesize > MAX_FILE_SIZE)
    {
        write_log(
            "PUT rejected: file=%s size=%lld reason=FILE_TOO_LARGE",
            filename,
            filesize);

        return send_response(
            client_fd,
            "ERR 004 FILE_TOO_LARGE SID:3280\n");
    }

    if (ensure_storage_directory() < 0)
    {
        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    char file_path[512];

    int path_length =
        snprintf(
            file_path,
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

        write_log(
            "PUT failed: file=%s reason=FILE_WRITE_FAILED",
            filename);

        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    char file_buffer[4096];

    long long remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining >
                    (long long)
                        sizeof(file_buffer)
                ? sizeof(file_buffer)
                : (size_t)remaining;

        ssize_t received =
            recv_all(
                client_fd,
                file_buffer,
                chunk_size);

        if (received <= 0)
        {
            fclose(file);
            remove(file_path);

            write_log(
                "PUT interrupted: file=%s",
                filename);

            return -1;
        }

        size_t written =
            fwrite(
                file_buffer,
                1,
                (size_t)received,
                file);

        if (written !=
            (size_t)received)
        {
            perror("fwrite");

            fclose(file);
            remove(file_path);

            write_log(
                "PUT failed: file=%s reason=FILE_WRITE_FAILED",
                filename);

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

        write_log(
            "PUT failed: file=%s reason=FILE_WRITE_FAILED",
            filename);

        return send_response(
            client_fd,
            "ERR 006 FILE_WRITE_FAILED SID:3280\n");
    }

    write_log(
        "FILE_UPLOAD completed: file=%s size=%lld path=%s",
        filename,
        filesize,
        file_path);

    char response[BUFFER_SIZE];

    int written =
        snprintf(
            response,
            sizeof(response),
            "OK FILE_RECEIVED %s SID:3280\n",
            filename);

    if (written < 0 ||
        (size_t)written >=
            sizeof(response))
    {
        return -1;
    }

    return send_response(
        client_fd,
        response);
}


/*
 * ------------------------------------------------
 * GET
 * ------------------------------------------------
 */

int handle_get(int client_fd,
               const char *filename)
{
    if (!is_safe_filename(filename))
    {
        return send_response(
            client_fd,
            "ERR 007 INVALID_FILE_REQUEST SID:3280\n");
    }

    if (ensure_storage_directory() < 0)
    {
        return send_response(
            client_fd,
            "ERR 006 FILE_READ_FAILED SID:3280\n");
    }

    char file_path[512];

    int path_length =
        snprintf(
            file_path,
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
        fopen(file_path, "rb");

    if (file == NULL)
    {
        write_log(
            "GET rejected: file=%s reason=FILE_NOT_FOUND",
            filename);

        return send_response(
            client_fd,
            "ERR 005 FILE_NOT_FOUND SID:3280\n");
    }

    if (fseek(file,
              0,
              SEEK_END) != 0)
    {
        perror("fseek");

        fclose(file);

        return send_response(
            client_fd,
            "ERR 006 FILE_READ_FAILED SID:3280\n");
    }

    long file_size =
        ftell(file);

    if (file_size < 0)
    {
        perror("ftell");

        fclose(file);

        return send_response(
            client_fd,
            "ERR 006 FILE_READ_FAILED SID:3280\n");
    }

    if (fseek(file,
              0,
              SEEK_SET) != 0)
    {
        perror("fseek");

        fclose(file);

        return send_response(
            client_fd,
            "ERR 006 FILE_READ_FAILED SID:3280\n");
    }

    char response[BUFFER_SIZE];

    int response_length =
        snprintf(
            response,
            sizeof(response),
            "OK FILE_SEND %s %ld SID:3280\n",
            filename,
            file_size);

    if (response_length < 0 ||
        (size_t)response_length >=
            sizeof(response))
    {
        fclose(file);
        return -1;
    }

    if (send_all(
            client_fd,
            response,
            (size_t)response_length) < 0)
    {
        perror("send GET header");

        fclose(file);
        return -1;
    }

    char file_buffer[4096];

    long total_sent = 0;

    while (total_sent < file_size)
    {
        size_t remaining =
            (size_t)
                (file_size -
                 total_sent);

        size_t chunk_size =
            remaining >
                    sizeof(file_buffer)
                ? sizeof(file_buffer)
                : remaining;

        size_t bytes_read =
            fread(
                file_buffer,
                1,
                chunk_size,
                file);

        if (bytes_read == 0)
        {
            if (ferror(file))
            {
                perror("fread");
            }

            fclose(file);

            write_log(
                "GET interrupted: file=%s",
                filename);

            return -1;
        }

        if (send_all(
                client_fd,
                file_buffer,
                bytes_read) < 0)
        {
            perror("send GET file");

            fclose(file);

            write_log(
                "GET interrupted: file=%s",
                filename);

            return -1;
        }

        total_sent +=
            (long)bytes_read;
    }

    fclose(file);

    printf(
        "[THREAD %lu] GET sent: %s (%ld bytes)\n",
        (unsigned long)pthread_self(),
        filename,
        total_sent);

    write_log(
        "FILE_DOWNLOAD completed: file=%s size=%ld path=%s",
        filename,
        total_sent,
        file_path);

    return 0;
}


/*
 * ------------------------------------------------
 * UDP MONITORING
 * ------------------------------------------------
 */

void *monitor_thread(void *arg)
{
    monitor_context_t *context =
        (monitor_context_t *)arg;

    int udp_socket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (udp_socket < 0)
    {
        perror("UDP socket");

        pthread_mutex_lock(
            &context->lock);

        context->active = 0;

        pthread_mutex_unlock(
            &context->lock);

        return NULL;
    }

    while (1)
    {
        struct sockaddr_in destination;

        pthread_mutex_lock(
            &context->lock);

        if (context->stop_requested)
        {
            pthread_mutex_unlock(
                &context->lock);

            break;
        }

        destination =
            context->destination;

        pthread_mutex_unlock(
            &context->lock);

        double cpu_load;
        long memory_used_mb;
        long uptime_seconds;

        if (get_cpu_load(
                &cpu_load) == 0 &&
            get_memory_used_mb(
                &memory_used_mb) == 0 &&
            get_uptime_seconds(
                &uptime_seconds) == 0)
        {
            char datagram[BUFFER_SIZE];

            int length =
                snprintf(
                    datagram,
                    sizeof(datagram),
                    "SYSINFO %.2f %ld %ld SID:3280",
                    cpu_load,
                    memory_used_mb,
                    uptime_seconds);

            if (length > 0 &&
                (size_t)length <
                    sizeof(datagram))
            {
                ssize_t sent =
                    sendto(
                        udp_socket,
                        datagram,
                        (size_t)length,
                        0,
                        (struct sockaddr *)
                            &destination,
                        sizeof(destination));

                if (sent < 0)
                {
                    perror("sendto");
                }
            }
        }

        struct timespec timeout;

        if (clock_gettime(
                CLOCK_REALTIME,
                &timeout) != 0)
        {
            perror("clock_gettime");
            break;
        }

        timeout.tv_sec +=
            MONITOR_INTERVAL;

        pthread_mutex_lock(
            &context->lock);

        if (!context->stop_requested)
        {
            pthread_cond_timedwait(
                &context->cond,
                &context->lock,
                &timeout);
        }

        int should_stop =
            context->stop_requested;

        pthread_mutex_unlock(
            &context->lock);

        if (should_stop)
        {
            break;
        }
    }

    close(udp_socket);

    pthread_mutex_lock(
        &context->lock);

    context->active = 0;

    pthread_mutex_unlock(
        &context->lock);

    return NULL;
}


int start_monitoring(
    monitor_context_t *context,
    pthread_t *monitor_tid,
    const struct sockaddr_in *client_addr,
    int udp_port)
{
    if (udp_port < 1 ||
        udp_port > 65535)
    {
        return -1;
    }

    pthread_mutex_lock(
        &context->lock);

    if (context->active)
    {
        pthread_mutex_unlock(
            &context->lock);

        return -2;
    }

    memset(
        &context->destination,
        0,
        sizeof(context->destination));

    context->destination.sin_family =
        AF_INET;

    context->destination.sin_addr =
        client_addr->sin_addr;

    context->destination.sin_port =
        htons((uint16_t)udp_port);

    context->stop_requested = 0;
    context->active = 1;

    pthread_mutex_unlock(
        &context->lock);

    int result =
        pthread_create(
            monitor_tid,
            NULL,
            monitor_thread,
            context);

    if (result != 0)
    {
        fprintf(
            stderr,
            "monitor pthread_create failed: %s\n",
            strerror(result));

        pthread_mutex_lock(
            &context->lock);

        context->active = 0;

        pthread_mutex_unlock(
            &context->lock);

        return -1;
    }

    return 0;
}


void stop_monitoring(
    monitor_context_t *context,
    pthread_t monitor_tid)
{
    pthread_mutex_lock(
        &context->lock);

    int was_active =
        context->active;

    if (was_active)
    {
        context->stop_requested = 1;

        pthread_cond_signal(
            &context->cond);
    }

    pthread_mutex_unlock(
        &context->lock);

    if (was_active)
    {
        pthread_join(
            monitor_tid,
            NULL);
    }
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

    char client_ip[
        INET_ADDRSTRLEN];

    if (inet_ntop(
            AF_INET,
            &client_addr.sin_addr,
            client_ip,
            sizeof(client_ip)) == NULL)
    {
        strcpy(client_ip,
               "UNKNOWN");
    }

    int client_port =
        ntohs(client_addr.sin_port);

    printf(
        "[THREAD %lu] Controller connected from %s:%d\n",
        (unsigned long)pthread_self(),
        client_ip,
        client_port);

    write_log(
        "CONNECTION opened: client=%s:%d thread=%lu",
        client_ip,
        client_port,
        (unsigned long)pthread_self());

    char line[BUFFER_SIZE];

    int authenticated = 0;

    monitor_context_t monitor;

    memset(
        &monitor,
        0,
        sizeof(monitor));

    pthread_mutex_init(
        &monitor.lock,
        NULL);

    pthread_cond_init(
        &monitor.cond,
        NULL);

    pthread_t monitor_tid;

    while (1)
    {
        ssize_t result =
            recv_line(
                client_fd,
                line,
                sizeof(line));

        if (result == 0)
        {
            printf(
                "[THREAD %lu] Controller disconnected.\n",
                (unsigned long)pthread_self());

            write_log(
                "DISCONNECTION unexpected/client_closed: client=%s:%d",
                client_ip,
                client_port);

            break;
        }

        if (result == -1)
        {
            perror("recv");

            write_log(
                "CONNECTION error: client=%s:%d",
                client_ip,
                client_port);

            break;
        }

        if (result == -2)
        {
            write_log(
                "COMMAND rejected: client=%s:%d reason=LINE_TOO_LONG",
                client_ip,
                client_port);

            if (send_response(
                    client_fd,
                    "ERR 003 LINE_TOO_LONG SID:3280\n") < 0)
            {
                break;
            }

            continue;
        }


        /*
         * Do not write the real AUTH token
         * into the log file.
         */
        if (strncmp(
                line,
                "AUTH ",
                5) == 0)
        {
            write_log(
                "COMMAND received: client=%s:%d command=AUTH [REDACTED]",
                client_ip,
                client_port);
        }
        else
        {
            write_log(
                "COMMAND received: client=%s:%d command=%s",
                client_ip,
                client_port,
                line);
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
            if (strncmp(
                    line,
                    "AUTH ",
                    5) == 0)
            {
                const char *token =
                    line + 5;

                if (strcmp(
                        token,
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

                    write_log(
                        "AUTH success: client=%s:%d SID:3280",
                        client_ip,
                        client_port);
                }
                else
                {
                    write_log(
                        "AUTH failed: client=%s:%d",
                        client_ip,
                        client_port);

                    if (send_response(
                            client_fd,
                            "ERR 001 AUTH_FAILED SID:3280\n") < 0)
                    {
                        break;
                    }
                }
            }
            else
            {
                write_log(
                    "AUTH required: client=%s:%d",
                    client_ip,
                    client_port);

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
        if (strcmp(
                line,
                "SYSINFO") == 0)
        {
            if (handle_sysinfo(
                    client_fd) < 0)
            {
                break;
            }

            continue;
        }


        /*
         * LISTPROC
         */
        if (strcmp(
                line,
                "LISTPROC") == 0)
        {
            if (handle_listproc(
                    client_fd) < 0)
            {
                break;
            }

            continue;
        }


        /*
         * EXEC
         */
        if (strncmp(
                line,
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

            continue;
        }

        if (strcmp(
                line,
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
         * PUT
         */
        if (strncmp(
                line,
                "PUT ",
                4) == 0)
        {
            char filename[256];

            long long filesize;

            char extra[2];

            int fields =
                sscanf(
                    line,
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

            if (handle_put(
                    client_fd,
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
         * GET
         */
        if (strncmp(
                line,
                "GET ",
                4) == 0)
        {
            char filename[256];

            char extra[2];

            int fields =
                sscanf(
                    line,
                    "GET %255s %1s",
                    filename,
                    extra);

            if (fields != 1)
            {
                if (send_response(
                        client_fd,
                        "ERR 007 INVALID_FILE_REQUEST SID:3280\n") < 0)
                {
                    break;
                }

                continue;
            }

            if (handle_get(
                    client_fd,
                    filename) < 0)
            {
                break;
            }

            continue;
        }


        /*
         * MONITOR START
         */
        if (strncmp(
                line,
                "MONITOR START ",
                14) == 0)
        {
            int udp_port;

            char extra[2];

            int fields =
                sscanf(
                    line,
                    "MONITOR START %d %1s",
                    &udp_port,
                    extra);

            if (fields != 1 ||
                udp_port < 1 ||
                udp_port > 65535)
            {
                write_log(
                    "MONITOR rejected: client=%s:%d reason=INVALID_UDP_PORT",
                    client_ip,
                    client_port);

                if (send_response(
                        client_fd,
                        "ERR 008 INVALID_UDP_PORT SID:3280\n") < 0)
                {
                    break;
                }

                continue;
            }

            int monitor_result =
                start_monitoring(
                    &monitor,
                    &monitor_tid,
                    &client_addr,
                    udp_port);

            if (monitor_result == -2)
            {
                if (send_response(
                        client_fd,
                        "ERR 009 MONITOR_ALREADY_RUNNING SID:3280\n") < 0)
                {
                    break;
                }

                continue;
            }

            if (monitor_result < 0)
            {
                if (send_response(
                        client_fd,
                        "ERR 010 MONITOR_START_FAILED SID:3280\n") < 0)
                {
                    break;
                }

                continue;
            }

            if (send_response(
                    client_fd,
                    "OK MONITOR_STARTED SID:3280\n") < 0)
            {
                stop_monitoring(
                    &monitor,
                    monitor_tid);

                break;
            }

            printf(
                "[THREAD %lu] UDP monitoring started for %s:%d\n",
                (unsigned long)pthread_self(),
                client_ip,
                udp_port);

            write_log(
                "MONITOR started: client=%s:%d udp_port=%d interval=%d_seconds",
                client_ip,
                client_port,
                udp_port,
                MONITOR_INTERVAL);

            continue;
        }


        /*
         * MONITOR STOP
         */
        if (strcmp(
                line,
                "MONITOR STOP") == 0)
        {
            pthread_mutex_lock(
                &monitor.lock);

            int monitor_active =
                monitor.active;

            pthread_mutex_unlock(
                &monitor.lock);

            if (monitor_active)
            {
                stop_monitoring(
                    &monitor,
                    monitor_tid);
            }

            if (send_response(
                    client_fd,
                    "OK MONITOR_STOPPED SID:3280\n") < 0)
            {
                break;
            }

            printf(
                "[THREAD %lu] UDP monitoring stopped.\n",
                (unsigned long)pthread_self());

            write_log(
                "MONITOR stopped: client=%s:%d",
                client_ip,
                client_port);

            continue;
        }


        /*
         * QUIT
         */
        if (strcmp(
                line,
                "QUIT") == 0)
        {
            pthread_mutex_lock(
                &monitor.lock);

            int monitor_active =
                monitor.active;

            pthread_mutex_unlock(
                &monitor.lock);

            if (monitor_active)
            {
                stop_monitoring(
                    &monitor,
                    monitor_tid);

                write_log(
                    "MONITOR stopped automatically during QUIT: client=%s:%d",
                    client_ip,
                    client_port);
            }

            if (send_response(
                    client_fd,
                    "OK BYE SID:3280\n") < 0)
            {
                break;
            }

            write_log(
                "QUIT completed: client=%s:%d SID:3280",
                client_ip,
                client_port);

            break;
        }


        /*
         * UNKNOWN COMMAND
         */
        write_log(
            "COMMAND rejected: client=%s:%d command=%s reason=UNKNOWN_COMMAND",
            client_ip,
            client_port,
            line);

        if (send_response(
                client_fd,
                "ERR 003 UNKNOWN_COMMAND SID:3280\n") < 0)
        {
            break;
        }
    }


    /*
     * Stop monitoring if the Controller
     * disconnects without MONITOR STOP/QUIT.
     */
    pthread_mutex_lock(
        &monitor.lock);

    int monitor_active =
        monitor.active;

    pthread_mutex_unlock(
        &monitor.lock);

    if (monitor_active)
    {
        stop_monitoring(
            &monitor,
            monitor_tid);

        write_log(
            "MONITOR stopped automatically after disconnection: client=%s:%d",
            client_ip,
            client_port);
    }


    pthread_cond_destroy(
        &monitor.cond);

    pthread_mutex_destroy(
        &monitor.lock);

    close(client_fd);

    printf(
        "[THREAD %lu] Controller session closed.\n",
        (unsigned long)pthread_self());

    write_log(
        "CONNECTION closed: client=%s:%d thread=%lu",
        client_ip,
        client_port,
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
        socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

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

    if (bind(
            server_fd,
            (struct sockaddr *)
                &server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);
        return EXIT_FAILURE;
    }

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
    printf("Storage Directory   : %s\n",
           STORAGE_DIR);
    printf("Log File            : %s\n",
           LOG_FILE);
    printf("Concurrency Model   : POSIX threads\n");
    printf("Monitor Interval    : %d seconds\n",
           MONITOR_INTERVAL);
    printf("----------------------------------------\n");
    printf("Waiting for Controller connections...\n\n");

    write_log(
        "AGENT started: port=%d SID:3280 storage=%s",
        AGENT_PORT,
        STORAGE_DIR);

    while (1)
    {
        client_info_t *client_info =
            malloc(
                sizeof(client_info_t));

        if (client_info == NULL)
        {
            perror("malloc");
            continue;
        }

        socklen_t client_addr_len =
            sizeof(
                client_info->client_addr);

        client_info->client_fd =
            accept(
                server_fd,
                (struct sockaddr *)
                    &client_info->client_addr,
                &client_addr_len);

        if (client_info->client_fd < 0)
        {
            if (errno == EINTR)
            {
                free(client_info);
                continue;
            }

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

            close(
                client_info->client_fd);

            free(client_info);

            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);

    pthread_mutex_destroy(
        &log_mutex);

    return EXIT_SUCCESS;
}
