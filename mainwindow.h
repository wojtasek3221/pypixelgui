#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QStringList>
#include <QByteArray>
#include <QPair>
#include <QList>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class WatchfaceEngine;

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

    void on_pushButton_9_clicked();

    void on_pushButton_21_clicked();

    void on_pushButton_24_clicked();

    void on_pushButton_22_clicked();

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

    // Watchface engine: owned here (not a local in the slot) so it stays
    // alive for as long as the QTimer keeps ticking.
    WatchfaceEngine *m_watchfaceEngine = nullptr;
    QTimer *m_watchfaceTimer = nullptr;
    bool copyDirRecursively(const QString &srcPath, const QString &dstPath);
};
#endif // MAINWINDOW_H