#pragma once
#include "app_state.h"
#include "backends/iface_base.h"
#include "daemon_client.h"
#include <random>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <deque>
#include <utility>

namespace ovb {

class Model {
  public:
    explicit Model(AppState &s);
    ~Model();
    Bus                       &newBus(const std::string &name);
    void                       deleteBus(uint32_t id);
    Bus                       *getBus(uint32_t id);
    void                       tick(double dt);
    std::vector<InterfaceDesc> enumerateIfaces() const;
    void                       attachIface(Bus &b, const InterfaceDesc &d);
    void                       detachIface(Bus &b);
    void                       startRecord(Bus &b);
    void                       stopRecord(Bus &b);
    void                       startRecordAll();
    void                       stopRecordAll();
    void                       startReplayAll();
    void                       replayFile(Bus &b, const std::string &mode);
    void                       startForward(Bus &b);
    void                       stopForward(Bus &b);
    void                       addLog(const std::string &msg);

  private:
    AppState        &state;
    std::mt19937_64  rng{1234};
    uint64_t         m_tick_ns{0};

    std::unordered_map<uint32_t, std::unique_ptr<iface::Base>> m_ifaces;

    DaemonClient m_daemon;
    std::unordered_map<uint32_t, std::unique_ptr<DaemonClient>> m_subs;

    std::mutex                                m_queue_mtx;
    std::deque<std::pair<uint32_t, RawFrame>> m_frame_queue;

    void subscribeFrames(Bus &b);
    void unsubscribeFrames(uint32_t busId);
};

} // namespace ovb