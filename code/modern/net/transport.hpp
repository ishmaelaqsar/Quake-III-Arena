#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <queue>
#include <cstdint>
#include <optional>
#include "bitstream.hpp"

namespace q3::net {

struct NetAddress {
    std::string ip{"127.0.0.1"};
    uint16_t port{27960};

    bool operator==(const NetAddress& rhs) const noexcept {
        return port == rhs.port && ip == rhs.ip;
    }

    std::string to_string() const {
        return ip + ":" + std::to_string(port);
    }
};

struct Packet {
    NetAddress address;
    std::vector<uint8_t> payload;
};

class INetTransport {
public:
    virtual ~INetTransport() = default;

    virtual bool open(uint16_t port) = 0;
    virtual void close() = 0;
    virtual bool is_open() const noexcept = 0;

    virtual bool send(const NetAddress& to, const uint8_t* data, std::size_t size) = 0;
    virtual std::optional<Packet> receive() = 0;

    bool send(const NetAddress& to, const BitWriter& writer) {
        return send(to, writer.data(), writer.byte_size());
    }
};

class LoopbackTransport : public INetTransport {
public:
    explicit LoopbackTransport(NetAddress local_addr = {"127.0.0.1", 27960})
        : local_addr_(std::move(local_addr)), open_(true) {}

    bool open(uint16_t port) override {
        local_addr_.port = port;
        open_ = true;
        return true;
    }

    void close() override {
        open_ = false;
        while (!packet_queue_.empty()) packet_queue_.pop();
    }

    bool is_open() const noexcept override {
        return open_;
    }

    bool send(const NetAddress& to, const uint8_t* data, std::size_t size) override {
        if (!open_) return false;
        Packet pkt;
        pkt.address = local_addr_;
        pkt.payload.assign(data, data + size);
        packet_queue_.push(std::move(pkt));
        return true;
    }

    std::optional<Packet> receive() override {
        if (!open_ || packet_queue_.empty()) {
            return std::nullopt;
        }
        Packet pkt = std::move(packet_queue_.front());
        packet_queue_.pop();
        return pkt;
    }

    // Connect two loopback endpoints for local multiplayer / split screen simulation
    void pipe_to(LoopbackTransport& other, const uint8_t* data, std::size_t size) {
        Packet pkt;
        pkt.address = local_addr_;
        pkt.payload.assign(data, data + size);
        other.packet_queue_.push(std::move(pkt));
    }

private:
    NetAddress local_addr_;
    bool open_{false};
    std::queue<Packet> packet_queue_;
};

} // namespace q3::net
