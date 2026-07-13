// TCP-Mini sender: reads a file, splits it into segments, and reliably
// delivers it to the receiver over a UDP socket. Supports three modes that
// mirror the assignment phases:
//   stopwait    - Stop-and-Wait (window size fixed at 1)
//   window      - Sliding window with a fixed window size, cumulative ACKs
//   congestion  - Sliding window governed by cwnd (Slow Start + Congestion
//                 Avoidance), with optional Fast Retransmit / Fast Recovery
//
// Usage:
//   ./sender <receiver_ip> <receiver_port> <input_file>
//             [--mode stopwait|window|congestion]
//             [--window-size N] [--packet-size N] [--timeout-ms N]
//             [--fast-retransmit] [--fast-recovery] [--log-dir DIR]

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "logger.hpp"
#include "packet.hpp"
#include "timer.hpp"

using namespace tcpmini;

namespace {

enum class Mode { StopWait, Window, Congestion };

struct Args {
    std::string receiverIp;
    int receiverPort = 0;
    std::string inputFile;
    Mode mode = Mode::Window;
    uint32_t windowSize = 4;      // fixed window (Window mode) / receiver_window cap (Congestion mode)
    uint16_t packetSize = 512;
    double timeoutMs = 1000.0;
    bool fastRetransmit = false;
    bool fastRecovery = false;
    std::string logDir = "logs";
};

Mode parseMode(const std::string& s) {
    if (s == "stopwait") return Mode::StopWait;
    if (s == "window") return Mode::Window;
    if (s == "congestion") return Mode::Congestion;
    std::cerr << "Unknown mode: " << s << " (expected stopwait|window|congestion)\n";
    std::exit(1);
}

Args parseArgs(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <receiver_ip> <receiver_port> <input_file> [--mode stopwait|window|congestion] "
                     "[--window-size N] [--packet-size N] [--timeout-ms N] [--fast-retransmit] "
                     "[--fast-recovery] [--log-dir DIR]\n";
        std::exit(1);
    }
    Args args;
    args.receiverIp = argv[1];
    args.receiverPort = std::atoi(argv[2]);
    args.inputFile = argv[3];

    for (int i = 4; i < argc; ++i) {
        std::string flag = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (flag == "--mode") {
            args.mode = parseMode(next("--mode"));
        } else if (flag == "--window-size") {
            args.windowSize = static_cast<uint32_t>(std::stoul(next("--window-size")));
        } else if (flag == "--packet-size") {
            args.packetSize = static_cast<uint16_t>(std::stoul(next("--packet-size")));
        } else if (flag == "--timeout-ms") {
            args.timeoutMs = std::stod(next("--timeout-ms"));
        } else if (flag == "--fast-retransmit") {
            args.fastRetransmit = true;
        } else if (flag == "--fast-recovery") {
            args.fastRecovery = true;
        } else if (flag == "--log-dir") {
            args.logDir = next("--log-dir");
        } else {
            std::cerr << "Unknown argument: " << flag << "\n";
            std::exit(1);
        }
    }
    if (args.mode == Mode::StopWait) args.windowSize = 1;
    return args;
}

std::vector<std::vector<uint8_t>> loadSegments(const std::string& path, uint16_t packetSize) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file: " << path << "\n";
        std::exit(1);
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::vector<std::vector<uint8_t>> segments;
    for (size_t off = 0; off < data.size(); off += packetSize) {
        size_t len = std::min<size_t>(packetSize, data.size() - off);
        segments.emplace_back(data.begin() + off, data.begin() + off + len);
    }
    if (data.empty()) segments.emplace_back();  // still transmit a single empty segment for empty files
    return segments;
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in receiverAddr{};
    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_port = htons(args.receiverPort);
    if (inet_pton(AF_INET, args.receiverIp.c_str(), &receiverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid receiver IP: " << args.receiverIp << "\n";
        return 1;
    }

    auto segments = loadSegments(args.inputFile, args.packetSize);
    const uint32_t totalPackets = static_cast<uint32_t>(segments.size());
    uint64_t totalBytes = 0;
    for (auto& s : segments) totalBytes += s.size();

    bool trackCwnd = (args.mode == Mode::Congestion);
    Logger logger(args.logDir + "/events.log", args.logDir + "/cwnd.csv", trackCwnd);

    // Congestion-control state (only meaningful in Congestion mode).
    double cwnd = 1.0;
    double ssthresh = 16.0;
    const double receiverWindow = static_cast<double>(std::max<uint32_t>(1, args.windowSize));

    auto effectiveWindow = [&]() -> uint32_t {
        switch (args.mode) {
            case Mode::StopWait:
                return 1;
            case Mode::Window:
                return std::max<uint32_t>(1, args.windowSize);
            case Mode::Congestion:
                return static_cast<uint32_t>(std::max(1.0, std::floor(std::min(receiverWindow, cwnd))));
        }
        return 1;
    };

    auto onNewAckAdvance = [&]() {
        if (args.mode != Mode::Congestion) return;
        if (cwnd < ssthresh) {
            cwnd += 1.0;  // slow start: ~doubles per RTT
        } else {
            cwnd += 1.0 / cwnd;  // congestion avoidance: ~+1 MSS per RTT
        }
        cwnd = std::min(cwnd, receiverWindow);
    };

    auto onTimeoutCongestionReaction = [&]() {
        if (args.mode != Mode::Congestion) return;
        ssthresh = std::max(1.0, cwnd / 2.0);
        cwnd = 1.0;
    };

    uint64_t packetsSent = 0;         // DATA sends including retransmissions
    uint64_t retransmissions = 0;
    uint64_t timeouts = 0;
    uint64_t fastRetransmits = 0;
    uint64_t ackReceived = 0;

    uint32_t sendBase = 0;
    uint32_t nextSeq = 0;
    uint32_t dupAckCount = 0;

    RtoTimer timer(args.timeoutMs);
    Stopwatch overall;

    auto sendData = [&](uint32_t seq, bool isRetransmit) {
        Packet p = makePacket(seq, 0, FLAG_DATA, segments[seq]);
        std::vector<uint8_t> buf = serialize(p);
        sendto(sock, buf.data(), buf.size(), 0, reinterpret_cast<const sockaddr*>(&receiverAddr), sizeof(receiverAddr));
        packetsSent++;
        if (isRetransmit) retransmissions++;
        std::ostringstream detail;
        detail << "seq=" << seq << " len=" << segments[seq].size() << (isRetransmit ? " [RETRANSMIT]" : "");
        logger.event(isRetransmit ? "RETRANSMIT" : "SEND", detail.str());
    };

    logger.cwndSample(cwnd, ssthresh);

    while (sendBase < totalPackets) {
        uint32_t window = effectiveWindow();
        while (nextSeq < totalPackets && nextSeq < sendBase + window) {
            sendData(nextSeq, false);
            if (!timer.isRunning()) timer.start();
            nextSeq++;
        }

        double waitMs = timer.isRunning() ? std::max(1.0, timer.remainingMs()) : 100.0;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(waitMs / 1000.0);
        tv.tv_usec = static_cast<suseconds_t>(std::fmod(waitMs, 1000.0) * 1000.0);

        int ready = select(sock + 1, &readfds, nullptr, nullptr, &tv);

        if (ready > 0 && FD_ISSET(sock, &readfds)) {
            std::vector<uint8_t> buf(65536);
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            ssize_t n = recvfrom(sock, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n <= 0) continue;

            Packet pkt;
            if (!deserialize(buf.data(), static_cast<size_t>(n), pkt)) continue;
            if (!(pkt.header.flags & FLAG_ACK)) continue;

            ackReceived++;
            uint32_t ackNum = pkt.header.ack_num;

            if (ackNum > sendBase) {
                uint32_t newlyAcked = ackNum - sendBase;
                sendBase = ackNum;
                dupAckCount = 0;

                for (uint32_t i = 0; i < newlyAcked; ++i) onNewAckAdvance();
                logger.cwndSample(cwnd, ssthresh);
                logger.event("ACK", "ack_num=" + std::to_string(ackNum) + " cwnd=" + std::to_string(cwnd) +
                                         " ssthresh=" + std::to_string(ssthresh));

                if (sendBase == nextSeq) {
                    timer.stop();
                } else {
                    timer.start();  // restart RTO for the new oldest unacked packet
                }
            } else if (ackNum == sendBase && sendBase < nextSeq) {
                dupAckCount++;
                logger.event("DUP_ACK", "ack_num=" + std::to_string(ackNum) + " count=" + std::to_string(dupAckCount));

                if (args.fastRetransmit && dupAckCount == 3) {
                    fastRetransmits++;
                    sendData(sendBase, true);
                    logger.event("FAST_RETRANSMIT", "seq=" + std::to_string(sendBase));

                    if (args.mode == Mode::Congestion) {
                        ssthresh = std::max(1.0, cwnd / 2.0);
                        cwnd = args.fastRecovery ? ssthresh : 1.0;
                        logger.cwndSample(cwnd, ssthresh);
                    }
                    dupAckCount = 0;
                }
            }
            // else: stale ACK for already-fully-acknowledged data; ignore.
        } else {
            // select() timed out; check whether the RTO actually expired.
            if (timer.isRunning() && timer.expired()) {
                timeouts++;
                logger.event("TIMEOUT", "sendBase=" + std::to_string(sendBase) + " nextSeq=" + std::to_string(nextSeq));
                onTimeoutCongestionReaction();
                logger.cwndSample(cwnd, ssthresh);
                for (uint32_t seq = sendBase; seq < nextSeq; ++seq) sendData(seq, true);
                dupAckCount = 0;
                timer.start();
            }
        }
    }

    // Best-effort FIN handshake.
    {
        RtoTimer finTimer(args.timeoutMs);
        for (int attempt = 0; attempt < 5; ++attempt) {
            Packet finPkt = makePacket(totalPackets, 0, FLAG_FIN, {});
            std::vector<uint8_t> buf = serialize(finPkt);
            sendto(sock, buf.data(), buf.size(), 0, reinterpret_cast<const sockaddr*>(&receiverAddr), sizeof(receiverAddr));
            logger.event("SEND_FIN", "attempt=" + std::to_string(attempt + 1));

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            struct timeval tv;
            tv.tv_sec = static_cast<time_t>(args.timeoutMs / 1000.0);
            tv.tv_usec = static_cast<suseconds_t>(std::fmod(args.timeoutMs, 1000.0) * 1000.0);

            int ready = select(sock + 1, &readfds, nullptr, nullptr, &tv);
            if (ready > 0) {
                std::vector<uint8_t> buf2(65536);
                sockaddr_in from{};
                socklen_t fromLen = sizeof(from);
                ssize_t n = recvfrom(sock, buf2.data(), buf2.size(), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                Packet pkt;
                if (n > 0 && deserialize(buf2.data(), static_cast<size_t>(n), pkt) && (pkt.header.flags & FLAG_FIN)) {
                    logger.event("FIN_ACK", "received");
                    break;
                }
            }
        }
    }

    double elapsedMs = overall.elapsedMs();
    double elapsedSec = elapsedMs / 1000.0;
    double throughputBps = elapsedSec > 0 ? (static_cast<double>(totalBytes) / elapsedSec) : 0.0;

    std::cout << "[sender] transfer complete\n"
              << "  mode                 : "
              << (args.mode == Mode::StopWait ? "stop-and-wait" : args.mode == Mode::Window ? "sliding-window" : "congestion-control")
              << "\n"
              << "  total packets        : " << totalPackets << "\n"
              << "  total bytes           : " << totalBytes << "\n"
              << "  packets sent (w/ rtx) : " << packetsSent << "\n"
              << "  retransmissions       : " << retransmissions << "\n"
              << "  fast retransmits      : " << fastRetransmits << "\n"
              << "  timeouts              : " << timeouts << "\n"
              << "  acks received         : " << ackReceived << "\n"
              << "  elapsed time (ms)     : " << elapsedMs << "\n"
              << "  throughput (bytes/s)  : " << throughputBps << "\n";

    close(sock);
    return 0;
}
