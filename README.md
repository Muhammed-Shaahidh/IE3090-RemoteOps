# RemoteOps

## IE3090 Network Programming Assignment

**Student Registration Number:** IT24610823

RemoteOps is a TCP/IP-based remote system monitoring and management tool implemented in C using BSD sockets.

The system consists of:

- **Agent** – runs on the managed Linux machine.
- **Controller** – connects to the Agent and performs authenticated RemoteOps operations.

---

## Personalisation

| Item | Value |
|---|---|
| Registration Number | IT24610823 |
| Agent TCP Port | 9461 |
| Authentication Token | OPS-0823 |
| Session ID | SID:3280 |
| Agent Source | agent_823.c |
| Controller Source | controller_823.c |
| Makefile | Makefile_823 |
| Storage Directory | ./agentfiles/IT24610823/ |
| Log File | remoteops_IT24610823.log |

---

## Implemented Features

RemoteOps supports:

- TCP Agent/Controller communication
- Authentication
- SYSINFO
- LISTPROC
- Restricted EXEC
- PUT file upload
- GET file download
- UDP periodic monitoring
- Multiple simultaneous Controllers
- Timestamped activity logging
- Protocol framing
- Error handling
- Graceful disconnection

---

## Supported EXEC Commands

Only the following commands are permitted:

- DATE
- UPTIME
- DISKFREE
- HOSTNAME
- WHOAMI

Other EXEC commands are rejected.

---

## Build

Compile the Agent and Controller using:

```bash
make -f Makefile_823
