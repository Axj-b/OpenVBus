
#include "scheduler.h"

namespace vbus {

    void Scheduler::post(SimTime t, std::function<void()> fn) {
        std::scoped_lock lk(m_mtx);
        q.push(Item{t, std::move(fn)});
    }

    void Scheduler::run_until(SimTime tmax) {
        // Drain a snapshot under lock to avoid holding mutex during callbacks.
        std::vector<Item> ready;
        {
            std::scoped_lock lk(m_mtx);
            while (!q.empty() && q.top().t <= tmax) {
                ready.push_back(std::move(const_cast<Item &>(q.top())));
                q.pop();
            }
        }
        for (auto &item : ready)
            item.fn();
    }

    void Scheduler::run() {
        std::vector<Item> ready;
        {
            std::scoped_lock lk(m_mtx);
            while (!q.empty()) {
                ready.push_back(std::move(const_cast<Item &>(q.top())));
                q.pop();
            }
        }
        for (auto &item : ready)
            item.fn();
    }

} // namespace vbus
