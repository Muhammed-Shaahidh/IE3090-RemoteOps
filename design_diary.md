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

