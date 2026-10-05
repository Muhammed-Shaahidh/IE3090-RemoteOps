#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9461
#define BUFFER_SIZE 1024

#define AUTH_TOKEN "OPS-0823"
#define SID "SID:3280"


typedef struct
{
    int socket_fd;
    int running;
    pthread_mutex_t lock;
} udp_receiver_t;


/*
 * ------------------------------------------------
 * TCP HELPERS
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

    return (ssize_t)position;
}


const char *get_filename_from_path(
    const char *path)
{
    const char *slash =
        strrchr(path, '/');

    if (slash != NULL)
    {
        return slash + 1;
    }

    return path;
}


/*
 * ------------------------------------------------
 * PUT
 * ------------------------------------------------
 */

int upload_file(int sock_fd,
                const char *local_path)
{
    FILE *file =
        fopen(local_path, "rb");

    if (file == NULL)
    {
        perror("fopen");

        printf(
            "Unable to open local file: %s\n",
            local_path);

        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        perror("fseek");

        fclose(file);
        return 0;
    }

    long file_size = ftell(file);

    if (file_size < 0)
    {
        perror("ftell");

        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        perror("fseek");

        fclose(file);
        return 0;
    }

    const char *filename =
        get_filename_from_path(local_path);

    if (filename[0] == '\0')
    {
        printf("Invalid filename.\n");

        fclose(file);
        return 0;
    }

    char header[BUFFER_SIZE];

    int header_length =
        snprintf(
            header,
            sizeof(header),
            "PUT %s %ld\n",
            filename,
            file_size);

    if (header_length < 0 ||
        (size_t)header_length >=
            sizeof(header))
    {
        printf("PUT command is too long.\n");

        fclose(file);
        return 0;
    }

    if (send_all(sock_fd,
                 header,
                 (size_t)header_length) < 0)
    {
        perror("send PUT header");

        fclose(file);
        return -1;
    }

    char file_buffer[4096];

    long total_sent = 0;

    while (total_sent < file_size)
    {
        size_t remaining =
            (size_t)(file_size - total_sent);

        size_t chunk_size =
            remaining > sizeof(file_buffer)
                ? sizeof(file_buffer)
                : remaining;

        size_t bytes_read =
            fread(file_buffer,
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
            return -1;
        }

        if (send_all(sock_fd,
                     file_buffer,
                     bytes_read) < 0)
        {
            perror("send file");

            fclose(file);
            return -1;
        }

        total_sent += (long)bytes_read;
    }

    fclose(file);

    printf(
        "Uploaded raw bytes : %ld\n",
        total_sent);

    return 1;
}


/*
 * ------------------------------------------------
 * GET
 * ------------------------------------------------
 */

int receive_download(int sock_fd,
                     const char *filename,
                     long long file_size)
{
    char local_path[512];

    int path_length =
        snprintf(
            local_path,
            sizeof(local_path),
            "downloaded_%s",
            filename);

    if (path_length < 0 ||
        (size_t)path_length >=
            sizeof(local_path))
    {
        fprintf(stderr,
                "Downloaded filename is too long.\n");

        return -1;
    }

    FILE *file =
        fopen(local_path, "wb");

    if (file == NULL)
    {
        perror("fopen download");
        return -1;
    }

    char file_buffer[4096];

    long long remaining =
        file_size;

    long long total_received = 0;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining >
                    (long long)sizeof(file_buffer)
                ? sizeof(file_buffer)
                : (size_t)remaining;

        ssize_t received =
            recv_all(sock_fd,
                     file_buffer,
                     chunk_size);

        if (received <= 0)
        {
            fclose(file);
            remove(local_path);

            fprintf(stderr,
                    "GET transfer interrupted.\n");

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
            remove(local_path);

            return -1;
        }

        remaining -= received;
        total_received += received;
    }

    if (fclose(file) != 0)
    {
        perror("fclose download");

        remove(local_path);
        return -1;
    }

    printf(
        "Downloaded raw bytes: %lld\n",
        total_received);

    printf(
        "Saved locally as    : %s\n",
        local_path);

    return 0;
}


int handle_get_response(int sock_fd,
                        const char *response)
{
    if (strncmp(response,
                "ERR ",
                4) == 0)
    {
        printf("Agent: %s\n",
               response);

        return 0;
    }

    char filename[256];
    long long file_size;
    char sid[64];
    char extra[2];

    int fields =
        sscanf(
            response,
            "OK FILE_SEND %255s %lld %63s %1s",
            filename,
            &file_size,
            sid,
            extra);

    if (fields != 3 ||
        strcmp(sid, SID) != 0 ||
        file_size < 0)
    {
        fprintf(
            stderr,
            "Invalid GET response from Agent: %s\n",
            response);

        return -1;
    }

    printf("Agent: %s\n",
           response);

    return receive_download(
        sock_fd,
        filename,
        file_size);
}


/*
 * ------------------------------------------------
 * UDP RECEIVER
 * ------------------------------------------------
 */

void *udp_receiver_thread(void *arg)
{
    udp_receiver_t *receiver =
        (udp_receiver_t *)arg;

    char buffer[BUFFER_SIZE];

    while (1)
    {
        struct sockaddr_in sender_addr;

        socklen_t sender_length =
            sizeof(sender_addr);

        ssize_t received =
            recvfrom(
                receiver->socket_fd,
                buffer,
                sizeof(buffer) - 1,
                0,
                (struct sockaddr *)&sender_addr,
                &sender_length);

        if (received < 0)
        {
            pthread_mutex_lock(
                &receiver->lock);

            int still_running =
                receiver->running;

            pthread_mutex_unlock(
                &receiver->lock);

            if (!still_running)
            {
                break;
            }

            if (errno == EINTR)
            {
                continue;
            }

            perror("recvfrom");
            break;
        }

        buffer[received] = '\0';

        pthread_mutex_lock(
            &receiver->lock);

        int still_running =
            receiver->running;

        pthread_mutex_unlock(
            &receiver->lock);

        if (!still_running)
        {
            break;
        }

        printf(
            "\n[UDP MONITOR] %s\n",
            buffer);

        printf("RemoteOps> ");
        fflush(stdout);
    }

    return NULL;
}


int start_udp_receiver(
    udp_receiver_t *receiver,
    pthread_t *thread_id,
    int udp_port)
{
    receiver->socket_fd =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (receiver->socket_fd < 0)
    {
        perror("UDP socket");
        return -1;
    }

    int reuse = 1;

    if (setsockopt(
            receiver->socket_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) < 0)
    {
        perror("UDP setsockopt");

        close(receiver->socket_fd);
        return -1;
    }

    struct sockaddr_in local_addr;

    memset(&local_addr,
           0,
           sizeof(local_addr));

    local_addr.sin_family =
        AF_INET;

    local_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);

    local_addr.sin_port =
        htons((uint16_t)udp_port);

    if (bind(
            receiver->socket_fd,
            (struct sockaddr *)&local_addr,
            sizeof(local_addr)) < 0)
    {
        perror("UDP bind");

        close(receiver->socket_fd);
        return -1;
    }

    pthread_mutex_lock(
        &receiver->lock);

    receiver->running = 1;

    pthread_mutex_unlock(
        &receiver->lock);

    int result =
        pthread_create(
            thread_id,
            NULL,
            udp_receiver_thread,
            receiver);

    if (result != 0)
    {
        fprintf(
            stderr,
            "UDP pthread_create failed: %s\n",
            strerror(result));

        pthread_mutex_lock(
            &receiver->lock);

        receiver->running = 0;

        pthread_mutex_unlock(
            &receiver->lock);

        close(receiver->socket_fd);

        return -1;
    }

    return 0;
}


void stop_udp_receiver(
    udp_receiver_t *receiver,
    pthread_t thread_id)
{
    pthread_mutex_lock(
        &receiver->lock);

    int was_running =
        receiver->running;

    receiver->running = 0;

    pthread_mutex_unlock(
        &receiver->lock);

    if (!was_running)
    {
        return;
    }

    /*
     * Closing the socket wakes the blocking
     * recvfrom() call.
     */
    shutdown(
        receiver->socket_fd,
        SHUT_RDWR);

    close(receiver->socket_fd);

    pthread_join(
        thread_id,
        NULL);

    receiver->socket_fd = -1;
}


/*
 * ------------------------------------------------
 * MAIN
 * ------------------------------------------------
 */

int main(void)
{
    int sock_fd;

    struct sockaddr_in agent_addr;

    char buffer[BUFFER_SIZE];

    udp_receiver_t udp_receiver;

    memset(&udp_receiver,
           0,
           sizeof(udp_receiver));

    udp_receiver.socket_fd = -1;

    pthread_mutex_init(
        &udp_receiver.lock,
        NULL);

    pthread_t udp_thread;


    sock_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sock_fd < 0)
    {
        perror("socket");

        pthread_mutex_destroy(
            &udp_receiver.lock);

        return EXIT_FAILURE;
    }

    memset(&agent_addr,
           0,
           sizeof(agent_addr));

    agent_addr.sin_family =
        AF_INET;

    agent_addr.sin_port =
        htons(AGENT_PORT);

    if (inet_pton(
            AF_INET,
            "127.0.0.1",
            &agent_addr.sin_addr) <= 0)
    {
        fprintf(stderr,
                "Invalid Agent address.\n");

        close(sock_fd);

        pthread_mutex_destroy(
            &udp_receiver.lock);

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

    printf(
        "Connecting to Agent at 127.0.0.1:%d...\n",
        AGENT_PORT);

    if (connect(
            sock_fd,
            (struct sockaddr *)&agent_addr,
            sizeof(agent_addr)) < 0)
    {
        perror("connect");

        close(sock_fd);

        pthread_mutex_destroy(
            &udp_receiver.lock);

        return EXIT_FAILURE;
    }

    printf("Connected to RemoteOps Agent.\n");
    printf("----------------------------------------\n");


    /*
     * AUTHENTICATION
     */
    const char *auth_command =
        "AUTH OPS-0823\n";

    if (send_all(
            sock_fd,
            auth_command,
            strlen(auth_command)) < 0)
    {
        perror("send");

        close(sock_fd);

        pthread_mutex_destroy(
            &udp_receiver.lock);

        return EXIT_FAILURE;
    }

    printf("Sent: AUTH %s\n",
           AUTH_TOKEN);

    ssize_t result =
        recv_line(
            sock_fd,
            buffer,
            sizeof(buffer));

    if (result <= 0)
    {
        if (result < 0)
        {
            perror("recv");
        }

        close(sock_fd);

        pthread_mutex_destroy(
            &udp_receiver.lock);

        return EXIT_FAILURE;
    }

    printf("Agent: %s\n",
           buffer);

    if (strcmp(
            buffer,
            "OK AUTHENTICATED SID:3280") != 0)
    {
        printf("Authentication failed.\n");

        close(sock_fd);

        pthread_mutex_destroy(
            &udp_receiver.lock);

        return EXIT_FAILURE;
    }

    printf("----------------------------------------\n");
    printf("Authentication successful.\n");
    printf("RemoteOps session authenticated.\n");

    printf("----------------------------------------\n");
    printf("Available Commands:\n");
    printf("  SYSINFO\n");
    printf("  LISTPROC\n");
    printf("  EXEC DATE\n");
    printf("  EXEC UPTIME\n");
    printf("  EXEC DISKFREE\n");
    printf("  EXEC HOSTNAME\n");
    printf("  EXEC WHOAMI\n");
    printf("  PUT <local-file>\n");
    printf("  GET <filename>\n");
    printf("  MONITOR START <udp_port>\n");
    printf("  MONITOR STOP\n");
    printf("  QUIT\n");
    printf("----------------------------------------\n");


    while (1)
    {
        char command[BUFFER_SIZE];

        printf("RemoteOps> ");
        fflush(stdout);

        if (fgets(
                command,
                sizeof(command),
                stdin) == NULL)
        {
            printf("\nInput closed.\n");
            break;
        }

        command[
            strcspn(command, "\n")] = '\0';

        if (strlen(command) == 0)
        {
            continue;
        }


        /*
         * PUT
         */
        if (strncmp(
                command,
                "PUT ",
                4) == 0)
        {
            const char *local_path =
                command + 4;

            int upload_result =
                upload_file(
                    sock_fd,
                    local_path);

            if (upload_result < 0)
            {
                break;
            }

            if (upload_result == 0)
            {
                continue;
            }

            ssize_t response_length =
                recv_line(
                    sock_fd,
                    buffer,
                    sizeof(buffer));

            if (response_length <= 0)
            {
                break;
            }

            printf("Agent: %s\n",
                   buffer);

            continue;
        }


        /*
         * GET
         */
        if (strncmp(
                command,
                "GET ",
                4) == 0)
        {
            const char *filename =
                command + 4;

            if (filename[0] == '\0')
            {
                printf(
                    "Usage: GET <filename>\n");

                continue;
            }

            char get_command[BUFFER_SIZE];

            int written =
                snprintf(
                    get_command,
                    sizeof(get_command),
                    "GET %s\n",
                    filename);

            if (written < 0 ||
                (size_t)written >=
                    sizeof(get_command))
            {
                printf(
                    "GET command is too long.\n");

                continue;
            }

            if (send_all(
                    sock_fd,
                    get_command,
                    (size_t)written) < 0)
            {
                perror("send GET");
                break;
            }

            ssize_t response_length =
                recv_line(
                    sock_fd,
                    buffer,
                    sizeof(buffer));

            if (response_length <= 0)
            {
                break;
            }

            if (handle_get_response(
                    sock_fd,
                    buffer) < 0)
            {
                break;
            }

            continue;
        }


        /*
         * MONITOR START <udp_port>
         */
        if (strncmp(
                command,
                "MONITOR START ",
                14) == 0)
        {
            int udp_port;
            char extra[2];

            int fields =
                sscanf(
                    command,
                    "MONITOR START %d %1s",
                    &udp_port,
                    extra);

            if (fields != 1 ||
                udp_port < 1 ||
                udp_port > 65535)
            {
                printf(
                    "Usage: MONITOR START <udp_port>\n");

                continue;
            }

            pthread_mutex_lock(
                &udp_receiver.lock);

            int already_running =
                udp_receiver.running;

            pthread_mutex_unlock(
                &udp_receiver.lock);

            if (already_running)
            {
                printf(
                    "UDP monitor is already running.\n");

                continue;
            }

            /*
             * Bind the Controller UDP socket before
             * asking the Agent to start transmitting.
             */
            if (start_udp_receiver(
                    &udp_receiver,
                    &udp_thread,
                    udp_port) < 0)
            {
                printf(
                    "Unable to start local UDP receiver.\n");

                continue;
            }

            char protocol_message[BUFFER_SIZE];

            int written =
                snprintf(
                    protocol_message,
                    sizeof(protocol_message),
                    "MONITOR START %d\n",
                    udp_port);

            if (written < 0 ||
                (size_t)written >=
                    sizeof(protocol_message))
            {
                stop_udp_receiver(
                    &udp_receiver,
                    udp_thread);

                continue;
            }

            if (send_all(
                    sock_fd,
                    protocol_message,
                    (size_t)written) < 0)
            {
                perror("send");

                stop_udp_receiver(
                    &udp_receiver,
                    udp_thread);

                break;
            }

            ssize_t response_length =
                recv_line(
                    sock_fd,
                    buffer,
                    sizeof(buffer));

            if (response_length <= 0)
            {
                stop_udp_receiver(
                    &udp_receiver,
                    udp_thread);

                break;
            }

            printf("Agent: %s\n",
                   buffer);

            /*
             * If the Agent rejected monitoring,
             * stop the locally created UDP listener.
             */
            if (strcmp(
                    buffer,
                    "OK MONITOR_STARTED SID:3280") != 0)
            {
                stop_udp_receiver(
                    &udp_receiver,
                    udp_thread);
            }

            continue;
        }


        /*
         * MONITOR STOP
         */
        if (strcmp(
                command,
                "MONITOR STOP") == 0)
        {
            const char *message =
                "MONITOR STOP\n";

            if (send_all(
                    sock_fd,
                    message,
                    strlen(message)) < 0)
            {
                perror("send");
                break;
            }

            ssize_t response_length =
                recv_line(
                    sock_fd,
                    buffer,
                    sizeof(buffer));

            if (response_length <= 0)
            {
                break;
            }

            printf("Agent: %s\n",
                   buffer);

            pthread_mutex_lock(
                &udp_receiver.lock);

            int running =
                udp_receiver.running;

            pthread_mutex_unlock(
                &udp_receiver.lock);

            if (running)
            {
                stop_udp_receiver(
                    &udp_receiver,
                    udp_thread);
            }

            continue;
        }


        /*
         * Normal TCP command.
         */
        char protocol_message[BUFFER_SIZE];

        int written =
            snprintf(
                protocol_message,
                sizeof(protocol_message),
                "%s\n",
                command);

        if (written < 0 ||
            (size_t)written >=
                sizeof(protocol_message))
        {
            fprintf(
                stderr,
                "Command is too long.\n");

            continue;
        }

        if (send_all(
                sock_fd,
                protocol_message,
                (size_t)written) < 0)
        {
            perror("send");
            break;
        }

        ssize_t response_length =
            recv_line(
                sock_fd,
                buffer,
                sizeof(buffer));

        if (response_length <= 0)
        {
            break;
        }

        printf("Agent: %s\n",
               buffer);

        if (strcmp(command,
                   "QUIT") == 0)
        {
            break;
        }
    }


    pthread_mutex_lock(
        &udp_receiver.lock);

    int udp_running =
        udp_receiver.running;

    pthread_mutex_unlock(
        &udp_receiver.lock);

    if (udp_running)
    {
        stop_udp_receiver(
            &udp_receiver,
            udp_thread);
    }


    close(sock_fd);

    pthread_mutex_destroy(
        &udp_receiver.lock);

    printf("----------------------------------------\n");
    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
