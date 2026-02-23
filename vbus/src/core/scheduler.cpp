
#include "scheduler.h"

namespace vbus {

    void Scheduler::post(SimTime t, std::function<void()> fn) {
        q.push(Item{t, std::move(fn)});
    }

    void Scheduler::run_until(SimTime tmax) {
        while (!q.empty() && q.top().t <= tmax) {
            auto item = std::move(const_cast<Item &>(q.top()));
            q.pop();
            item.fn();
        }
    }

    void Scheduler::run() {
        while (!q.empty()) {
            auto item = std::move(const_cast<Item &>(q.top()));
            q.pop();
            item.fn();
        }
    }

} // namespace vbus
