#pragma once

#ifdef HAVE_QT

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include "WasapiRunner.h"
#include "../aec_core/AECProcessor.h"

// Check Qt version or namespace
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    using namespace QtCharts;
#else
    using namespace QtCharts;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateStats();
    void onStartClicked();
    void onStopClicked();
    void onApplySettings();
    void onResetSettings();

private:
    void setupUi();
    void setupCharts();
    
    // Core components
    WasapiRunner runner;
    QTimer* statsTimer;
    
    // UI - Status Panel
    QLabel* lblBackendInfo;
    QLabel* lblErle; // Instant
    QLabel* lblAvgErle;
    QLabel* lblMaxErle;
    QLabel* lblConvergedTime;
    
    QLabel* lblLagMs;
    QLabel* lblDtd;
    QLabel* lblFreeze;
    QLabel* lblMu;
    QLabel* lblStatus;
    
    // UI - Metrics
    QLabel* lblInputEnergy;
    QLabel* lblErrorEnergy;
    QLabel* lblEchoReduction;

    // UI - Control Panel
            QCheckBox* chkAdvanced;
            QWidget* advancedContainer; // To toggle visibility
            QLineEdit* editFilterLen;
            QLineEdit* editMu;
            QLineEdit* editEpsilon;
            // DTD Params
            QLineEdit* editDtdAlpha;
            QLineEdit* editDtdBeta;
            QLineEdit* editFreezeBlocks;

            QPushButton* btnApply;
    QPushButton* btnReset;
    QPushButton* btnStart;
    QPushButton* btnStop;
    
    // UI - Debug Panel
    QLabel* lblDelayUpdates;
    QLabel* lblBlockSize;
    
    // Charts
    QChartView* chartView;
    QLineSeries* seriesMic;
    QLineSeries* seriesRef;
    QLineSeries* seriesErr;
    QValueAxis* axisX;
    QValueAxis* axisY;
    
    // Data for charts
    int chartSampleCount;
};

#endif // HAVE_QT
