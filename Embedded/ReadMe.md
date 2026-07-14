# Real-Time Embedded Systems

Welcome to the **Real-Time Embedded Systems** project.

This repository contains a **WebSocket server implementation for Raspberry Pi** developed as part of the Real-Time Embedded Systems assignment.

The application connects to a WebSocket data stream, processes incoming messages in real time, and records runtime performance metrics for monitoring and analysis.

---

# Requirements

Before building and running the project, make sure you have:

- Raspberry Pi running Linux (Raspberry Pi OS recommended)
- GCC compiler
- Make build system
- Required development libraries
- `tmux` for running the application remotely
- `gnuplot` for metric visualization (optional)

---

# Installation

Clone the repository:

```bash
git clone <repository-url>
cd <repository-folder>
```

Install the required dependencies:

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    libwebsockets-dev \
    libssl-dev \
    libcjson-dev \
    pkg-config \
    gnuplot \
    tmux
```

These packages provide:

- **GCC / build-essential** → C compiler and build tools
- **libwebsockets-dev** → WebSocket communication support
- **libssl-dev** → SSL/TLS support
- **libcjson-dev** → JSON parsing
- **pkg-config** → Library configuration
- **gnuplot** → Data visualization
- **tmux** → Persistent terminal sessions

---

# Building the Project

Before compiling, clean any previous build files:

```bash
make clean
```

Compile the project:

```bash
make
```

If compilation errors occur, ensure all required libraries are installed correctly.

After a successful build, the executable will be generated:

```bash
./jetstream
```

---

# Running the WebSocket Server

Because the application is designed to run continuously on a Raspberry Pi, it is recommended to execute it inside a `tmux` session.

Start a new tmux session:

```bash
tmux
```

Run the server:

```bash
./jetstream
```

The program will now run inside the tmux environment.

To detach from the session while keeping the server running:

```
CTRL + B
then
D
```

You can safely disconnect from SSH and the application will continue running.

---

# Monitoring Runtime Metrics

The server continuously records runtime information into:

```
metrics_log.txt
```

To monitor the metrics in real time:

```bash
tail -f metrics_log.txt
```

The log file can be used to verify:

- WebSocket connection status
- Message processing activity
- Runtime performance
- CPU usage
- Buffer behavior
- System stability

---

# Managing tmux Sessions

List active tmux sessions:

```bash
tmux ls
```

Reconnect to an existing session:

```bash
tmux attach
```

or:

```bash
tmux attach -t <session-name>
```

Terminate a tmux session:

```bash
tmux kill-session -t <session-name>
```

---

# Project Structure

```
.
├── main.c
├── producer.c
├── producer.h
├── consumer.c
├── consumer.h
├── logger.c
├── logger.h
├── common.c
├── common.h
├── network_reset.c
├── network_reset.c
├── Makefile
├── jetstream
└── metrics_log.txt
```

---

# Notes

- The system is designed for real-time execution on embedded hardware.
- Long-running execution should always be performed inside `tmux`.
- Monitoring `metrics_log.txt` is recommended to detect connection issues or performance degradation.
- The project has been tested on Raspberry Pi hardware.

---

# Real-Time Embedded Systems Assignment

**WebSocket Server for Raspberry Pi**

Developed as part of the Real-Time Embedded Systems course.
