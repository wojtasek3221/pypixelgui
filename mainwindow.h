#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QStringList>
#include <QByteArray>
#include <QPair>
#include <QList>
#include <QTimer>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class WatchfaceEngine;
class AudioLoopbackCapture;
class SpectrumAnalyzer;
class SpectrumVisualizerEngine;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    QTimer *m_clockTimer = nullptr;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_pushButton_2_clicked();

    void on_pushButton_clicked();

    void on_stackedWidget_currentChanged(int arg1);

    void onPipReadyReadStandardOutput();

    void onPipReadyReadStandardError();

    void onPipFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void onPipErrorOccurred(QProcess::ProcessError error);

    void on_pushButton_3_clicked();

    // Persistent BLE bridge process (keeps one connection open,
    // avoids the pairing animation replaying on every command)
    void onBridgeReadyReadStandardOutput();
    void onBridgeReadyReadStandardError();
    void onBridgeErrorOccurred(QProcess::ProcessError error);
    void onBridgeFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void on_pushButton_6_clicked();

    void on_pushButton_15_clicked();

    void on_pushButton_16_clicked();

    void on_pushButton_4_clicked();

    void on_pushButton_7_clicked();

    void on_pushButton_17_clicked();

    void on_pushButton_19_clicked();

    void on_pushButton_18_clicked();

    void on_listWidget_currentRowChanged(int currentRow);

    void on_pushButton_20_clicked();

    void on_pushButton_8_clicked();

    //void onPlayerctlReadyRead();

    void on_pushButton_21_clicked();

    void on_pushButton_24_clicked();

    void on_pushButton_22_clicked();

    void on_pushButton_9_clicked();

    void on_pushButton_10_clicked();

    void on_pushButton_11_clicked();

    void on_pushButton_5_clicked();

    void on_pushButton_14_clicked();

    void on_pushButton_26_clicked();

    void on_pushButton_25_clicked();

    void on_pushButton_28_clicked();

    void on_pushButton_29_clicked();

    void on_pushButton_30_clicked();

    void on_pushButton_12_clicked();

    // Music visualizer pipeline
    void onLoopbackSamplesReady(QVector<float> samples, int sampleRate);
    void onLoopbackErrorOccurred(QString message);
    void onSpectrumBandsReady(QVector<int> levels);
    void onRhythmSendTimerTimeout();


signals:
    // void nowPlayingTitleChanged(const QString &title);

private:
    Ui::MainWindow *ui;
    QProcess *pipProcess;

    // QProcess *playerctlProcess = nullptr;
    // QByteArray playerctlBuffer;
    //void startTitleMonitor();
    void installPipPackage(const QString &packageName);
    bool createVirtualEnv();
    QString venvPythonPath() const;

    // Bridge (persistent-connection) support
    QProcess *bridgeProcess = nullptr;
    QString bridgeAddress;
    bool bridgeReady = false;
    QByteArray bridgeBuffer;
    QList<QPair<QString, QStringList>> pendingBridgeCommands;

    QString bridgeScriptPath() const;
    void installBridgeScript();
    void startBridge(const QString &address);
    void stopBridge();
    void sendPypixelCommand(const QString &command, const QStringList &params);
    void flushPendingBridgeCommands();

    // Real LED matrix size, queried from the device via get_device_info
    // once the bridge connects (see onBridgeReadyReadStandardOutput).
    // 32x32 here is only a fallback for as long as that reply hasn't come
    // back yet - it is NOT assumed to be the device's actual size.
    int m_deviceWidth = 32;
    int m_deviceHeight = 32;
    void applyDeviceMatrixSize(); // pushes m_deviceWidth/Height into whichever engines exist

    // Watchface engine: owned here (not a local in the slot) so it stays
    // alive for as long as the QTimer keeps ticking.
    WatchfaceEngine *m_watchfaceEngine = nullptr;
    QTimer *m_watchfaceTimer = nullptr;
    bool copyDirRecursively(const QString &srcPath, const QString &dstPath);

    // Music visualizer: mic/loopback -> FFT -> set_rhythm_mode.
    // Capture and FFT run continuously and can produce updates far faster
    // than the BLE link should be sent commands, so the latest band levels
    // are cached and pushed out on a timer instead of on every bandsReady.
    AudioLoopbackCapture *m_audioCapture = nullptr;
    SpectrumAnalyzer *m_spectrumAnalyzer = nullptr;
    SpectrumVisualizerEngine *m_visualizerEngine = nullptr;
    QTimer *m_rhythmSendTimer = nullptr;
    QVector<int> m_latestBandLevels;
    bool m_visualizerActive = false;
    // send_image_hex is an ACKNOWLEDGED command on the device (unlike
    // set_pixel), so each call has to complete a full BLE write+ack round
    // trip. If we render a new frame every bandsReady tick regardless of
    // whether the previous one has been acked, they queue up faster than
    // the bridge's single-threaded command loop can drain them, and the
    // display ends up perpetually catching up on stale frames instead of
    // showing "now". This flag makes sure only one is ever in flight -
    // render() is only called when it's clear, so the engine's own
    // frame-diff always tracks what was actually transmitted.
    bool m_frameSendPending = false;
    qint64 m_rhythmSendTimestampMs = 0; // for measuring real ack round-trip time
    void trySendVisualizerFrame();
    void startVisualizer();
    void stopVisualizer();
};
#endif // MAINWINDOW_H