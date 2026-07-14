# 🚀 Real-Time Embedded Systems

> **WebSocket Server Implementation for Raspberry Pi**

[![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi-red?style=flat-square&logo=raspberry-pi)](https://www.raspberrypi.org/)
[![Language](https://img.shields.io/badge/language-C-blue?style=flat-square&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)
[![Build](https://img.shields.io/badge/build-Makefile-orange?style=flat-square)](Makefile)

---

## 📖 Overview

This repository contains a **WebSocket server implementation for Raspberry Pi** developed as part of the Real-Time Embedded Systems assignment.

The application:
- 🔌 Connects to a WebSocket data stream
- ⚡ Processes incoming messages in real time
- 📊 Records runtime performance metrics for monitoring and analysis
- 🎯 Optimized for embedded systems with limited resources

---

## 📋 Requirements

Ensure your Raspberry Pi meets the following requirements before proceeding:

| Requirement | Description |
|-------------|-------------|
| 🖥️ **Hardware** | Raspberry Pi (any model with Linux support) |
| 🐧 **OS** | Raspberry Pi OS (or any Debian-based Linux) |
| 🔧 **Compiler** | GCC compiler with build tools |
| 📦 **Build System** | Make |
| 📚 **Libraries** | libwebsockets-dev, libssl-dev, libcjson-dev |
| 🖥️ **Session Manager** | tmux (for remote execution) |
| 📈 **Visualization** | gnuplot (optional, for metrics visualization) |

---

## 🛠️ Installation

### 1️⃣ Clone the Repository

```bash
git clone <repository-url>
cd <repository-folder>
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
