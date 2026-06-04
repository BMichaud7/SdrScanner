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
#include "MainWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QStatusBar>
#include <QMessageBox>
#include <QColor>
#include <cmath>

enum Col { C_FREQ=0, C_BW, C_POWER, C_TYPE, C_SEEN, C_HITS, C_COUNT };

static const char* HDR[] = {"Freq (MHz)", "BW (kHz)", "+dBc", "Type", "Last Seen", "Hits"};

// ── Construction ──────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("SDR Band Scanner");
    resize(960, 600);
    buildUi();
    loadSettings();

    age_timer_ = new QTimer(this);
    connect(age_timer_, &QTimer::timeout, this, &MainWindow::onAgeTick);
    age_timer_->start(1000);
}

MainWindow::~MainWindow() { onStop(); }

void MainWindow::saveSettings() {
    QSettings s;
    s.setValue("broker",    le_broker_->text());
    s.setValue("start_mhz", sb_start_->value());
    s.setValue("end_mhz",   sb_end_->value());
    s.setValue("step_mhz",  sb_step_->value());
    s.setValue("dwell_ms",  sb_dwell_->value());
    s.setValue("thresh_db", sb_thresh_->value());
}

void MainWindow::loadSettings() {
    QSettings s;
    le_broker_->setText(s.value("broker",    "amqp://localhost:5672").toString());
    sb_start_->setValue( s.value("start_mhz", 80.0).toDouble());
    sb_end_->setValue(   s.value("end_mhz",  200.0).toDouble());
    sb_step_->setValue(  s.value("step_mhz",  20.0).toDouble());
    sb_dwell_->setValue( s.value("dwell_ms", 2000.0).toDouble());
    sb_thresh_->setValue(s.value("thresh_db",  10.0).toDouble());
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* vbox = new QVBoxLayout(central);

    // ── Controls group ────────────────────────────────────────────────────────
    auto* grp   = new QGroupBox("Scan Parameters", central);
    auto* gvbox = new QVBoxLayout(grp);

    // Row 1: broker URL
    auto* row1  = new QHBoxLayout;
    row1->addWidget(new QLabel("Broker:"));
    le_broker_ = new QLineEdit("amqp://localhost:5672");
    le_broker_->setPlaceholderText("amqp://host:5672");
    le_broker_->setMinimumWidth(220);
    row1->addWidget(le_broker_, 1);
    gvbox->addLayout(row1);

    // Row 2: freq params + buttons
    auto* hbox  = new QHBoxLayout;

    auto makeHz = [&](const char* lbl, double val, double lo, double hi, double step) {
        hbox->addWidget(new QLabel(lbl));
        auto* sb = new QDoubleSpinBox;
        sb->setRange(lo, hi); sb->setValue(val); sb->setSingleStep(step);
        sb->setSuffix(" MHz"); sb->setDecimals(1);
        hbox->addWidget(sb);
        return sb;
    };
    sb_start_  = makeHz("Start:",    80.0,  50.0, 6000.0, 1.0);
    sb_end_    = makeHz("End:",     200.0,  50.0, 6000.0, 1.0);
    sb_step_   = makeHz("BW/step:", 20.0,   1.0,   56.0, 1.0);

    hbox->addWidget(new QLabel("Dwell:"));
    sb_dwell_ = new QDoubleSpinBox;
    sb_dwell_->setRange(500, 10000); sb_dwell_->setValue(2000);
    sb_dwell_->setSuffix(" ms"); sb_dwell_->setDecimals(0);
    hbox->addWidget(sb_dwell_);

    hbox->addWidget(new QLabel("Threshold:"));
    sb_thresh_ = new QDoubleSpinBox;
    sb_thresh_->setRange(3.0, 30.0); sb_thresh_->setValue(10.0);
    sb_thresh_->setSuffix(" dB"); sb_thresh_->setDecimals(1);
    hbox->addWidget(sb_thresh_);
    hbox->addStretch();

    btn_start_ = new QPushButton("▶  Start");
    btn_stop_  = new QPushButton("■  Stop");
    btn_stop_->setEnabled(false);
    hbox->addWidget(btn_start_);
    hbox->addWidget(btn_stop_);
    gvbox->addLayout(hbox);

    connect(btn_start_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(btn_stop_,  &QPushButton::clicked, this, &MainWindow::onStop);

    vbox->addWidget(grp);

    // ── Signal table ──────────────────────────────────────────────────────────
    table_ = new QTableWidget(0, C_COUNT, central);
    QStringList headers;
    for (auto* h : HDR) headers << h;
    table_->setHorizontalHeaderLabels(headers);
    table_->horizontalHeader()->setSectionResizeMode(C_TYPE, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(C_SEEN, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->setAlternatingRowColors(true);
    vbox->addWidget(table_);

    // ── Status bar ────────────────────────────────────────────────────────────
    status_lbl_ = new QLabel("Ready");
    statusBar()->addWidget(status_lbl_);
}

// ── Worker lifecycle ──────────────────────────────────────────────────────────

void MainWindow::onStart() {
    if (worker_) return;

    saveSettings();

    ScanConfig cfg;
    cfg.broker       = le_broker_->text().toStdString();
    cfg.start        = au::mega(au::hertz)(sb_start_->value());
    cfg.end          = au::mega(au::hertz)(sb_end_->value());
    cfg.step         = au::mega(au::hertz)(sb_step_->value());
    cfg.dwell        = au::milli(au::seconds)(sb_dwell_->value());
    cfg.threshold_db = sb_thresh_->value();

    worker_ = new ScanWorker(cfg, this);
    connect(worker_, &ScanWorker::stepStarted,  this, &MainWindow::onStepStarted);
    connect(worker_, &ScanWorker::signalsFound,  this, &MainWindow::onSignalsFound);
    connect(worker_, &ScanWorker::scanError,     this, &MainWindow::onScanError);
    connect(worker_, &ScanWorker::finished,      this, [this]{
        worker_->deleteLater();
        worker_ = nullptr;
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
        status_lbl_->setText("Stopped");
    });

    btn_start_->setEnabled(false);
    btn_stop_->setEnabled(true);
    worker_->start();
}

void MainWindow::onStop() {
    if (!worker_) return;
    worker_->halt();
    worker_->wait(8000);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onStepStarted(au::QuantityD<au::Hertz> cf, int step, int total) {
    double cf_mhz = cf.in(au::mega(au::hertz));
    status_lbl_->setText(QString("Scanning %1 MHz  (step %2/%3)")
                         .arg(cf_mhz, 0, 'f', 1).arg(step).arg(total));
}

void MainWindow::onSignalsFound(au::QuantityD<au::Hertz> /*cf*/, QVector<Signal> sigs) {
    auto now = QDateTime::currentDateTime();
    for (const auto& s : sigs) {
        int key = freqKey(s.freq);
        // Find nearest existing entry within ±5 buckets (50 kHz)
        int best = -1; int bestDist = 6;
        for (auto it = registry_.lowerBound(key-5); it != registry_.end() && it.key() <= key+5; ++it) {
            int d = std::abs(it.key() - key);
            if (d < bestDist) { bestDist = d; best = it.key(); }
        }
        if (best >= 0) {
            auto& e   = registry_[best];
            e.sig      = s;
            e.last_seen= now;
            e.hits++;
        } else {
            registry_[key] = { s, now, now, 1 };
        }
    }
    rebuildTable();
}

void MainWindow::onScanError(QString msg) {
    status_lbl_->setText("Error: " + msg);
}

// ── Table management ──────────────────────────────────────────────────────────

QString MainWindow::ageSuffix(const QDateTime& dt) {
    int secs = (int)dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)  return QString("%1s ago").arg(secs);
    if (secs < 3600) return QString("%1m ago").arg(secs/60);
    return QString("%1h ago").arg(secs/3600);
}

void MainWindow::rebuildTable() {
    table_->setSortingEnabled(false);
    table_->setRowCount((int)registry_.size());

    auto setCell = [&](int row, int col, const QString& text, Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter) {
        auto* item = table_->item(row, col);
        if (!item) { item = new QTableWidgetItem; table_->setItem(row, col, item); }
        item->setText(text);
        item->setTextAlignment(align);
    };

    int row = 0;
    auto now = QDateTime::currentDateTime();
    for (auto& e : registry_) {
        int secs = (int)e.last_seen.secsTo(now);

        // Row background: green if recent, grey if stale
        QColor bg;
        if      (secs <  10) bg = QColor(180, 240, 180);  // bright green
        else if (secs <  60) bg = QColor(220, 245, 220);  // pale green
        else if (secs < 180) bg = QColor(240, 240, 240);  // grey
        else                 bg = QColor(210, 210, 210);  // darker grey (about to expire)

        for (int c = 0; c < C_COUNT; ++c) {
            auto* item = table_->item(row, c);
            if (!item) { item = new QTableWidgetItem; table_->setItem(row, c, item); }
            item->setBackground(bg);
        }

        double freq_mhz = e.sig.freq.in(au::mega(au::hertz));
        double bw_khz   = e.sig.bw.in(au::kilo(au::hertz));
        setCell(row, C_FREQ,  QString::number(freq_mhz, 'f', 3));
        setCell(row, C_BW,    QString::number(bw_khz,   'f', 1));
        setCell(row, C_POWER, QString("+%1").arg(e.sig.power_dbc, 0, 'f', 1));
        setCell(row, C_TYPE,  QString::fromStdString(e.sig.type), Qt::AlignLeft | Qt::AlignVCenter);
        setCell(row, C_SEEN,  ageSuffix(e.last_seen));
        setCell(row, C_HITS,  QString::number(e.hits));
        ++row;
    }
    table_->setSortingEnabled(true);
}

void MainWindow::onAgeTick() {
    auto now = QDateTime::currentDateTime();
    bool changed = false;
    for (auto it = registry_.begin(); it != registry_.end(); ) {
        if (it->last_seen.secsTo(now) > 300) {  // 5 min expiry
            it = registry_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed || !registry_.isEmpty())
        rebuildTable();
}
