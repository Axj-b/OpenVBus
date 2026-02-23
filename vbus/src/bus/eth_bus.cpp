
#include "eth_bus.h"
#include <algorithm>

namespace vbus {

    EthHub::EthHub(Scheduler &s, Clock &c, uint64_t bps)
        : m_Scheduler(s), m_Clock(c), m_Link_bps(bps) {}

    void EthHub::Connect(IEndpoint *ep) {
        std::scoped_lock lk(m_Mtx);
        m_EndpointList.push_back(ep);
    }

    void EthHub::Disconnect(IEndpoint *ep) {
        std::scoped_lock lk(m_Mtx);
        m_EndpointList.erase(std::remove(m_EndpointList.begin(), m_EndpointList.end(), ep), m_EndpointList.end());
    }

    void EthHub::Send(IEndpoint *src, Frame frame) {
        frame.Proto = Proto::ETH2;
        frame.Ts_ns = m_Clock.Now().count();

        // Serialization delay: (payload + 18-byte Ethernet framing) * 8 bits / link_bps
        const uint64_t bits      = (frame.Payload.size() + 18) * 8;
        const SimTime  delay     = SimTime(m_Link_bps > 0 ? bits * 1'000'000'000ULL / m_Link_bps : 0);
        const SimTime  deliverAt = m_Clock.Now() + delay;

        m_Stats.Tx_frames.fetch_add(1, std::memory_order_relaxed);

        // Capture destination list and callbacks under lock, then schedule delivery.
        std::vector<IEndpoint *> targets;
        std::function<void(const Frame &)> rec_cb;
        std::function<void(const Frame &)> sub_cb;
        std::function<void(const Frame &)> fwd_cb;
        {
            std::scoped_lock lk(m_Mtx);
            for (auto *ep : m_EndpointList)
                if (ep != src)
                    targets.push_back(ep);
            rec_cb = m_Rec_cb;
            sub_cb = m_Sub_cb;
            fwd_cb = m_Fwd_cb;
        }

        // Forward cb fires immediately on the caller's thread — zero scheduler
        // latency, essential for transparent UDP/video pass-through.
        if (fwd_cb) fwd_cb(frame);

        BusStats *stats = &m_Stats;
        m_Scheduler.post(deliverAt,
            [targets = std::move(targets), frame = std::move(frame), rec_cb = std::move(rec_cb), sub_cb = std::move(sub_cb), stats]() {
                if (rec_cb)
                    rec_cb(frame);
                if (sub_cb)
                    sub_cb(frame);
                for (auto *ep : targets) {
                    ep->On_rx(frame);
                    stats->Rx_frames.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

} // namespace vbus
