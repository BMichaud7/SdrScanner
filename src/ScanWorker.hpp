#pragma once
#include "Spectrum.hpp"
#include "AmqpSession.hpp"
#include <QThread>
#include <QVector>
#include <atomic>
#include <cstdint>

// Q_DECLARE_METATYPE so Signal can cross thread boundaries via queued signals
Q_DECLARE_METATYPE(Signal)
Q_DECLARE_METATYPE(QVector<Signal>)

struct ScanConfig {
    double start_mhz   = 80.0;
    double end_mhz     = 200.0;
    double step_mhz    = 20.0;   // BW per step
    double dwell_ms    = 2000.0;
    double threshold_db= 10.0;
    // AMQP broker
    std::string broker   = "amqp://localhost:5672";
    std::string user     = "sdr_ctrl";
    std::string password = "sdr_hw_test";
    std::string dest_ip  = "127.0.0.1";
};

class ScanWorker : public QThread {
    Q_OBJECT
public:
    explicit ScanWorker(const ScanConfig& cfg, QObject* parent = nullptr);
    void halt();   // thread-safe stop

signals:
    void stepStarted(double cf_mhz, int step, int total);
    void signalsFound(double cf_mhz, QVector<Signal> sigs);
    void scanError(QString msg);

protected:
    void run() override;

private:
    ScanConfig        cfg_;
    std::atomic<bool> stop_{false};
    AmqpSession       amqp_;
    Spectrum          spectrum_;

    // Returns interleaved CF32 samples collected for dwell_ms on the given port.
    std::vector<float> collectIQ(int port);

    std::string makeUuid();
    int64_t     nowMs();
};
