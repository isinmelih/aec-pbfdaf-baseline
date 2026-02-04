#include "MainWindow.h"
#include <cmath>

#ifdef HAVE_QT

#include <QtCharts/QChart>
#include <QGroupBox>
#include <QFormLayout>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), chartSampleCount(0) {
    setupUi();
    setupCharts();
    
    statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
    statsTimer->start(33); // ~30fps
}

MainWindow::~MainWindow() {
    if (runner.isRunning()) {
        runner.stop();
    }
}

void MainWindow::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

    // 0. Backend Info Label
    // "Backend: PBFDAF AEC Baseline"
    lblBackendInfo = new QLabel("Backend: PBFDAF AEC Baseline\n(Real-time, Gradient Constraint, Overlap-Save)");
    lblBackendInfo->setStyleSheet("font-weight: bold; color: #004d40; padding: 8px; background: #b2dfdb; border: 1px solid #00695c; border-radius: 4px;");
    lblBackendInfo->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(lblBackendInfo);
    
    // Top Row: Panels
    QHBoxLayout* topLayout = new QHBoxLayout();
    
    // 1. Signal/Status Panel
    QGroupBox* grpStatus = new QGroupBox("AEC Performance Metrics");
    QFormLayout* layoutStatus = new QFormLayout(grpStatus);
    
    // Prominent ERLE Display
    lblErle = new QLabel("0.0 dB");
    lblErle->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    lblAvgErle = new QLabel("0.0 dB");
    lblMaxErle = new QLabel("0.0 dB");
    lblConvergedTime = new QLabel("0 ms");
    
    lblLagMs = new QLabel("0.0 ms");
    lblDtd = new QLabel("No");
    lblFreeze = new QLabel("No");
    lblMu = new QLabel("0.00");
    lblStatus = new QLabel("Stopped");
    
    // Metrics
    lblInputEnergy = new QLabel("- inf dB");
    lblErrorEnergy = new QLabel("- inf dB");
    lblEchoReduction = new QLabel("0.0 dB");
    
    layoutStatus->addRow("Instant ERLE:", lblErle);
    layoutStatus->addRow("Avg ERLE (Valid):", lblAvgErle);
    layoutStatus->addRow("Max ERLE:", lblMaxErle);
    layoutStatus->addRow("Convergence Time:", lblConvergedTime);
    layoutStatus->addRow(new QLabel("----------------"));
    layoutStatus->addRow("Status:", lblStatus);
    layoutStatus->addRow("Est. Lag:", lblLagMs);
    layoutStatus->addRow("DTD Active:", lblDtd);
    layoutStatus->addRow("Freeze:", lblFreeze);
    layoutStatus->addRow("Step Size (mu):", lblMu);
    
    layoutStatus->addRow(new QLabel("--- Metrics ---"));
    layoutStatus->addRow("Input Energy:", lblInputEnergy);
    layoutStatus->addRow("Error Energy:", lblErrorEnergy);
    layoutStatus->addRow("Echo Reduction:", lblEchoReduction);
    
    // 2. Control Panel
    QGroupBox* grpControl = new QGroupBox("Control Panel");
    QVBoxLayout* layoutControl = new QVBoxLayout(grpControl);

    // Core Params Group
    QGroupBox* grpCore = new QGroupBox("Core Parameters");
    QFormLayout* layoutCore = new QFormLayout(grpCore);

    editFilterLen = new QLineEdit("512");
    editFilterLen->setToolTip("Maximum echo path length in samples (longer = better cancellation but higher CPU)");
    
    editMu = new QLineEdit("0.03");
    editMu->setToolTip("Step size – Higher = faster convergence but lower stability");

    layoutCore->addRow("Filter Len:", editFilterLen);
    layoutCore->addRow("Step Size (Mu):", editMu);
    layoutControl->addWidget(grpCore);

    // Advanced Params Group (Toggleable)
    chkAdvanced = new QCheckBox("Advanced Mode");
    layoutControl->addWidget(chkAdvanced);

    advancedContainer = new QWidget();
    QFormLayout* layoutAdvanced = new QFormLayout(advancedContainer);
    
    editEpsilon = new QLineEdit("1e-6");
    editEpsilon->setToolTip("Regularization – Prevents divergence in low SNR conditions");
    
    layoutAdvanced->addRow("Epsilon:", editEpsilon);

    // Group 2: Stability & DTD
    QGroupBox* grpDtd = new QGroupBox("Stability & DTD");
    QFormLayout* layoutDtd = new QFormLayout(grpDtd);
    
    editDtdAlpha = new QLineEdit("2.0");
    editDtdAlpha->setToolTip("DTD Alpha: Adaptation speed");
    
    editDtdBeta = new QLineEdit("1.5");
    editDtdBeta->setToolTip("DTD Beta: Detection sensitivity");
    
    editFreezeBlocks = new QLineEdit("5");
    editFreezeBlocks->setToolTip("Double-talk detection parameters (alpha: adaptation speed, beta: detection sensitivity)");
    
    layoutDtd->addRow("DTD Alpha:", editDtdAlpha);
    layoutDtd->addRow("DTD Beta:", editDtdBeta);
    layoutDtd->addRow("Freeze Blocks:", editFreezeBlocks);
    
    layoutAdvanced->addWidget(grpDtd);
    
    advancedContainer->setVisible(false); // Hidden by default
    layoutControl->addWidget(advancedContainer);
    
    connect(chkAdvanced, &QCheckBox::toggled, [this](bool checked){
        advancedContainer->setVisible(checked);
    });
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnApply = new QPushButton("Apply");
    btnApply->setToolTip("Apply current parameters safely.");
    
    btnReset = new QPushButton("Reset to Safe Defaults");
    btnReset->setToolTip("Reset all parameters to safe, tested defaults.");

    btnStart = new QPushButton("Start Audio");
    btnStop = new QPushButton("Stop Audio");
    btnStop->setEnabled(false);
    
    connect(btnApply, &QPushButton::clicked, this, &MainWindow::onApplySettings);
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::onResetSettings);
    connect(btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    
    btnLayout->addWidget(btnApply);
    btnLayout->addWidget(btnReset);
    
    layoutControl->addLayout(btnLayout);
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->addWidget(btnStart);
    actionLayout->addWidget(btnStop);
    layoutControl->addLayout(actionLayout);
    
    // 3. Debug Panel
    QGroupBox* grpDebug = new QGroupBox("Debug Info");
    QFormLayout* layoutDebug = new QFormLayout(grpDebug);
    lblDelayUpdates = new QLabel("0");
    lblBlockSize = new QLabel("1024");
    
    layoutDebug->addRow("Delay Updates:", lblDelayUpdates);
    layoutDebug->addRow("Block Size:", lblBlockSize);
    
    topLayout->addWidget(grpStatus);
    topLayout->addWidget(grpControl);
    topLayout->addWidget(grpDebug);
    
    mainLayout->addLayout(topLayout);
    
    // Bottom: Energy Chart
    QGroupBox* grpChart = new QGroupBox("Energy Monitor");
    QVBoxLayout* layoutChart = new QVBoxLayout(grpChart);
    
    QChart* chart = new QChart();
    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    
    layoutChart->addWidget(chartView);
    mainLayout->addWidget(grpChart);
}

void MainWindow::setupCharts() {
    QChart* chart = chartView->chart();
    chart->setTitle("Mic (Blue) vs Ref (Green) vs Error (Red)");
    
    seriesMic = new QLineSeries();
    seriesMic->setName("Mic");
    seriesMic->setColor(Qt::blue);
    
    seriesRef = new QLineSeries();
    seriesRef->setName("Ref");
    seriesRef->setColor(Qt::green);
    
    seriesErr = new QLineSeries();
    seriesErr->setName("Error");
    seriesErr->setColor(Qt::red);
    
    chart->addSeries(seriesMic);
    chart->addSeries(seriesRef);
    chart->addSeries(seriesErr);
    
    axisX = new QValueAxis();
    axisX->setRange(0, 100);
    axisX->setLabelFormat("%d");
    chart->addAxis(axisX, Qt::AlignBottom);
    
    seriesMic->attachAxis(axisX);
    seriesRef->attachAxis(axisX);
    seriesErr->attachAxis(axisX);
    
    axisY = new QValueAxis();
    axisY->setRange(-1.0, 1.0);
    chart->addAxis(axisY, Qt::AlignLeft);
    
    seriesMic->attachAxis(axisY);
    seriesRef->attachAxis(axisY);
    seriesErr->attachAxis(axisY);
}

void MainWindow::updateStats() {
    if (!runner.isRunning()) {
        lblStatus->setText("Stopped");
        return;
    }
    
    lblStatus->setText("Running");
    AECProcessor* aec = runner.getProcessor();
    if (!aec) return;
    
    AECStats stats = aec->getStats();
    
    // --- ERLE Visualization Logic ---
    auto getErleStyle = [](float db, bool valid) {
        if (!valid) return "font-size: 18px; font-weight: bold; color: #757575;"; // Gray
        if (db > 30.0f) return "font-size: 18px; font-weight: bold; color: #2e7d32;"; // Green
        if (db > 10.0f) return "font-size: 18px; font-weight: bold; color: #ef6c00;"; // Orange
        return "font-size: 18px; font-weight: bold; color: #c62828;"; // Red
    };

    // Instant ERLE
    if (stats.dtd) {
        lblErle->setText("DT (Paused)");
        lblErle->setStyleSheet(getErleStyle(0, false));
    } else {
        lblErle->setText(QString::asprintf("%.1f dB", stats.erle));
        lblErle->setStyleSheet(getErleStyle(stats.erle, true));
    }
    
    // Avg ERLE
    lblAvgErle->setText(QString::asprintf("%.1f dB", stats.avgErle));
    // Color code Avg ERLE too
    QString avgStyle = "font-weight: bold; ";
    if (stats.avgErle > 30) avgStyle += "color: #2e7d32;";
    else if (stats.avgErle > 10) avgStyle += "color: #ef6c00;";
    else avgStyle += "color: #c62828;";
    lblAvgErle->setStyleSheet(avgStyle);

    // Max ERLE & Convergence
    lblMaxErle->setText(QString::asprintf("%.1f dB", stats.maxErle));
    lblConvergedTime->setText(QString::asprintf("%.0f ms", stats.convergedTimeMs));
    
    // Other Status
    lblLagMs->setText(QString::asprintf("%.1f ms", stats.currentLagMs));
    lblDtd->setText(stats.dtd ? "YES" : "No");
    if (stats.dtd) lblDtd->setStyleSheet("font-weight: bold; color: #c62828;"); // Red for DTD
    else lblDtd->setStyleSheet("color: #2e7d32;"); // Green for No DTD

    lblFreeze->setText(stats.freeze ? "YES (Frozen)" : "No");
    lblMu->setText(QString::asprintf("%.4f", stats.mu));
    lblDelayUpdates->setText(QString::number(stats.delayUpdateCount));
    
    // Metrics
    auto toDb = [](float val) {
        if (val < 1e-10f) return -100.0f;
        return 10.0f * std::log10(val);
    };
    
    float inDb = toDb(stats.micE);
    float errDb = toDb(stats.errE);
    // Simple echo reduction: Input - Error
    float redDb = inDb - errDb;
    if (redDb < 0) redDb = 0; // Should not be negative usually
    
    lblInputEnergy->setText(QString::asprintf("%.1f dB", inDb));
    lblErrorEnergy->setText(QString::asprintf("%.1f dB", errDb));
    lblEchoReduction->setText(QString::asprintf("%.1f dB", redDb));
}

// Helper for validation
bool validateParam(QLineEdit* edit, float min, float max, const QString& msg) {
    bool ok;
    float val = edit->text().toFloat(&ok);
    if (!ok || val < min || val > max) {
        edit->setStyleSheet("border: 2px solid red;");
        edit->setToolTip(msg + QString("\nValid Range: %1 - %2").arg(min).arg(max));
        return false;
    }
    edit->setStyleSheet(""); // Reset style
    return true;
}

void MainWindow::onStartClicked() {
    bool valid = true;
    valid &= validateParam(editFilterLen, 128, 2048, "Filter length out of range!");
    valid &= validateParam(editMu, 0.001f, 0.1f, "Mu > 0.1 instability risk");
    valid &= validateParam(editEpsilon, 1e-6f, 1e-2f, "Epsilon out of range!");
    valid &= validateParam(editDtdAlpha, 1.0f, 10.0f, "DTD Alpha out of range!");
    valid &= validateParam(editDtdBeta, 1.0f, 5.0f, "DTD Beta out of range!");
    valid &= validateParam(editFreezeBlocks, 0, 50, "Freeze Blocks out of range!");

    if (!valid) {
        lblStatus->setText("Invalid Parameters!");
        return;
    }

    RunnerParams p;
    p.micIndex = -1;
    p.spkIndex = -1;
    p.sampleRate = 48000;
    p.durationMs = -1;
    
    p.filterLen = editFilterLen->text().toInt();
    p.mu = editMu->text().toFloat();
    p.epsilon = editEpsilon->text().toFloat();
    p.dtdAlpha = editDtdAlpha->text().toFloat();
    p.dtdBeta = editDtdBeta->text().toFloat();
    p.freezeBlocks = editFreezeBlocks->text().toInt();

    if (runner.start(p)) {
        btnStart->setEnabled(false);
        btnStop->setEnabled(true);
        // Reset styles just in case
        editFilterLen->setStyleSheet("");
        editMu->setStyleSheet("");
    }
}

void MainWindow::onStopClicked() {
    runner.stop();
    btnStart->setEnabled(true);
    btnStop->setEnabled(false);
    lblStatus->setText("Stopped");
}

void MainWindow::onApplySettings() {
    AECProcessor* aec = runner.getProcessor();
    if (!aec) return;
    
    if (!validateParam(editMu, 0.001f, 0.1f, "Mu unstable!")) return;
    float mu = editMu->text().toFloat();
    aec->setMu(mu);
    
    if (validateParam(editDtdAlpha, 1.0f, 10.0f, "Alpha invalid") && 
        validateParam(editDtdBeta, 1.0f, 5.0f, "Beta invalid")) {
        aec->setDtdParams(editDtdAlpha->text().toFloat(), editDtdBeta->text().toFloat());
    }
    
    if (validateParam(editFreezeBlocks, 0, 50, "Freeze invalid")) {
        aec->setFreezeBlocks(editFreezeBlocks->text().toInt());
    }
}

void MainWindow::onResetSettings() {
    editFilterLen->setText("512");
    editMu->setText("0.03");
    editEpsilon->setText("1e-6");
    editDtdAlpha->setText("2.0");
    editDtdBeta->setText("1.5");
    editFreezeBlocks->setText("5");
    
    // Clear errors
    editFilterLen->setStyleSheet("");
    editMu->setStyleSheet("");
    editEpsilon->setStyleSheet("");
    editDtdAlpha->setStyleSheet("");
    editDtdBeta->setStyleSheet("");
    editFreezeBlocks->setStyleSheet("");
    
    onApplySettings();
}

#endif // HAVE_QT
