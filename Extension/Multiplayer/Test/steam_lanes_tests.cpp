#include "Extension/Multiplayer/Steam/steam_lanes.h"
#include <iostream>
#include <stdexcept>

using namespace dingosdk::multiplayer;
namespace {
void check(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
struct FakeMessage : SteamNetworkingMessage_t { FakeMessage() : SteamNetworkingMessage_t() {} };
struct Fake {
    static inline int allocated{}, released{}, sent{};
    static inline bool fail_allocation{};
    static inline int64 result = 1;
    static inline EResult configured = k_EResultOK;
    static inline uint16 lane{};
    static inline int flags{};
    static inline std::vector<std::uint8_t> payload;
    static EResult configure(void *, HSteamNetConnection connection, int count, const int *priorities,
                              const uint16 *weights) {
        check(connection == 42 && count == 4, "Wrong lane configuration target");
        check(priorities[1] < priorities[0] && priorities[0] == priorities[2] &&
              weights[0] == 16 && weights[2] == 1 && weights[3] == 4 && priorities[3] == priorities[0], "Gameplay preference starves control, cosmetics or voice");
        return configured;
    }
    static SteamNetworkingMessage_t *allocate(void *, int size) {
        if (fail_allocation) return nullptr;
        auto *message = new FakeMessage{};
        message->m_pData = new std::uint8_t[size];
        message->m_cbSize = size;
        ++allocated;
        return message;
    }
    static void send(void *, int count, SteamNetworkingMessage_t **messages, int64 *out, bool take_ownership) {
        check(count == 1 && take_ownership && messages[0]->m_conn == 42, "Wrong Steam message ownership/connection");
        auto *message = messages[0];
        lane = message->m_idxLane;
        flags = message->m_nFlags;
        const auto *bytes = static_cast<std::uint8_t *>(message->m_pData);
        payload.assign(bytes, bytes + message->m_cbSize);
        out[0] = result;
        delete[] bytes;
        delete static_cast<FakeMessage *>(message);
        ++sent;
        ++released;
    }
};
}
int main() {
    try {
        SteamLanes api{reinterpret_cast<void *>(1), Fake::configure, Fake::allocate, Fake::send};
        check(api.setup(nullptr, 42), "Available Steam lanes not configured");
        Fake::configured = k_EResultInvalidState;
        check(!api.setup(nullptr, 42) && !SteamLanes{}.setup(nullptr, 42), "Missing/failed lane setup cannot fall back");
        const std::vector<std::uint8_t> bytes{3, 5, 7};
        for (const auto kind : {PacketKind::pose, PacketKind::audio, PacketKind::cosmetics, PacketKind::welcome,
                               PacketKind::roster, PacketKind::world_state, PacketKind::voice}) {
            const auto expected = kind == PacketKind::voice ? 3 : kind == PacketKind::pose || kind == PacketKind::audio ? 0
                : kind == PacketKind::cosmetics ? 2 : 1;
            check(api.transmit(nullptr, 42, bytes, k_nSteamNetworkingSend_ReliableNoNagle, traffic_lane(kind)) == k_EResultOK,
                  "Valid lane message failed");
            check(Fake::lane == expected && Fake::payload == bytes && Fake::flags == k_nSteamNetworkingSend_ReliableNoNagle,
                  "Message lost lane, flags or payload");
        }
        for (const auto result : {-static_cast<int64>(k_EResultIgnored), -static_cast<int64>(k_EResultLimitExceeded), int64{0}}) {
            Fake::result = result;
            check(api.transmit(nullptr, 42, bytes, 0, TrafficLane::gameplay) != k_EResultOK,
                  "Failed Steam enqueue treated as delivered");
            check(Fake::allocated == Fake::released, "Failed send leaked or retained a Steam message");
        }
        Fake::fail_allocation = true;
        const auto sent = Fake::sent;
        check(api.transmit(nullptr, 42, bytes, 0, TrafficLane::gameplay) == k_EResultLimitExceeded && Fake::sent == sent,
              "Allocation failure still attempted a send");
        SteamNetConnectionRealTimeStatus_t total{};
        total.m_eState = k_ESteamNetworkingConnectionState_Connected;
        total.m_usecQueueTime = 500000;
        total.m_cbPendingReliable = 300000;
        std::array<SteamNetConnectionRealTimeLaneStatus_t, 4> queues{};
        queues[0].m_usecQueueTime = 1000;
        queues[2].m_usecQueueTime = 500000;
        queues[2].m_cbPendingReliable = 300000;
        check(!lane_congested(total, queues, TrafficLane::gameplay) &&
              lane_congested(total, queues, TrafficLane::cosmetics) &&
              lane_congested(total, {}, TrafficLane::gameplay), "Cosmetic backlog suppresses prioritized gameplay or breaks fallback");
        queues[0].m_usecQueueTime = 76000;
        check(lane_congested(total, queues, TrafficLane::gameplay), "Stale gameplay queue was allowed to grow");
        total.m_eState = k_ESteamNetworkingConnectionState_Connecting;
        total.m_usecQueueTime = queues[0].m_usecQueueTime = std::numeric_limits<SteamNetworkingMicroseconds>::max();
        check(lane_queue_time(total, {}, TrafficLane::gameplay) == 0 &&
              !lane_congested(total, queues, TrafficLane::gameplay), "Connecting peer triggers upload backoff");
        total.m_eState = k_ESteamNetworkingConnectionState_Connected;
        check(lane_queue_time(total, queues, TrafficLane::gameplay) == 0 &&
              !lane_congested(total, queues, TrafficLane::gameplay), "Unknown queue estimate triggers backoff");
        queues[0].m_cbPendingUnreliable = 300000;
        check(lane_congested(total, queues, TrafficLane::gameplay), "Unknown estimate bypasses pending-byte limit");
        std::cout << "Steam lane adapter: configuration/fallback, message ownership, errors, payloads and per-lane congestion passed.\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
