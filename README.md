# 🌐 Computer Networks

[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Sockets](https://img.shields.io/badge/Sockets-TCP%20%2F%20UDP-informational)](https://en.wikipedia.org/wiki/Berkeley_sockets)
[![Python](https://img.shields.io/badge/Python-matplotlib%2Fpandas-3776AB?logo=python&logoColor=white)](https://www.python.org/)

Welcome to my **Computer Networks** repository! It contains the computer
assignments for the Computer Networks course at the **University of
Tehran (UT)**: hands-on socket programming and a from-scratch reliable
transport protocol built directly on raw sockets, without any networking
framework doing the work underneath.

---

## 🛠️ Tech Stack & Tools

| Area | Tools |
|---|---|
| **Language** | C++17 |
| **Networking** | Berkeley sockets (`sys/socket.h`), TCP, raw UDP |
| **Concurrency** | `std::thread`, `std::mutex` |
| **Experimentation** | Python (`matplotlib`, `pandas`) driving simulated packet loss/delay |

---

## 📂 Repository Layout

```
.
├── CA1/   Multi-threaded TCP chat server/client with file transfer
└── CA4/   TCP-Mini — a reliable transport protocol built from scratch over UDP
```

---

## 📦 Assignments Overview

### [CA1 — TCP Chat Server & File Transfer](./CA1)
A multi-threaded TCP chat application: `ChatServer` accepts and tracks
concurrent client connections (one thread per client, a shared client map
guarded by a mutex) and broadcasts messages between them; `ChatClient`
handles sending/receiving concurrently and supports downloading files
from the server over the same connection.

### [CA4 — TCP-Mini: A Reliable Transport Protocol over UDP](./CA4)
UDP guarantees nothing — no ordering, no delivery, no congestion control.
This assignment builds exactly those guarantees back on top of it, in three
increasingly capable modes selectable at runtime:

* **Stop-and-Wait** — send one segment, wait for its ACK before the next.
* **Sliding Window** — a fixed-size window of in-flight, cumulatively-ACKed
  segments.
* **Congestion Control** — a proper `cwnd`/`ssthresh` state machine with
  slow start, congestion avoidance, and optional fast retransmit / fast
  recovery on duplicate ACKs.

Every segment carries a custom header (sequence/ack numbers, length, an
Internet-style one's-complement checksum, and control flags for
DATA/ACK/FIN) validated and retransmitted on a per-segment timer. A
`Logger` records every event plus a `cwnd`/`ssthresh` trace, and
[`scripts/run_experiments.py`](./CA4/codes/scripts/run_experiments.py)
drives the sender/receiver through three required scenarios — no loss,
moderate loss (8%, 50ms delay), and high loss — rendering the resulting
congestion-window and throughput-over-time plots used in
[`report.pdf`](./CA4/codes/report.pdf).

---

## 🚀 Getting Started

**CA1:**
```bash
cd CA1/codes
g++ -std=c++17 -pthread server.cpp -o server
g++ -std=c++17 -pthread client.cpp -o client
./server        # in one terminal
./client        # in another
```

**CA4:**
```bash
cd CA4/codes
make                                   # builds ./sender and ./receiver
./receiver <port> <output_file> &
./sender <receiver_ip> <receiver_port> <input_file> --mode congestion

# reproduce the report's experiments end-to-end
python3 scripts/run_experiments.py
```

---

## 📄 License

This repository is shared for educational purposes. See individual
assignment folders for any assignment-specific licensing notes.
