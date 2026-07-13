#pragma once
// Small time-keeping helpers used for RTO management and logging timestamps.

#include <chrono>

namespace tcpmini {

class Stopwatch {
public:
    Stopwatch() { reset(); }

    void reset() { start_ = std::chrono::steady_clock::now(); }

    double elapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

// Tracks a single retransmission timer (classic Go-Back-N style: one timer
// for the oldest unacknowledged packet).
class RtoTimer {
public:
    explicit RtoTimer(double rtoMs) : rtoMs_(rtoMs) {}

    void start() {
        running_ = true;
        startedAt_ = std::chrono::steady_clock::now();
    }

    void stop() { running_ = false; }

    bool isRunning() const { return running_; }

    bool expired() const {
        if (!running_) return false;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - startedAt_).count();
        return elapsed >= rtoMs_;
    }

    // Milliseconds remaining until expiry (0 if already expired or not running).
    double remainingMs() const {
        if (!running_) return rtoMs_;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - startedAt_).count();
        double remaining = rtoMs_ - elapsed;
        return remaining > 0 ? remaining : 0.0;
    }

    void setRto(double rtoMs) { rtoMs_ = rtoMs; }
    double rto() const { return rtoMs_; }

private:
    double rtoMs_;
    bool running_ = false;
    std::chrono::steady_clock::time_point startedAt_;
};

}  // namespace tcpmini
