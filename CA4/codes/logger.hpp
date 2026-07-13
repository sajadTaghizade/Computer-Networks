#pragma once
// Event log (events.log) and congestion-window trace (cwnd.csv) writers.

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "timer.hpp"

namespace tcpmini {

class Logger {
public:
    Logger(const std::string& eventsPath, const std::string& cwndCsvPath, bool trackCwnd) : trackCwnd_(trackCwnd) {
        events_.open(eventsPath, std::ios::out | std::ios::trunc);
        if (trackCwnd_) {
            cwnd_.open(cwndCsvPath, std::ios::out | std::ios::trunc);
            cwnd_ << "time_ms,cwnd,ssthresh\n";
        }
        clock_.reset();
    }

    void event(const std::string& tag, const std::string& detail) {
        double t = clock_.elapsedMs();
        std::ostringstream line;
        line << "[" << std::fixed << std::setprecision(2) << t << "ms] " << tag << " " << detail;
        if (events_.is_open()) events_ << line.str() << "\n";
    }

    void cwndSample(double cwnd, double ssthresh) {
        if (!trackCwnd_ || !cwnd_.is_open()) return;
        cwnd_ << std::fixed << std::setprecision(2) << clock_.elapsedMs() << "," << cwnd << "," << ssthresh << "\n";
    }

    double elapsedMs() const { return clock_.elapsedMs(); }

private:
    std::ofstream events_;
    std::ofstream cwnd_;
    bool trackCwnd_;
    Stopwatch clock_;
};

}  // namespace tcpmini
