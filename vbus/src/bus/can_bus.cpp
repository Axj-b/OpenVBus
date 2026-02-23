
#include "can_bus.h"
#include <algorithm>

namespace vbus {

    CanBus::CanBus(Scheduler &s, Clock &c, uint32_t br)
        : m_Scheduler(s), m_Clock(c), m_Bitrate(br) {}

    void CanBus::Connect(IEndpoint *ep) {
        std::scoped_lock lk(m_Mtx);
        m_EndpointList.push_back(ep);
    }

    void CanBus::Disconnect(IEndpoint *ep) {
        std::scoped_lock lk(m_Mtx);
        m_EndpointList.erase(std::remove(m_EndpointList.begin(), m_EndpointList.end(), ep), m_EndpointList.end());
    }

    void CanBus::Send(IEndpoint *src, Frame f) {
        // CAN 2.0 frame bit count: SOF(1) + ID(11) + ctrl(6) + data(N*8) + CRC(16) + ACK(2) + EOF(7) = 43 + N*8
        // CAN FD: use extended count; approximate with 67 + N*8 for DLC.
        const bool     isFD      = (f.Proto == Proto::CANFD);
        const uint64_t dataBits  = f.Payload.size() * 8;
        const uint64_t frameBits = (isFD ? 67ULL : 43ULL) + dataBits;
        const SimTime  delay     = SimTime(m_Bitrate > 0 ? frameBits * 1'000'000'000ULL / m_Bitrate : 0);
        const SimTime  deliverAt = m_Clock.Now() + delay;

        if (f.Proto != Proto::CANFD)
            f.Proto = Proto::CAN20;
        f.Ts_ns = m_Clock.Now().count();

        m_Stats.Tx_frames.fetch_add(1, std::memory_order_relaxed);

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

        if (fwd_cb) fwd_cb(f);

        BusStats *stats = &m_Stats;
        m_Scheduler.post(deliverAt,
            [targets = std::move(targets), f = std::move(f), rec_cb = std::move(rec_cb), sub_cb = std::move(sub_cb), stats]() {
                if (rec_cb)
                    rec_cb(f);
                if (sub_cb)
                    sub_cb(f);
                for (auto *ep : targets) {
                    ep->On_rx(f);
                    stats->Rx_frames.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }

} // namespace vbus
