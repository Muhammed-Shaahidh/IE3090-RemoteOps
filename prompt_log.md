# RemoteOps AI Prompt Log

## Interaction 01

**Date:** 03 October 2026

**AI Tool:** ChatGPT

**Stage:** Assignment analysis and implementation planning

**Prompt Summary:**
Asked ChatGPT to review the IE3090 Network Programming assignment brief, explain the purpose of the RemoteOps assignment, identify the submission requirements, and provide a step-by-step implementation plan.

**AI Assistance Received:**
ChatGPT explained the Agent/Controller architecture, TCP and UDP communication requirements, mandatory RemoteOps commands, personalised parameters, required report evidence, GitHub requirements, prompt log, reflection and final submission structure.

**How the Output Was Used:**
The explanation was used to understand the assignment requirements and create an implementation roadmap. No complete application code was copied into the project at this stage.

**What I Learned:**
I understood that the Agent acts as the server and the Controller acts as the client. TCP is used for reliable command and file-transfer communication, while UDP is used for periodic monitoring messages.


## Interaction 02

**Date:** 03 October 2026

**AI Tool:** ChatGPT

**Stage:** Personalisation and initial project setup

**Prompt Summary:**
Provided registration number IT24610823 and requested guidance to develop the implementation and Implementation Report in parallel.

**AI Assistance Received:**
ChatGPT helped calculate the personalised Agent port, source filenames, session ID, authentication token, log filename, storage directory and submission ZIP filename. It also provided guidance for creating the initial project structure, README, design diary and Implementation Report.

**How the Output Was Used:**
The calculated values were checked against the formulas in the assignment specification and then used to initialise the project documentation and directory structure.

**What I Learned:**
I learned how the assignment derives unique implementation values from the registration number and why these values must remain consistent throughout the Agent, Controller, README, report and final submission.


## Interaction 03

**Date:** 04 October 2026

**AI Tool:** ChatGPT

**Stage:** Basic TCP Agent and Controller implementation

**Prompt Summary:**
Requested the next implementation stage after completing the initial RemoteOps project structure.

**AI Assistance Received:**
ChatGPT provided guidance for implementing the first TCP connection between the Agent and Controller using the BSD sockets API. The guidance covered socket creation, binding, listening, accepting connections, connecting from the Controller, and performing an initial send/receive test.

**How the Output Was Used:**
The suggested implementation was reviewed and used to build the initial TCP communication stage. The temporary HELLO exchange was used only to verify basic connectivity before implementing the assignment-defined protocol.

**What I Learned:**
I learned the roles of socket(), bind(), listen(), accept(), connect(), send() and recv() in a TCP client/server application. I also learned that the Agent uses the personalised port 9461 and that htons() converts the port value to network byte order.


## Interaction 04

**Date:** 04 October 2026

**AI Tool:** ChatGPT

**Stage:** Multi-client concurrency implementation

**Prompt Summary:**
Requested the next development stage after successfully completing and committing the basic TCP Agent and Controller communication.

**AI Assistance Received:**
ChatGPT provided guidance for extending the Agent to handle multiple Controller connections using POSIX threads. The guidance included pthread_create(), pthread_detach(), per-client connection structures, dynamic memory allocation, compilation with -pthread, and concurrency testing.

**How the Output Was Used:**
The suggested thread-based design was reviewed and integrated into the Agent. The implementation was compiled and tested by launching multiple Controller processes.

**What I Learned:**
I learned how a TCP server can continue accepting connections while separate worker threads process individual clients. I also learned the purpose of pthread_create(), pthread_detach(), and dynamically allocating per-client connection information.


## Interaction 05

**Date:** 04 October 2026

**AI Tool:** ChatGPT

**Stage:** Authentication and protocol framing

**Prompt Summary:**
Requested the next implementation stage after completing multi-client concurrency.

**AI Assistance Received:**
ChatGPT provided guidance for replacing the temporary HELLO test with the assignment-defined AUTH protocol. The guidance included personalised token validation, per-session authentication state, rejection of unauthenticated commands, persistent Controller sessions, newline-based command handling and reliable send operations.

**How the Output Was Used:**
The authentication design was reviewed and integrated into the Agent and Controller. Successful authentication, invalid authentication and command access before authentication were tested.

**What I Learned:**
I learned how authentication state can be maintained independently for each TCP client session. I also learned that TCP is a byte-stream protocol and that application-level message boundaries must be handled explicitly rather than assuming that each recv() call corresponds to one complete command.


## Interaction 06

**Date:** 04 October 2026

**AI Tool:** ChatGPT

**Stage:** SYSINFO command and persistent Controller session

**Prompt Summary:**
Requested continuation of the RemoteOps implementation after completing authentication and pushing the existing project history to GitHub.

**AI Assistance Received:**
ChatGPT provided guidance for implementing the SYSINFO command using the Linux /proc filesystem, maintaining an interactive authenticated Controller session, implementing graceful QUIT handling, testing the returned system statistics and documenting the implementation.

**How the Output Was Used:**
The suggested design was reviewed and integrated into the Agent and Controller. SYSINFO was tested within a persistent authenticated session and the returned values were compared with Linux system information.

**What I Learned:**
I learned how Linux exposes system information through the /proc virtual filesystem and how a persistent TCP session can process multiple application-level commands after authentication.


## Interaction 07

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** LISTPROC process listing

**Prompt Summary:**
Requested continuation of the RemoteOps assignment using the uploaded assignment brief as the authoritative protocol specification.

**AI Assistance Received:**
ChatGPT reviewed the assignment-defined LISTPROC protocol and provided guidance for obtaining a Linux process snapshot using popen(), formatting it as the required single-line OK PROCS response, integrating LISTPROC into the authenticated command handler, testing the result and documenting the implementation.

**How the Output Was Used:**
The proposed approach was reviewed and integrated into the existing Agent. The Controller command menu was updated and LISTPROC was tested against the Linux process table.

**What I Learned:**
I learned how popen() can be used to read the output of another Linux process through a stream and how dynamically generated operating-system information can be converted into an application-level TCP protocol response.



## Interaction 08

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** Restricted EXEC command implementation

**Prompt Summary:**
Requested continuation of the RemoteOps implementation after completing LISTPROC, while preserving the complete current Agent and Controller source code.

**AI Assistance Received:**
ChatGPT reviewed the assignment-defined EXEC protocol and provided guidance for implementing the fixed whitelist containing DATE, UPTIME, DISKFREE, HOSTNAME and WHOAMI. It also suggested mapping protocol names to predefined Linux commands instead of executing arbitrary Controller input, normalizing command output to preserve the line-based protocol, and testing both allowed and disallowed commands.

**How the Output Was Used:**
The proposed implementation was reviewed and integrated into the existing Agent and Controller. All five permitted commands were tested, and commands outside the whitelist were tested to confirm that they were rejected.

**What I Learned:**
I learned how a command whitelist can provide restricted remote execution without exposing an unrestricted shell. I also learned how popen() can capture command output and why multi-line operating-system output must be converted to the single-line format required by the application protocol.


## Interaction 09

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** PUT file upload

**Prompt Summary:**
Requested continuation of the RemoteOps assignment with the PUT file-upload stage and complete updated Agent and Controller source files.

**AI Assistance Received:**
ChatGPT reviewed the assignment-defined PUT protocol and provided guidance for combining a newline-terminated PUT header with an exact raw-byte transfer. It suggested separate byte-count-based receive logic for file data, binary-mode file handling, personalised storage, filename validation, upload-size checking, and SHA-256 integrity testing.

**How the Output Was Used:**
The suggested implementation was reviewed and integrated into the existing Agent and Controller while preserving AUTH, SYSINFO, LISTPROC, EXEC and QUIT functionality. Text and binary files were uploaded and their stored copies were verified using SHA-256 hashes.

**What I Learned:**
I learned that TCP transfers a continuous byte stream and does not preserve file or message boundaries. Therefore, a file-transfer protocol must explicitly track the expected number of bytes and continue calling send() and recv() until the complete file has been transferred.



## Interaction 10

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** GET file download

**Prompt Summary:**
Requested continuation of the RemoteOps assignment with the GET file-download stage while preserving the complete current Agent and Controller implementations.

**AI Assistance Received:**
ChatGPT provided guidance for implementing the assignment-defined GET protocol using a newline-terminated FILE_SEND header followed by an exact raw-byte transfer. The guidance included Agent-side binary file transmission, Controller-side exact-byte reception, filename validation, missing-file handling and SHA-256 integrity verification.

**How the Output Was Used:**
The suggested GET implementation was integrated into the existing Agent and Controller while retaining AUTH, SYSINFO, LISTPROC, EXEC, PUT and QUIT functionality. Text and binary downloads were tested and compared with their original files.

**What I Learned:**
I learned how a TCP application can transition safely between line-based control messages and binary payloads on the same persistent connection. I also learned why the receiver must consume exactly the announced file size before returning to normal command processing.



## Interaction 11

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** UDP periodic monitoring

**Prompt Summary:**
Requested continuation of the RemoteOps implementation with the assignment-defined UDP periodic monitoring feature and complete updated Agent and Controller source files.

**AI Assistance Received:**
ChatGPT provided guidance for implementing MONITOR START and MONITOR STOP over the existing authenticated TCP channel while using a secondary UDP socket for periodic system-statistics transmission. The proposed implementation used separate POSIX threads for Agent-side monitoring and Controller-side UDP reception.

**How the Output Was Used:**
The suggested approach was reviewed and integrated into the existing RemoteOps implementation while preserving AUTH, SYSINFO, LISTPROC, EXEC, PUT, GET and QUIT functionality. UDP monitoring was tested together with normal TCP command processing and graceful monitoring termination.

**What I Learned:**
I learned how TCP and UDP can be used together within the same application. TCP provides reliable ordered command and response communication, while UDP can provide lightweight asynchronous monitoring updates. I also learned how threads allow the Controller to receive UDP datagrams without blocking its interactive TCP command loop.



## Interaction 12

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** Logging and graceful disconnection

**Prompt Summary:**
Requested continuation of the RemoteOps assignment after UDP monitoring, with complete updated source code whenever implementation changes were required.

**AI Assistance Received:**
ChatGPT provided an updated Agent implementation containing timestamped persistent logging, mutex-protected concurrent log writing, authentication-token redaction, file-transfer logging, monitoring activity logging and graceful cleanup of monitoring resources during QUIT or unexpected Controller disconnection.

**How the Output Was Used:**
The logging functionality was integrated into the complete Agent implementation while retaining AUTH, SYSINFO, LISTPROC, EXEC, PUT, GET, UDP monitoring and QUIT functionality. The personalised log file was inspected after a complete RemoteOps session and graceful and unexpected disconnection scenarios were tested.

**What I Learned:**
I learned why a multi-threaded server requires synchronization when several threads write to one shared log file. I also learned how graceful connection termination should clean up associated resources, such as monitoring threads and sockets, before a client session finishes.



## Interaction 13

**Date:** 05 October 2026

**AI Tool:** ChatGPT

**Stage:** Protocol framing and error-handling verification

**Prompt Summary:**
Requested continuation of the RemoteOps assignment after completing logging and graceful session cleanup.

**AI Assistance Received:**
ChatGPT provided a structured set of tests for newline-based TCP framing, partial command delivery, multiple commands within one TCP stream, authentication enforcement, unknown commands, EXEC whitelist rejection, missing files, oversized uploads, invalid UDP ports and oversized control messages.

**How the Output Was Used:**
The existing RemoteOps Agent was tested using netcat and controlled shell input to simulate several TCP stream and protocol-error conditions. The results were documented in Section 14 of the implementation report.

**What I Learned:**
I learned that TCP does not preserve application message boundaries and that an application must implement its own framing mechanism. I also learned why RemoteOps uses newline framing for control messages but exact byte-count framing for raw file transfers.
