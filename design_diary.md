# RemoteOps Design Diary

## 03 October 2026 – Initial Planning and Project Setup

I reviewed the IE3090 assignment specification and identified the main requirements of the RemoteOps system. The application will consist of an Agent acting as the server and a Controller acting as the client.

TCP will be used as the main communication channel because the assignment requires reliable communication for authentication, command execution and file transfers. UDP will be used separately for periodic monitoring information as specified by the given protocol.

The implementation will be developed entirely in C using the standard BSD sockets API.

For concurrency, the initial design decision is to use POSIX threads (pthreads), with each connected Controller being handled independently by a worker thread. This approach was selected because it allows multiple Controller connections to be processed concurrently while keeping the Agent as a single server process.

The personalised values for registration number IT24610823 were calculated and recorded before implementation. The Agent TCP port is 9461, the session identifier is SID:3280, and the authentication token is OPS-0823.

The project directory, personalised source filenames, documentation files and Agent storage directory were created before beginning the socket implementation.


## 04 October 2026 – Basic TCP Communication

The first networking implementation established a basic TCP connection between the Agent and Controller.

The Agent creates an IPv4 TCP socket, binds it to the personalised port 9461, listens for incoming connections and accepts a Controller connection. The Controller connects to the Agent using the loopback address 127.0.0.1 during local development.

A temporary HELLO message was used to verify bidirectional communication before implementing the actual assignment protocol. This temporary message is not part of the final RemoteOps protocol and will be removed when authentication is implemented.

SO_REUSEADDR was enabled on the Agent socket to make repeated development and testing easier when restarting the server.

At this stage, the Agent handles only one Controller connection. Concurrent client handling using POSIX threads will be implemented in a later stage.


## 04 October 2026 – Multi-Client Concurrency

The Agent was extended from a single-connection implementation to a concurrent server using POSIX threads.

The main Agent thread now continuously accepts incoming Controller connections. Each accepted connection is assigned to an independent worker thread using pthread_create(). This allows the Agent to continue accepting new connections while existing Controller sessions are being processed.

A separate dynamically allocated client_info_t structure is used for each Controller to prevent connection information from being overwritten between threads. Worker threads are detached using pthread_detach() so their resources can be reclaimed automatically when they terminate.

The concurrency mechanism was tested by launching multiple Controller processes against the Agent. The Agent successfully created worker threads and continued accepting additional connections.


## 04 October 2026 – Authentication and Protocol Framing

The temporary HELLO development exchange was removed and replaced with the assignment-defined AUTH command.

Each Controller session now begins in an unauthenticated state. The Agent validates the personalised authentication token OPS-0823 before allowing access to other RemoteOps functionality. Successful authentication returns OK AUTHENTICATED SID:3280, while incorrect authentication attempts and commands issued before authentication are rejected.

A persistent command-processing loop was introduced inside each Controller worker thread so the connection can remain active after authentication and process additional RemoteOps commands in later implementation stages.

Helper functions were also introduced for reliable protocol communication. recv_line() reads newline-terminated protocol messages, while send_all() ensures that an entire response buffer is transmitted even if a single send() call does not send every byte.


## 04 October 2026 – SYSINFO and Persistent Controller Session

The RemoteOps Agent was extended with the SYSINFO command. System statistics are obtained directly from the Linux /proc virtual filesystem. The one-minute system load is read from /proc/loadavg, memory usage is calculated using MemTotal and MemAvailable from /proc/meminfo, and uptime is obtained from /proc/uptime.

The Controller was also changed from an authentication-only test program into a persistent interactive session. After successful authentication, the user can enter RemoteOps commands without reconnecting for each operation.

QUIT was implemented to provide graceful session termination. The Agent returns OK BYE SID:3280 before closing the corresponding Controller connection.

SYSINFO was tested multiple times within the same authenticated TCP session and its values were compared with the underlying Linux /proc information.


## 05 October 2026 – LISTPROC Process Monitoring

The RemoteOps Agent was extended with the LISTPROC command. The implementation obtains a snapshot of currently running Linux processes using the ps utility through popen().

Process information is read from the pipe, parsed into process identifiers and command names, and formatted as a comma-separated list according to the assignment-defined OK PROCS response format. The personalised SID:3280 tag is appended to the response.

LISTPROC was tested repeatedly within an authenticated persistent TCP session. The returned process information was compared with direct output from the Linux ps command to verify the implementation.

The existing AUTH, SYSINFO and QUIT functionality was also retained and checked to ensure that the new command did not break previously implemented protocol features.


## 05 October 2026 – Restricted EXEC Command

The RemoteOps Agent was extended with the EXEC command. The assignment-defined whitelist was implemented using the five permitted command names: DATE, UPTIME, DISKFREE, HOSTNAME and WHOAMI.

Instead of directly executing command text supplied by the Controller, each permitted protocol name is mapped internally to a predefined Linux command. This prevents EXEC from providing unrestricted shell access.

Command output is obtained using popen(). Because the RemoteOps control protocol requires text responses to remain on a single newline-terminated line, embedded whitespace and newlines in command output are normalized before constructing the OK EXEC_RESULT response.

All five permitted commands were tested successfully. Non-whitelisted commands such as EXEC LS and EXEC RM were also tested and correctly rejected with ERR 002 COMMAND_NOT_ALLOWED SID:3280.


## 05 October 2026 – PUT File Upload

The RemoteOps PUT command was implemented to support authenticated file uploads from the Controller to the Agent.

Unlike the existing line-based commands, PUT requires two protocol phases. The Controller first sends the newline-terminated PUT header containing the filename and file size and then immediately transmits exactly the declared number of raw file bytes.

The Agent validates the filename and stores uploaded files under the personalised directory ./agentfiles/IT24610823/. File data is received using byte-count-based loops rather than newline framing because binary files may contain arbitrary byte values.

A maximum upload size of 10 MB was selected for this implementation. Oversized files are rejected using the assignment-defined FILE_TOO_LARGE error.

The implementation was tested using both a text file and a binary file. SHA-256 hashes were compared before and after transfer to verify byte-for-byte file integrity.
