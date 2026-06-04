/*
========================================================================
Project: OpenRFStack
Author:  Brendan Michaud
Year:    2026
Part of OpenRFStack (https://github.com/OpenRFStack)

Licensed under the Personal Use License.
Do not use for commercial, organizational, or military purposes.
Contact author for permission: https://github.com/OpenRFStack
========================================================================
*/
#pragma once
#include "Spectrum.hpp"
#include "AmqpSession.hpp"
#include "au/units/hertz.hh"
#include "au/units/seconds.hh"
#include "au/prefix.hh"
#include <QThread>
#include <QVector>
#include <atomic>
#include <cstdint>

// Q_DECLARE_METATYPE so Signal can cross thread boundaries via queued signals
Q_DECLARE_METATYPE(Signal)
Q_DECLARE_METATYPE(QVector<Signal>)

struct ScanConfig {
    au::QuantityD<au::Hertz>   start     = au::mega(au::hertz)(80.0);
    au::QuantityD<au::Hertz>   end       = au::mega(au::hertz)(200.0);
    au::QuantityD<au::Hertz>   step      = au::mega(au::hertz)(20.0);   // BW per step
    au::QuantityD<au::Seconds> dwell     = au::milli(au::seconds)(2000.0);
    double                     threshold_db = 10.0;
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
    void stepStarted(au::QuantityD<au::Hertz> cf, int step, int total);
    void signalsFound(au::QuantityD<au::Hertz> cf, QVector<Signal> sigs);
    void scanError(QString msg);

protected:
    void run() override;

private:
    ScanConfig        cfg_;
    std::atomic<bool> stop_{false};
    AmqpSession       amqp_;
    Spectrum          spectrum_;

    // Returns interleaved CF32 samples collected for dwell on the given port.
    std::vector<float> collectIQ(int port);

    std::string makeUuid();
    int64_t     nowMs();
};
