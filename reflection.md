
---

# `reflection.md` — 300–500 words

Your assignment requires a **300–500 word reflection**. Use this as your final reflection:

```markdown
# Reflection – RemoteOps Assignment

**Registration Number:** IT24610823

Developing RemoteOps gave me practical experience in network programming beyond simply creating a connection between two programs. Before completing this assignment, I understood the basic purpose of TCP and UDP, but implementing a complete application helped me understand how these protocols behave in real programs.

One of the most important things I learned was that TCP is a byte-stream protocol and does not preserve application message boundaries. A command sent using one send operation may be received in several parts, while multiple commands may also arrive together. To solve this, I implemented newline-based framing for RemoteOps control messages. PUT and GET required a different approach because file data can contain any byte value. Therefore, I used the declared file size and processed exactly that number of bytes. Testing files using SHA-256 hashes helped me verify that the transfer was byte-for-byte correct.

Concurrency was another important learning area. The Agent uses POSIX threads so that several Controllers can remain connected simultaneously. Testing five Controller connections helped me understand how a server can accept new clients while existing clients continue executing commands. It also introduced synchronization issues. For example, multiple threads can write to the same activity log, so I used a mutex to protect the shared log file.

I also learned how TCP and UDP can be combined in one application. TCP is used for reliable authenticated commands, while UDP provides periodic system-monitoring updates. A separate monitoring thread allows UDP statistics to be transmitted without blocking TCP command processing.

Security-related design decisions were also important. Instead of allowing arbitrary shell commands, RemoteOps supports only a fixed EXEC whitelist. Uploaded files are restricted to a personalised storage directory, filenames are validated, and the authentication token is redacted from the log.

AI assistance was useful for reviewing protocol requirements, planning implementation stages, identifying test cases and understanding networking concepts. However, I compiled, executed and tested each stage in the Linux environment and used the observed results to verify the implementation.

Overall, this assignment improved my understanding of socket programming, protocol design, TCP framing, UDP communication, concurrency, synchronization, file transfer and error handling. The most valuable lesson was that reliable network software requires careful handling of message boundaries, partial communication, failures and shared resources rather than only establishing a socket connection.
