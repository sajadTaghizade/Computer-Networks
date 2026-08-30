# 🌐 Computer Networks

[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Sockets](https://img.shields.io/badge/Sockets-TCP%20%2F%20UDP-informational)](https://en.wikipedia.org/wiki/Berkeley_sockets)
[![ns-3](https://img.shields.io/badge/ns--3-WiFi%20simulation-orange)](https://www.nsnam.org/)
[![GNS3](https://img.shields.io/badge/GNS3-Cisco%20routing-informational)](https://www.gns3.com/)
[![Python](https://img.shields.io/badge/Python-matplotlib%2Fpandas-3776AB?logo=python&logoColor=white)](https://www.python.org/)

Welcome to my **Computer Networks** repository! It contains the computer
assignments for the Computer Networks course at the **University of
Tehran (UT)**: hands-on socket programming, a from-scratch reliable
transport protocol, wireless network simulation, and real router
configuration — the stack from application-layer sockets down to Wi-Fi PHY
behaviour and IP routing.

---

## 🛠️ Tech Stack & Tools

| Area | Tools |
|---|---|
| **Language** | C++17 |
| **Networking** | Berkeley sockets (`sys/socket.h`), TCP, raw UDP |
| **Concurrency** | `std::thread`, `std::mutex` |
| **Wireless simulation** | [ns-3](https://www.nsnam.org/) (WiFi, mobility, spectrum, flow-monitor modules) |
| **Routing / topology** | [GNS3](https://www.gns3.com/) with Dynamips-emulated Cisco IOS routers and switches |
| **Experimentation** | Python (`matplotlib`, `pandas`) driving simulated packet loss/delay |

---

## 📂 Repository Layout

```
.
├── CA1/   Multi-threaded TCP chat server/client with file transfer
├── CA2/   ns-3 WiFi network simulation (802.11ac -> 802.11ax, mobility, SINR)
├── CA3/   GNS3 router topologies with static IP routing (Cisco IOS)
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

### [CA2 — Wireless Network Simulation (ns-3)](./CA2)
Four ns-3 simulations of a single-AP WiFi network (one access point, five
stations), each phase changing one variable and measuring the effect with
the `flow-monitor` module:

* **Phase 1/2** — 802.11ac, stations at fixed positions, used to establish a
  throughput/delay baseline (and to isolate the effect of receiver noise
  figure between the two).
* **Phase 3/4** — upgraded to 802.11ax, the AP raised to a fixed height with
  stations placed via a random-disc position allocator around it, and
  per-packet SINR sampled through the `spectrum` module — moving from an
  idealized fixed layout to a more realistic, randomized deployment.

Flow statistics for each phase are exported as `phaseN-flowmon.xml` and
analyzed in [`Report.pdf`](./CA2/Report.pdf).

### [CA3 — Router Topologies & Static Routing (GNS3)](./CA3)
Four GNS3 network topologies (`CA3`, `CA3-3`, `CA3-4`, `CA3-5`) of increasing
complexity, built from Dynamips-emulated Cisco IOS routers and switches with
virtual PCs (VPCS) as end hosts. Each topology is configured with static
`ip route` entries to achieve full reachability across subnets connected
over Ethernet and serial (WAN) links, with packet captures taken on key
links to verify traffic actually takes the configured path. Full analysis
in [`Rport.pdf`](./CA3/Rport.pdf).

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

**CA2** (requires an [ns-3](https://www.nsnam.org/) installation):
```bash
cp CA2/phase1.cc <ns-3-dir>/scratch/
cd <ns-3-dir> && ./ns3 run scratch/phase1
```

**CA3** — open any `.gns3` project under `CA3/GNS files/` in GNS3 (with
Dynamips/VPCS configured) and start the topology; router configs are
pre-loaded from the committed `*-startup-config.cfg` files.

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
