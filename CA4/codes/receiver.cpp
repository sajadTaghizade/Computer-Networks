// TCP-Mini receiver: reliable file reconstruction over a UDP socket with
// cumulative ACKs, plus optional artificial packet loss / ACK delay for
// congestion-control experiments.
//
// Usage:
//   ./receiver <port> <output_file> [--loss P] [--delay MS] [--seed N]

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

#include "logger.hpp"
#include "packet.hpp"
#include "timer.hpp"

using namespace tcpmini;

namespace {

struct Args {
    int port = 0;
    std::string outputFile;
    double lossProb = 0.0;
    int delayMs = 0;
    std::string logDir = "logs";
    unsigned int seed = 0;
    bool hasSeed = false;
};

Args parseArgs(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <output_file> [--loss P] [--delay MS] [--seed N] [--log-dir DIR]\n";
        std::exit(1);
    }
    Args args;
    args.port = std::atoi(argv[1]);
    args.outputFile = argv[2];
    for (int i = 3; i < argc; ++i) {
        std::string flag = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (flag == "--loss") {
            args.lossProb = std::stod(next("--loss"));
        } else if (flag == "--delay") {
            args.delayMs = std::stoi(next("--delay"));
        } else if (flag == "--log-dir") {
            args.logDir = next("--log-dir");
        } else if (flag == "--seed") {
            args.seed = static_cast<unsigned int>(std::stoul(next("--seed")));
            args.hasSeed = true;
        } else {
            std::cerr << "Unknown argument: " << flag << "\n";
            std::exit(1);
        }
    }
    return args;
}

void sendAck(int sock, const sockaddr_in& peer, uint32_t ackNum, uint8_t extraFlags = 0) {
    Packet ack = makePacket(0, ackNum, FLAG_ACK | extraFlags, {});
    std::vector<uint8_t> buf = serialize(ack);
    sendto(sock, buf.data(), buf.size(), 0, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(args.port);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    std::ofstream out(args.outputFile, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Cannot open output file: " << args.outputFile << "\n";
        return 1;
    }

    Logger logger(args.logDir + "/receiver_events.log", "", false);
    std::mt19937 rng(args.hasSeed ? args.seed : std::random_device{}());
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    uint32_t expectedSeq = 0;
    uint64_t bytesWritten = 0;
    uint64_t packetsReceived = 0;
    uint64_t packetsAccepted = 0;
    uint64_t packetsDroppedLoss = 0;
    uint64_t packetsCorrupt = 0;
    uint64_t duplicateOrOutOfOrder = 0;

    Stopwatch overall;
    std::cout << "[receiver] listening on port " << args.port << " -> " << args.outputFile
              << " (loss=" << args.lossProb << ", delay=" << args.delayMs << "ms)\n";

    std::vector<uint8_t> buffer(65536);
    sockaddr_in peer{};
    socklen_t peerLen = sizeof(peer);

    bool finished = false;
    while (!finished) {
        ssize_t n = recvfrom(sock, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        Packet pkt;
        if (!deserialize(buffer.data(), static_cast<size_t>(n), pkt)) {
            packetsCorrupt++;
            logger.event("CORRUPT", "dropped invalid/checksum-mismatched packet");
            continue;
        }
        packetsReceived++;

        if (pkt.header.flags & FLAG_FIN) {
            logger.event("FIN", "received FIN, sending FIN-ACK");
            sendAck(sock, peer, expectedSeq, FLAG_FIN);
            finished = true;
            break;
        }

        if (!(pkt.header.flags & FLAG_DATA)) {
            continue;  // ignore stray control packets
        }

        // Simulated artificial packet loss.
        if (args.lossProb > 0.0 && uniform(rng) < args.lossProb) {
            packetsDroppedLoss++;
            logger.event("SIM_LOSS", "seq=" + std::to_string(pkt.header.seq_num) + " dropped artificially");
            continue;
        }

        if (pkt.header.seq_num == expectedSeq) {
            out.write(reinterpret_cast<const char*>(pkt.payload.data()), pkt.payload.size());
            bytesWritten += pkt.payload.size();
            expectedSeq++;
            packetsAccepted++;
            logger.event("RECV", "seq=" + std::to_string(pkt.header.seq_num) + " len=" + std::to_string(pkt.header.length) +
                                      " accepted, expecting seq=" + std::to_string(expectedSeq));
        } else {
            duplicateOrOutOfOrder++;
            logger.event("RECV_UNEXPECTED", "seq=" + std::to_string(pkt.header.seq_num) +
                                                 " expected=" + std::to_string(expectedSeq) + " -> duplicate ACK");
        }

        if (args.delayMs > 0) {
            usleep(static_cast<useconds_t>(args.delayMs) * 1000);
        }
        sendAck(sock, peer, expectedSeq);
        logger.event("SEND_ACK", "ack_num=" + std::to_string(expectedSeq));
    }

    out.close();
    double elapsed = overall.elapsedMs();

    std::cout << "[receiver] transfer complete\n"
              << "  bytes written        : " << bytesWritten << "\n"
              << "  packets received     : " << packetsReceived << "\n"
              << "  packets accepted     : " << packetsAccepted << "\n"
              << "  packets dropped(loss): " << packetsDroppedLoss << "\n"
              << "  corrupt packets      : " << packetsCorrupt << "\n"
              << "  duplicate/out-of-order: " << duplicateOrOutOfOrder << "\n"
              << "  elapsed time (ms)    : " << elapsed << "\n";

    close(sock);
    return 0;
}
