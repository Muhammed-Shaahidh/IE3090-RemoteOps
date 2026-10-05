#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

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

        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
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


/*
 * Return only the filename portion of a local path.
 *
 * Example:
 *
 * ./testfiles/sample.txt
 *
 * becomes:
 *
 * sample.txt
 */
const char *get_filename_from_path(const char *path)
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
 * Upload one local file using the assignment's
 * PUT wire protocol.
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

    /*
     * Determine file size.
     */
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

    /*
     * Construct assignment-defined PUT header.
     */
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

    /*
     * First send the newline-terminated PUT header.
     */
    if (send_all(sock_fd,
                 header,
                 (size_t)header_length) < 0)
    {
        perror("send PUT header");

        fclose(file);
        return -1;
    }

    /*
     * Then immediately send exactly file_size
     * raw bytes.
     */
    char file_buffer[4096];

    long total_sent = 0;

    while (total_sent < file_size)
    {
        size_t bytes_read =
            fread(file_buffer,
                  1,
                  sizeof(file_buffer),
                  file);

        if (bytes_read > 0)
        {
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

        if (bytes_read < sizeof(file_buffer))
        {
            if (ferror(file))
            {
                perror("fread");

                fclose(file);
                return -1;
            }

            break;
        }
    }

    fclose(file);

    if (total_sent != file_size)
    {
        fprintf(
            stderr,
            "File transfer incomplete. "
            "Expected %ld bytes, sent %ld bytes.\n",
            file_size,
            total_sent);

        return -1;
    }

    printf(
        "Uploaded raw bytes : %ld\n",
        total_sent);

    return 1;
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

    printf("Connected to RemoteOps Agent.\n");
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

    if (result < 0)
    {
        perror("recv");

        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (result == 0)
    {
        printf(
            "Agent disconnected unexpectedly.\n");

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
         * ------------------------------------------------
         * LOCAL PUT HANDLING
         * ------------------------------------------------
         *
         * User interface:
         *
         * PUT <local-file>
         *
         * Wire protocol:
         *
         * PUT <filename> <filesize>\n
         * <raw bytes>
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

            /*
             * Local file could not be opened.
             * No protocol command was sent.
             */
            if (upload_result == 0)
            {
                continue;
            }

            /*
             * File bytes have been sent.
             * Wait for Agent confirmation.
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
                printf(
                    "Agent disconnected.\n");

                break;
            }

            printf("Agent: %s\n",
                   buffer);

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
            fprintf(
                stderr,
                "Command is too long.\n");

            continue;
        }

        if (send_all(sock_fd,
                     protocol_message,
                     strlen(protocol_message)) < 0)
        {
            perror("send");
            break;
        }

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
            printf(
                "Agent disconnected.\n");

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
