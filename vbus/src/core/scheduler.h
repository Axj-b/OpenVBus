
#pragma once
#include <queue>
#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>
#include "clock.h"

namespace vbus {

class Scheduler {
public:
    explicit Scheduler(Clock& c) : clk(c) {}
    void post(SimTime t, std::function<void()> fn);
    void run_until(SimTime tmax);
    void run(); // until empty
    SimTime now() const { return clk.Now(); }
private:
    struct Item {
        SimTime t;
        std::function<void()> fn;
        bool operator<(const Item& o) const { return t > o.t; } // min-heap
    };
    Clock& clk;
    std::priority_queue<Item> q;
    std::mutex m_mtx;
};

} // namespace vbus
