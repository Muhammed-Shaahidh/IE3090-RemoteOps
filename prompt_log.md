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
