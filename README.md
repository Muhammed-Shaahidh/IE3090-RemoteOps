# IE3090 Network Programming Assignment

## RemoteOps – Remote System Monitoring and Management Tool

**Registration Number:** IT24610823

## Personalisation Details

| Item | Value |
|---|---|
| Registration Number | IT24610823 |
| Agent Listening Port | 9461 |
| Agent Source File | agent_823.c |
| Controller Source File | controller_823.c |
| Makefile | Makefile_823 |
| Session ID | SID:3280 |
| Authentication Token | OPS-0823 |
| Log File | remoteops_IT24610823.log |
| Storage Path | ./agentfiles/IT24610823/ |
| Submission Archive | IE3090_IT24610823.zip |

## Project Overview

RemoteOps is a client/server remote system monitoring and management application implemented in C using the standard BSD sockets API.

The system consists of:

- Agent – TCP server running on the managed machine.
- Controller – TCP client used by the administrator.
- TCP control channel – used for authentication, system information, process listing, command execution and file transfer.
- UDP monitoring channel – used to periodically transmit system statistics from the Agent to the Controller.

## Supported Commands

- AUTH
- SYSINFO
- LISTPROC
- EXEC
- PUT
- GET
- MONITOR START
- MONITOR STOP
- QUIT

## Build Instructions

Build instructions will be updated as implementation progresses.

## Run Instructions

Run instructions will be updated as implementation progresses.
