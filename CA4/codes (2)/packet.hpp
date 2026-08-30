#pragma once
// TCP-Mini packet format: header + payload, (de)serialization and checksum.

#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace tcpmini {

constexpr uint16_t MAX_PAYLOAD_DEFAULT = 1024;

enum Flags : uint8_t {
    FLAG_DATA = 1 << 0,
    FLAG_ACK = 1 << 1,
    FLAG_FIN = 1 << 2,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t seq_num;   // index of this segment (or 0 for pure ACK/FIN control packets)
    uint32_t ack_num;   // next expected seq_num (cumulative ACK)
    uint16_t length;    // payload length in bytes
    uint16_t checksum;  // 16-bit one's complement checksum over header+payload
    uint8_t flags;      // bitmask of Flags
};
#pragma pack(pop)

constexpr size_t HEADER_SIZE = sizeof(PacketHeader);

struct Packet {
    PacketHeader header{};
    std::vector<uint8_t> payload;
};

// Internet-style 16-bit one's complement checksum over the header (with the
// checksum field itself treated as 0) followed by the payload bytes.
inline uint16_t computeChecksum(const PacketHeader& header, const uint8_t* payload, uint16_t length) {
    PacketHeader tmp = header;
    tmp.checksum = 0;

    uint32_t sum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&tmp);
    size_t n = sizeof(PacketHeader);
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint16_t word = (static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1];
        sum += word;
    }
    if (n % 2 == 1) sum += static_cast<uint16_t>(bytes[n - 1]) << 8;

    for (size_t i = 0; i + 1 < length; i += 2) {
        uint16_t word = (static_cast<uint16_t>(payload[i]) << 8) | payload[i + 1];
        sum += word;
    }
    if (length % 2 == 1) sum += static_cast<uint16_t>(payload[length - 1]) << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

inline std::vector<uint8_t> serialize(const Packet& packet) {
    PacketHeader net{};
    net.seq_num = htonl(packet.header.seq_num);
    net.ack_num = htonl(packet.header.ack_num);
    net.length = htons(packet.header.length);
    net.flags = packet.header.flags;
    net.checksum = htons(packet.header.checksum);

    std::vector<uint8_t> buffer(HEADER_SIZE + packet.payload.size());
    std::memcpy(buffer.data(), &net, HEADER_SIZE);
    if (!packet.payload.empty()) {
        std::memcpy(buffer.data() + HEADER_SIZE, packet.payload.data(), packet.payload.size());
    }
    return buffer;
}

inline bool deserialize(const uint8_t* buffer, size_t size, Packet& out) {
    if (size < HEADER_SIZE) return false;

    PacketHeader net{};
    std::memcpy(&net, buffer, HEADER_SIZE);

    out.header.seq_num = ntohl(net.seq_num);
    out.header.ack_num = ntohl(net.ack_num);
    out.header.length = ntohs(net.length);
    out.header.flags = net.flags;
    out.header.checksum = ntohs(net.checksum);

    size_t payloadSize = size - HEADER_SIZE;
    if (out.header.length != payloadSize) return false;

    out.payload.assign(buffer + HEADER_SIZE, buffer + size);

    uint16_t expected = computeChecksum(out.header, out.payload.data(), out.header.length);
    return expected == out.header.checksum;
}

// Builds a packet and fills in length + checksum.
inline Packet makePacket(uint32_t seq, uint32_t ack, uint8_t flags, std::vector<uint8_t> payload) {
    Packet p;
    p.header.seq_num = seq;
    p.header.ack_num = ack;
    p.header.flags = flags;
    p.header.length = static_cast<uint16_t>(payload.size());
    p.payload = std::move(payload);
    p.header.checksum = computeChecksum(p.header, p.payload.data(), p.header.length);
    return p;
}

}  // namespace tcpmini
