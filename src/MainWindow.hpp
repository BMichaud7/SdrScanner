#pragma once
#include "ScanWorker.hpp"
#include <QMainWindow>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <QDateTime>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStart();
    void onStop();
    void onStepStarted(double cf_mhz, int step, int total);
    void onSignalsFound(double cf_mhz, QVector<Signal> sigs);
    void onScanError(QString msg);
    void onAgeTick();   // 1-second timer: refresh "Last Seen", drop stale entries

private:
    // ── Controls ──────────────────────────────────────────────────────────────
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
    void rebuildTable();
    int  freqKey(double freq_mhz) const { return (int)std::round(freq_mhz * 100); }
    static QString ageSuffix(const QDateTime& dt);
};
