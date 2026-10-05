#define _POSIX_C_SOURCE 200809L

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
 * ------------------------------------------------
 * TCP HELPERS
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

        total_sent +=
            (size_t)sent;
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

        buffer[position++] =
            character;
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

    if (fseek(file,
              0,
              SEEK_END) != 0)
    {
        perror("fseek");

        fclose(file);
        return 0;
    }

    long file_size =
        ftell(file);

    if (file_size < 0)
    {
        perror("ftell");

        fclose(file);
        return 0;
    }

    if (fseek(file,
              0,
              SEEK_SET) != 0)
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
        printf(
            "PUT command is too long.\n");

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
            (size_t)(file_size -
                     total_sent);

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

        total_sent +=
            (long)bytes_read;
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


/*
 * Receive exactly file_size bytes from the Agent
 * and store them in a local file.
 */
int receive_download(int sock_fd,
                     const char *filename,
                     long long file_size)
{
    /*
     * Prefix the downloaded filename so we don't
     * accidentally overwrite the local original
     * used for PUT/GET integrity testing.
     */
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

            fprintf(
                stderr,
                "GET transfer interrupted.\n");

            return -1;
        }

        size_t written =
            fwrite(file_buffer,
                   1,
                   (size_t)received,
                   file);

        if (written !=
            (size_t)received)
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


/*
 * Process the response to GET.
 */
int handle_get_response(int sock_fd,
                        const char *response)
{
    /*
     * Error responses are ordinary text lines.
     */
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

    /*
     * Expected:
     *
     * OK FILE_SEND <filename> <filesize> SID:3280
     */
    int fields =
        sscanf(
            response,
            "OK FILE_SEND %255s %lld %63s %1s",
            filename,
            &file_size,
            sid,
            extra);

    if (fields != 3 ||
        strcmp(sid,
               SID) != 0 ||
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

    /*
     * The next file_size bytes on the TCP stream
     * are raw file data.
     */
    if (receive_download(sock_fd,
                         filename,
                         file_size) < 0)
    {
        return -1;
    }

    return 0;
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

    sock_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sock_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&agent_addr,
           0,
           sizeof(agent_addr));

    agent_addr.sin_family =
        AF_INET;

    agent_addr.sin_port =
        htons(AGENT_PORT);

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

    printf(
        "Connecting to Agent at 127.0.0.1:%d...\n",
        AGENT_PORT);

    if (connect(sock_fd,
                (struct sockaddr *)&agent_addr,
                sizeof(agent_addr)) < 0)
    {
        perror("connect");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf(
        "Connected to RemoteOps Agent.\n");

    printf("----------------------------------------\n");


    /*
     * AUTHENTICATION
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

    ssize_t result =
        recv_line(sock_fd,
                  buffer,
                  sizeof(buffer));

    if (result <= 0)
    {
        if (result < 0)
        {
            perror("recv");
        }
        else
        {
            printf(
                "Agent disconnected unexpectedly.\n");
        }

        close(sock_fd);
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
    printf("  QUIT\n");
    printf("----------------------------------------\n");


    /*
     * INTERACTIVE SESSION
     */
    while (1)
    {
        char command[BUFFER_SIZE];

        printf("RemoteOps> ");
        fflush(stdout);

        if (fgets(command,
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
         * PUT local handling.
         */
        if (strncmp(command,
                    "PUT ",
                    4) == 0)
        {
            const char *local_path =
                command + 4;

            if (strlen(local_path) == 0)
            {
                printf(
                    "Usage: PUT <local-file>\n");

                continue;
            }

            int upload_result =
                upload_file(sock_fd,
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
                recv_line(sock_fd,
                          buffer,
                          sizeof(buffer));

            if (response_length <= 0)
            {
                if (response_length < 0)
                {
                    perror("recv");
                }

                break;
            }

            printf("Agent: %s\n",
                   buffer);

            continue;
        }


        /*
         * GET handling.
         *
         * Send GET as a normal protocol line.
         */
        if (strncmp(command,
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

            /*
             * First receive the text header.
             */
            ssize_t response_length =
                recv_line(
                    sock_fd,
                    buffer,
                    sizeof(buffer));

            if (response_length <= 0)
            {
                if (response_length < 0)
                {
                    perror("recv");
                }

                break;
            }

            /*
             * If successful, this function then
             * receives exactly the announced
             * number of raw file bytes.
             */
            if (handle_get_response(
                    sock_fd,
                    buffer) < 0)
            {
                break;
            }

            continue;
        }


        /*
         * Normal line-based protocol commands.
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
            fprintf(stderr,
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
            if (response_length < 0)
            {
                perror("recv");
            }

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

    close(sock_fd);

    printf("----------------------------------------\n");
    printf("Controller connection closed.\n");

    return EXIT_SUCCESS;
}
