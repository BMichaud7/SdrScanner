#include "ScanWorker.hpp"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;
using namespace std::chrono;

// IQ packet header (32 bytes, little-endian)
struct __attribute__((packed)) IqHdr {
    uint32_t magic;          // 0x49515030
    uint32_t seq_num;
    uint64_t timestamp_ns;
    uint64_t center_freq_hz;
    uint32_t sample_rate_sps;
    uint16_t n_samples;
    uint8_t  format;
    uint8_t  flags;
};
static_assert(sizeof(IqHdr) == 32);
static constexpr uint32_t IQ_MAGIC = 0x49515030;

// ── Helpers ───────────────────────────────────────────────────────────────────

std::string ScanWorker::makeUuid() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    auto a = dist(rng), b = dist(rng);
    std::ostringstream s;
    s << std::hex << std::setfill('0')
      << std::setw(8)  << (a >> 32)        << '-'
      << std::setw(4)  << ((a >> 16) & 0xffff) << '-'
      << std::setw(4)  << (a & 0xffff)     << '-'
      << std::setw(4)  << (b >> 48)        << '-'
      << std::setw(12) << (b & 0xffffffffffff);
    return s.str();
}

int64_t ScanWorker::nowMs() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ── Constructor ───────────────────────────────────────────────────────────────

ScanWorker::ScanWorker(const ScanConfig& cfg, QObject* parent)
    : QThread(parent), cfg_(cfg) {}

void ScanWorker::halt() { stop_.store(true); }

// ── IQ collection ─────────────────────────────────────────────────────────────

std::vector<float> ScanWorker::collectIQ(int port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return {};

    // 8 MB receive buffer
    int rcvbuf = 8 * 1024 * 1024;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // 400 ms receive timeout so we can check stop_ regularly
    struct timeval tv { 0, 400000 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { ::close(fd); return {}; }

    std::vector<float> samples;
    auto deadline = steady_clock::now() + milliseconds((int64_t)cfg_.dwell_ms);
    uint8_t buf[65536];

    while (!stop_.load() && steady_clock::now() < deadline) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < (ssize_t)sizeof(IqHdr)) continue;
        IqHdr hdr;
        std::memcpy(&hdr, buf, sizeof(hdr));
        if (hdr.magic != IQ_MAGIC) continue;
        int n_bytes = hdr.n_samples * 8;   // CF32: 4B I + 4B Q
        if (n < (ssize_t)(sizeof(IqHdr) + n_bytes)) continue;
        auto* iq = reinterpret_cast<float*>(buf + sizeof(IqHdr));
        for (int i = 0; i < hdr.n_samples * 2; ++i)
            samples.push_back(iq[i]);
    }
    ::close(fd);
    return samples;
}

// ── Scan loop ─────────────────────────────────────────────────────────────────

void ScanWorker::run() {
    amqp_.start(cfg_.broker, cfg_.user, cfg_.password);

    // Compute step centres
    double half = cfg_.step_mhz / 2.0;
    std::vector<double> centres;
    for (double cf = cfg_.start_mhz + half; cf < cfg_.end_mhz + 1e-6; cf += cfg_.step_mhz)
        centres.push_back(cf);
    int total = (int)centres.size();

    while (!stop_.load()) {
        for (int idx = 0; idx < total && !stop_.load(); ++idx) {
            double cf = centres[idx];
            emit stepStarted(cf, idx + 1, total);

            std::string rid = makeUuid();
            json req = {
                {"msg_type",       "TASK_REQUEST"},
                {"schema_version", "2.0"},
                {"request_id",     rid},
                {"timestamp_ms",   nowMs()},
                {"task_type",      "WIDEBAND"},
                {"rank",           2},
                {"schedule",       {{"mode", "IMMEDIATE"},
                                    {"duration_ms", (int)(cfg_.dwell_ms + 1000)}}},
                {"rf",             {{"center_freq_hz",  cf * 1e6},
                                    {"bandwidth_hz",     cfg_.step_mhz * 1e6},
                                    {"sample_rate_sps",  cfg_.step_mhz * 1e6},
                                    {"rx_count",         1}}},
                {"streaming",      {{"dest_ip", cfg_.dest_ip}}},
                {"wideband",       {{"record_raw_iq", true}, {"fft_size", 2048}}}
            };

            json resp = amqp_.rpc(req, 20000, &stop_);
            if (stop_.load()) break;
            if (resp.is_null()) {
                // AMQP disconnected or timed out — abort the whole scan
                emit scanError("AMQP timeout/disconnect — stopping scan");
                return;
            }
            if (resp.value("status", "") != "ACCEPTED") {
                emit scanError(QString("Step %1 MHz rejected: %2")
                    .arg(cf).arg(QString::fromStdString(
                        resp.value("reject_reason", "?"))));
                continue;
            }

            std::string task_id = resp.value("task_id", "");
            int udp_port = 0;
            auto& streams = resp["streams"];
            if (streams.is_array() && !streams.empty())
                udp_port = streams[0].value("udp_port", 0);

            if (!udp_port) { emit scanError("No UDP port in response"); continue; }

            auto iq = collectIQ(udp_port);

            // Stop the task
            amqp_.fire({
                {"msg_type",    "TASK_STOP"},
                {"request_id",  makeUuid()},
                {"task_id",     task_id},
                {"timestamp_ms", nowMs()},
                {"reason",      "scan step done"}
            });

            auto sigs = spectrum_.analyse(iq, cf * 1e6,
                                          cfg_.step_mhz * 1e6,
                                          cfg_.threshold_db);
            emit signalsFound(cf, QVector<Signal>(sigs.begin(), sigs.end()));

            // Brief inter-step pause for hw cleanup
            if (!stop_.load()) msleep(300);
        }
    }

    amqp_.stop();
}
