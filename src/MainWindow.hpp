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
#include "ScanWorker.hpp"
#include <QMainWindow>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QMap>
#include <QDateTime>
#include <QSettings>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStart();
    void onStop();
    void onStepStarted(au::QuantityD<au::Hertz> cf, int step, int total);
    void onSignalsFound(au::QuantityD<au::Hertz> cf, QVector<Signal> sigs);
    void onScanError(QString msg);
    void onAgeTick();   // 1-second timer: refresh "Last Seen", drop stale entries

private:
    // ── Controls ──────────────────────────────────────────────────────────────
    QLineEdit*      le_broker_;
    QDoubleSpinBox* sb_start_;
    QDoubleSpinBox* sb_end_;
    QDoubleSpinBox* sb_step_;
    QDoubleSpinBox* sb_dwell_;
    QDoubleSpinBox* sb_thresh_;
    QPushButton*    btn_start_;
    QPushButton*    btn_stop_;

    // ── Signal table ──────────────────────────────────────────────────────────
    QTableWidget*   table_;
    QLabel*         status_lbl_;
    QTimer*         age_timer_;

    // ── Signal registry ───────────────────────────────────────────────────────
    struct Entry {
        Signal    sig;
        QDateTime first_seen;
        QDateTime last_seen;
        int       hits = 0;
    };
    // Key = round(freq_mhz * 100)  → 10 kHz buckets
    QMap<int, Entry> registry_;

    // ── Worker ────────────────────────────────────────────────────────────────
    ScanWorker* worker_ = nullptr;

    void buildUi();
    void saveSettings();
    void loadSettings();
    void rebuildTable();
    // freq in Hz → 10 kHz bucket key
    int  freqKey(au::QuantityD<au::Hertz> freq) const {
        return (int)std::round(freq.in(au::mega(au::hertz)) * 100);
    }
    static QString ageSuffix(const QDateTime& dt);
};

/*
========================================================================
End of file — OpenRFStack
Subject to Personal Use License
https://github.com/OpenRFStack
========================================================================
*/
