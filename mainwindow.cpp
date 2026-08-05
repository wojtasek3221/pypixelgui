#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "watchfaceengine.h"
#include "audioloopback.h"
#include "spectrumanalyzer.h"
#include "spectrumvisualizerengine.h"
#include <QApplication>
#include <QProcess>
#include <QMetaEnum>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <cstdio>
#include <QTextStream>
static int page = 0;
static bool deps_ok = true;
static QString mac_add;
static bool power = true;
static bool br_active = true;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , pipProcess(nullptr)
{
    ui->setupUi(this);

    QFile file(QDir::homePath() + "/.pypixelguiconf/conf.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Could not open file:" << file.errorString();
        return;
    }

    QTextStream in(&file);
    QString content = in.readAll();   // read whole file
    file.close();

    if(content != "")
    {
        mac_add = content;
        startBridge(mac_add);
        ui->label_22->setText(mac_add);
        sendPypixelCommand("send_text", QStringList()
                                            << "CONNECTION SUCCESFULL!"
                                            << "animation=1" << "speed=50" << "color=ffffff");
        ui->stackedWidget->setCurrentIndex(5);
        ui->widget->hide();
        ui->lineEdit_6->setText(ui->label_9->text());
    }
    else
    {
        ui->stackedWidget->setCurrentIndex(0);
        ui->pushButton_2->setEnabled(false);

        ui->textEdit->insertPlainText("checking dependencies...");
        QDir().mkpath(QDir::homePath() + "/.pypixelguiconf");
        QDir().mkpath(QDir::homePath() + "/.pypixelguiconf/faces");
        QFile file(QDir::homePath() + "/.pypixelguiconf/conf.txt");
        file.open(QIODevice::WriteOnly);


        QProcess process;
        process.start("python3", QStringList() << "--version");
        if (!process.waitForStarted(1000))
        {
            ui->textEdit->insertPlainText("\n Python couldn't be found, make sure it is installed and try again");
            deps_ok = false;
        }
        else
        {
            process.waitForFinished(2000);
            ui->textEdit->insertPlainText("\n Python found, note make sure that pip and venv works");
        }

        process.start("git", QStringList() << "--version");
        if (!process.waitForStarted(1000))
        {
            ui->textEdit->insertPlainText("\n Git couldn't be found, make sure it is installed and try again");
            deps_ok = false;
        }
        else
        {
            process.waitForFinished(2000);
            ui->textEdit->insertPlainText("\n Git found");
        }

        process.start("bluetoothctl", QStringList() << "--version");
        if (!process.waitForStarted(1000))
        {
            ui->textEdit->insertPlainText("\n Bluez couldn't be found, make sure it is installed and try again");
            deps_ok = false;
        }
        else
        {
            process.waitForFinished(2000);
            ui->textEdit->insertPlainText("\n Bluez found");
        }

        if(deps_ok == true)
        {
            ui->pushButton_2->setEnabled(true);
        }
    }
}

MainWindow::~MainWindow()
{
    stopVisualizer();
    stopBridge();
    delete ui;
}

void MainWindow::on_pushButton_2_clicked()
{
    page = page + 1;
    ui->stackedWidget->setCurrentIndex(page);
    if(page == 1)
    {
        ui->pushButton->setText("Previous");
        ui->pushButton_2->setText("Next");
    }
}


void MainWindow::on_pushButton_clicked()
{
    if(page == 0)
    {
        QApplication::quit();
    }
    else
    {
        page = page - 1;
        ui->stackedWidget->setCurrentIndex(page);
        if(page == 0)
        {
            ui->pushButton->setText("Exit");
            ui->pushButton_2->setText("Agree");
        }
    }
}


void MainWindow::on_stackedWidget_currentChanged(int arg1)
{
    if(arg1 == 2)
    {
        createVirtualEnv();
        installPipPackage("pypixelcolor");
        installBridgeScript();
    }
    else if(arg1 == 3)
    {
        ui->pushButton_2->setEnabled(false);
    }
    else if(arg1 == 4)
    {
        mac_add = ui->lineEdit->text();
    }
    else if(arg1 == 5)
    {
        ui->label_9->setText(ui->lineEdit_2->text());
        ui->widget->hide();

    }
}

//
//venv
//
QString MainWindow::venvPythonPath() const
{
    const QString projectDir = QDir::homePath() + "/pypixel";
#ifdef Q_OS_WIN
    return projectDir + "/venv/Scripts/python.exe";
#else
    return projectDir + "/venv/bin/python";
#endif
}

bool MainWindow::createVirtualEnv()
{
    const QString projectDir = QDir::homePath() + "/pypixel";

    // equivalent of: mkdir ~/pypixel
    QDir dir;
    if (!dir.mkpath(projectDir)) {
        ui->outputTextEdit->append("Failed to create directory: " + projectDir);
        return false;
    }

    ui->outputTextEdit->append("Creating virtual environment...");

    // equivalent of: cd ~/pypixel && python3 -m venv venv
    QProcess venvProcess;
    venvProcess.setWorkingDirectory(projectDir);
    venvProcess.start("python3", QStringList() << "-m" << "venv" << "venv");

    if (!venvProcess.waitForStarted(2000)) {
        ui->outputTextEdit->append("Could not start python3 to create the virtual environment.");
        return false;
    }

    // venv creation can take a little while, give it more time than a version check
    if (!venvProcess.waitForFinished(30000)) {
        ui->outputTextEdit->append("Timed out while creating the virtual environment.");
        return false;
    }

    if (venvProcess.exitStatus() != QProcess::NormalExit || venvProcess.exitCode() != 0) {
        ui->outputTextEdit->append("Failed to create virtual environment:\n"
                                   + QString(venvProcess.readAllStandardError()));
        return false;
    }

    // There's no direct equivalent of "source venv/bin/activate" for QProcess,
    // since QProcess doesn't run through a shell and activation just adjusts
    // PATH/env vars for that shell session. Instead, we skip activation entirely
    // and call the venv's own python binary directly - venvPythonPath() below -
    // which has the same effect.
    ui->outputTextEdit->append("Virtual environment created at " + projectDir + "/venv");
    return true;
}

void MainWindow::installPipPackage(const QString &packageName)
{
    // Guard against starting a second install while one is running
    if (pipProcess != nullptr) {
        ui->outputTextEdit->append("An install is already in progress.");
        return;
    }

    // Create the venv on first use, if it doesn't already exist
    if (!QFile::exists(venvPythonPath())) {
        if (!createVirtualEnv()) {
            ui->outputTextEdit->append("Cannot install packages without a virtual environment.");
            return;
        }
    }

    pipProcess = new QProcess(this);

    connect(pipProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onPipReadyReadStandardOutput);
    connect(pipProcess, &QProcess::readyReadStandardError,
            this, &MainWindow::onPipReadyReadStandardError);
    connect(pipProcess, &QProcess::errorOccurred,
            this, &MainWindow::onPipErrorOccurred);
    connect(pipProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onPipFinished);

    ui->outputTextEdit->append("Installing " + packageName + "...");

    // -u = unbuffered output, so you get live progress instead of it
    // arriving in one big chunk at the end.
    // Use the venv's own python instead of the system python3 - this is what
    // "activating" the venv would otherwise achieve.
    pipProcess->start(venvPythonPath(), QStringList()
                                            << "-u" << "-m" << "pip" << "install" << packageName);
}

void MainWindow::onPipReadyReadStandardOutput()
{
    QString output = pipProcess->readAllStandardOutput();
    ui->outputTextEdit->append(output.trimmed());
}

void MainWindow::onPipReadyReadStandardError()
{
    // pip writes some normal progress info to stderr too, not just errors
    QString output = pipProcess->readAllStandardError();
    ui->outputTextEdit->append(output.trimmed());
}

void MainWindow::onPipFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::NormalExit && exitCode == 0)
        ui->outputTextEdit->append("✅ Install finished successfully.");
    else
        ui->outputTextEdit->append(QString("❌ Install failed (exit code %1).").arg(exitCode));

    pipProcess->deleteLater();
    pipProcess = nullptr;
}

void MainWindow::onPipErrorOccurred(QProcess::ProcessError error)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<QProcess::ProcessError>();
    ui->outputTextEdit->append(QString("Process error: %1").arg(metaEnum.valueToKey(error)));
}

//
// persistent BLE bridge (Option 1: one connection kept open, reused for
// every command, instead of reconnecting - and replaying the pairing
// animation - on every single call)
//
QString MainWindow::bridgeScriptPath() const
{
    return QDir::homePath() + "/pypixel/pypixel_bridge.py";
}

void MainWindow::installBridgeScript()
{
    const QString path = bridgeScriptPath();
    if (QFile::exists(path))
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui->outputTextEdit->append("Failed to write bridge script: " + file.errorString());
        return;
    }

    static const char *script = R"PYEOF(#!/usr/bin/env python3
"""
pypixel_bridge.py - persistent BLE bridge for pypixelcolor.
"""
import sys
import json
import time
import asyncio
import argparse

from pypixelcolor.lib.device_session import DeviceSession
from pypixelcolor.commands import COMMANDS


def build_command_args(params):
    positional_args = []
    keyword_args = {}
    for param in params:
        if "=" in param:
            key, value = param.split("=", 1)
            keyword_args[key.replace('-', '_')] = value
        else:
            positional_args.append(param)
    return positional_args, keyword_args


def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


async def read_stdin_line(loop):
    return await loop.run_in_executor(None, sys.stdin.readline)


async def main(address):
    session = DeviceSession(address)
    try:
        await session.connect()
    except Exception as e:
        emit({"status": "error", "message": f"Failed to connect: {e}"})
        sys.exit(1)

    emit({"status": "ready", "message": "connected"})

    loop = asyncio.get_event_loop()
    while True:
        line = await read_stdin_line(loop)
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        try:
            command_data = json.loads(line)
            command_name = command_data.get("command")
            params = command_data.get("params", [])

            if command_name == "get_device_info":
                info = session.get_device_info()
                emit({
                    "status": "success",
                    "command": command_name,
                    "data": {"width": info.width, "height": info.height, "led_type": info.led_type},
                })
            elif command_name in COMMANDS:
                positional_args, keyword_args = build_command_args(params)
                command_func = COMMANDS[command_name]
                try:
                    t0 = time.monotonic()
                    await session.execute_command(command_func, *positional_args, **keyword_args)
                    elapsed_ms = (time.monotonic() - t0) * 1000
                    emit({"status": "success", "command": command_name, "elapsed_ms": elapsed_ms})
                except Exception as cmd_err:
                    sys.stderr.write(f"Command error ({command_name}): {cmd_err}\n")
                    sys.stderr.flush()
                    emit({"status": "error", "command": command_name, "message": str(cmd_err)})
            else:
                emit({"status": "error", "message": f"Unknown command: {command_name}"})
        except Exception as e:
            sys.stderr.write(f"Bridge loop exception: {e}\n")
            sys.stderr.flush()
            emit({"status": "error", "message": str(e)})

    try:
        await session.disconnect()
    except Exception:
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-a", "--address", required=True)
    args = parser.parse_args()
    asyncio.run(main(args.address))
)PYEOF";

    file.write(script);
    file.close();

    ui->outputTextEdit->append("Bridge script installed at " + path);
}

void MainWindow::startBridge(const QString &address)
{
    if (bridgeProcess != nullptr) {
        if (bridgeAddress == address) {
            // Already connected (or connecting) to this device - reuse it
            return;
        }
        // A different address was requested - tear down the old session first
        stopBridge();
    }

    if (!QFile::exists(bridgeScriptPath())) {
        ui->outputTextEdit->append("Bridge script missing, installing it now...");
        installBridgeScript();
    }

    bridgeAddress = address;
    bridgeReady = false;
    bridgeBuffer.clear();

    bridgeProcess = new QProcess(this);
    bridgeProcess->setProgram(venvPythonPath());
    bridgeProcess->setArguments(QStringList() << bridgeScriptPath() << "-a" << address);

    connect(bridgeProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onBridgeReadyReadStandardOutput);
    connect(bridgeProcess, &QProcess::readyReadStandardError,
            this, &MainWindow::onBridgeReadyReadStandardError);
    connect(bridgeProcess, &QProcess::errorOccurred,
            this, &MainWindow::onBridgeErrorOccurred);
    connect(bridgeProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onBridgeFinished);

    ui->outputTextEdit->append("Connecting to " + address + " (this may show the pairing animation once)...");
    bridgeProcess->start();
}

void MainWindow::stopBridge()
{
    if (bridgeProcess == nullptr)
        return;

    // Closing stdin gives the Python script a clean EOF, so it disconnects
    // from the BLE device gracefully instead of just being killed.
    bridgeProcess->closeWriteChannel();
    if (!bridgeProcess->waitForFinished(3000)) {
        bridgeProcess->kill();
        bridgeProcess->waitForFinished(1000);
    }

    bridgeProcess->deleteLater();
    bridgeProcess = nullptr;
    bridgeReady = false;
    bridgeAddress.clear();
    bridgeBuffer.clear();
    pendingBridgeCommands.clear();
}

void MainWindow::applyDeviceMatrixSize()
{
    if (m_watchfaceEngine)
        m_watchfaceEngine->setMatrixSize(m_deviceWidth, m_deviceHeight);
    if (m_visualizerEngine)
        m_visualizerEngine->setMatrixSize(m_deviceWidth, m_deviceHeight);
}

void MainWindow::sendPypixelCommand(const QString &command, const QStringList &params)
{
    if (bridgeProcess == nullptr) {
        ui->outputTextEdit->append("Not connected - click the connect button first.");
        return;
    }

    if (!bridgeReady) {
        // Still connecting - queue it and it'll be sent as soon as we get "ready"
        pendingBridgeCommands.append(qMakePair(command, params));
        return;
    }

    QJsonArray paramArray;
    for (const QString &p : params)
        paramArray.append(p);

    QJsonObject obj;
    obj["command"] = command;
    obj["params"] = paramArray;

    bridgeProcess->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n");
}

void MainWindow::flushPendingBridgeCommands()
{
    const auto commands = pendingBridgeCommands;
    pendingBridgeCommands.clear();
    for (const auto &cmd : commands)
        sendPypixelCommand(cmd.first, cmd.second);
}

void MainWindow::onBridgeReadyReadStandardOutput()
{
    bridgeBuffer += bridgeProcess->readAllStandardOutput();

    int newlineIndex;
    while ((newlineIndex = bridgeBuffer.indexOf('\n')) != -1) {
        const QByteArray line = bridgeBuffer.left(newlineIndex);
        bridgeBuffer.remove(0, newlineIndex + 1);

        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qDebug() << "[BLE Bridge STDOUT]:" << QString(trimmed);
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString status = obj.value("status").toString();

        if (status == "ready") {
            bridgeReady = true;
            qDebug() << "✅ [BLE Bridge] Connected & Ready.";
            ui->pushButton_2->setEnabled(true);
            flushPendingBridgeCommands();
            // Ask the device what size it actually is, rather than assuming
            // 32x32 - handled below when the "get_device_info" reply arrives.
            sendPypixelCommand("get_device_info", QStringList());
        } else if (status == "success") {
            const QString cmd = obj.value("command").toString();
            if (cmd == "get_device_info") {
                const QJsonObject data = obj.value("data").toObject();
                const int width = data.value("width").toInt(m_deviceWidth);
                const int height = data.value("height").toInt(m_deviceHeight);
                if (width > 0 && height > 0) {
                    m_deviceWidth = width;
                    m_deviceHeight = height;
                    qDebug() << "✅ [Device Info] Matrix size:" << m_deviceWidth << "x" << m_deviceHeight;
                    applyDeviceMatrixSize();
                }
            } else if (cmd == "send_image_hex" && m_visualizerActive) {
                const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_rhythmSendTimestampMs;
                qDebug() << "✅ [Visualizer Frame ACK]:" << elapsedMs << "ms";
                m_frameSendPending = false;
                // Bands may well have moved on again while this frame was
                // in flight - try the latest ones right away instead of
                // waiting for the next timer tick.
                trySendVisualizerFrame();
            } else {
                qDebug() << "✅ [Command Success]:" << cmd;
            }
        } else {
            const QString cmd = obj.value("command").toString();
            qDebug() << "❌ [Command Error]:" << cmd << "Message:" << obj.value("message").toString();
            if (cmd == "send_image_hex" && m_visualizerActive) {
                m_frameSendPending = false;
            }
        }
    }
}

void MainWindow::onBridgeReadyReadStandardError()
{
    const QString output = bridgeProcess->readAllStandardError();
    if (!output.trimmed().isEmpty()) {
        qDebug() << "[BLE Bridge STDERR]:" << output.trimmed();
    }
}

void MainWindow::onBridgeErrorOccurred(QProcess::ProcessError error)
{
    QMetaEnum metaEnum = QMetaEnum::fromType<QProcess::ProcessError>();
    ui->outputTextEdit->append(QString("Bridge process error: %1").arg(metaEnum.valueToKey(error)));
}

void MainWindow::onBridgeFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    ui->outputTextEdit->append(QString("Bridge process exited (code %1).").arg(exitCode));
    Q_UNUSED(exitStatus);
    bridgeReady = false;
    bridgeAddress.clear();
    m_frameSendPending = false;
    if (bridgeProcess) {
        bridgeProcess->deleteLater();
        bridgeProcess = nullptr;
    }
}

void MainWindow::on_pushButton_3_clicked()
{
    mac_add = ui->lineEdit->text();
    QFile file(QDir::homePath() + "/.pypixelguiconf/conf.txt");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Could not open file:" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out << mac_add;
    file.close();
    startBridge(mac_add);
    ui->label_22->setText(mac_add);
    sendPypixelCommand("send_text", QStringList()
                                        << "CONNECTION SUCCESFULL!"
                                        << "animation=1" << "speed=50" << "color=ffffff");
}
void MainWindow::on_pushButton_6_clicked()
{
    ui->stackedWidget->setCurrentIndex(6);
}


void MainWindow::on_pushButton_15_clicked()
{
    sendPypixelCommand("send_text", QStringList()
                       << ui->lineEdit_3->text()
                       << QString("animation=%1").arg(ui->comboBox_2->currentIndex())
                       << QString("speed=%1").arg(ui->horizontalSlider->value())
                       << QString("color=%1").arg(ui->lineEdit_4->text()));
}


void MainWindow::on_pushButton_16_clicked()
{
    ui->stackedWidget->setCurrentIndex(5);
}



void MainWindow::on_pushButton_4_clicked()
{
    sendPypixelCommand("send_text", QStringList()
                       << "."
                       << "animation=1" << "speed=50" << "color=000000");
}


void MainWindow::on_pushButton_7_clicked()
{
    ui->stackedWidget->setCurrentIndex(7);
}


void MainWindow::on_pushButton_17_clicked()
{
    QStringList mimeTypeFilters({
        "image/jpeg", // will show "JPEG image (*.jpeg *.jpg *.jpe)"
        "image/png",  // will show "PNG image (*.png)"
        "application/octet-stream" // will show "All files (*)"
    });

    QFileDialog dialog(this);
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setDirectory(QDir::homePath());     // restrict to a single existing file
    dialog.setAcceptMode(QFileDialog::AcceptOpen);   // "Open" dialog (default, but explicit is nice)

    if (dialog.exec() == QDialog::Accepted) {
        QStringList files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            ui->lineEdit_7->setText(files.first());
        }
    }
}


void MainWindow::on_pushButton_19_clicked()
{
    ui->stackedWidget->setCurrentIndex(5);
}


void MainWindow::on_pushButton_18_clicked()
{
    sendPypixelCommand("send_image", QStringList() << ui->lineEdit_7->text());
}



void MainWindow::on_listWidget_currentRowChanged(int currentRow)
{

}


void MainWindow::on_pushButton_20_clicked()
{
    QString show_date = ui->checkBox_2->isChecked() ? "True" : "False";
    QString format_24 = ui->checkBox_3->isChecked() ? "True" : "False";

    sendPypixelCommand("set_clock_mode", QStringList()
                                             << QString("style=%1").arg(ui->comboBox->currentIndex() + 1)
                                             << QString("show_date=%1").arg(show_date)
                                             << QString("format_24=%1").arg(format_24));

}


void MainWindow::on_pushButton_8_clicked()
{
    ui->stackedWidget->setCurrentIndex(8);
    ui->listWidget->clear();
    QDir dir(QDir::homePath() + "/.pypixelguiconf/faces");
    QStringList dirList = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dirName : dirList) {
        ui->listWidget->addItem(dirName);
    }
}

//
//music player
//



void MainWindow::on_pushButton_21_clicked()
{
    QFileDialog dialog(this);
    dialog.setNameFilter(tr("Json watchface file (face.json)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setDirectory(QDir::homePath());     // restrict to a single existing file
    dialog.setAcceptMode(QFileDialog::AcceptOpen);   // "Open" dialog (default, but explicit is nice)

    if (dialog.exec() == QDialog::Accepted) {
        QStringList files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            ui->lineEdit_5->setText(files.first());
        }
    }
}

bool MainWindow::copyDirRecursively(const QString &srcPath, const QString &dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists())
        return false;

    QDir dstDir(dstPath);
    if (!dstDir.exists()) {
        if (!QDir().mkpath(dstPath))
            return false;
    }

    const QFileInfoList entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        QString srcItemPath = entry.absoluteFilePath();
        QString dstItemPath = dstPath + "/" + entry.fileName();

        if (entry.isDir()) {
            if (!copyDirRecursively(srcItemPath, dstItemPath))
                return false;
        } else {
            // remove existing file at destination first, QFile::copy fails if it exists
            if (QFile::exists(dstItemPath))
                QFile::remove(dstItemPath);
            if (!QFile::copy(srcItemPath, dstItemPath))
                return false;
        }
    }
    return true;
}

void MainWindow::on_pushButton_24_clicked()
{
    QFileInfo fileInfo(ui->lineEdit_5->text());
    QString srcDir = fileInfo.path();                 // .../watch
    QString dstDir = QDir::homePath() + "/.pypixelguiconf/faces/" + fileInfo.dir().dirName(); // .../faces/watch

    if (!copyDirRecursively(srcDir, dstDir)) {
        qWarning() << "Failed to copy" << srcDir << "to" << dstDir;
    }

    ui->listWidget->clear();
    QDir dir(QDir::homePath() + "/.pypixelguiconf/faces");
    QStringList dirList = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &dirName : dirList) {
        ui->listWidget->addItem(dirName);
    }
}


void MainWindow::on_pushButton_22_clicked()
{
    ui->listWidget->currentItem()->text();
    // Create the engine once and own it on MainWindow, rather than as a
    // local variable - a local would be destroyed the instant this slot
    // returns, but the QTimer below keeps firing (and touching it) long
    // after that, which is undefined behaviour / a crash waiting to happen.
    if (!m_watchfaceEngine) {
        m_watchfaceEngine = new WatchfaceEngine(this);
        // Seed with the real queried device size rather than the engine's
        // own 32x32 default - loadWatchface() below will still override
        // this if the face's JSON specifies its own width/height.
        m_watchfaceEngine->setMatrixSize(m_deviceWidth, m_deviceHeight);

        // Needs 'this' captured - sendPypixelCommand is a MainWindow member.
        //
        // We send the whole frame as one image (send_image_hex) rather than
        // one set_pixel command per changed pixel. pypixelcolor's set_pixel
        // is fire-and-forget over BLE (requires_ack=False) - unlike
        // send_text/send_image, which wait for the device to confirm
        // delivery - so a burst of per-pixel writes for a single digit
        // change can be silently dropped, leaving the display stuck on a
        // stale frame with no error and no way for us to detect it.
        m_watchfaceEngine->sendFrame = [this](const QImage &frame) {
            QByteArray pngBytes;
            QBuffer buffer(&pngBytes);
            buffer.open(QIODevice::WriteOnly);
            if (!frame.save(&buffer, "PNG")) {
                ui->outputTextEdit->append("Failed to encode watchface frame.");
                return;
            }
            sendPypixelCommand("send_image_hex", QStringList()
                                                     << QString::fromLatin1(pngBytes.toHex())
                                                     << ".png");
        };
    }

    if (!m_watchfaceEngine->loadWatchface(QDir::homePath() + "/.pypixelguiconf/faces/" + ui->listWidget->currentItem()->text() + "/face.json")) {
        ui->outputTextEdit->append("Failed to load watchface (missing file or invalid JSON) - not starting updates.");
        return;
    }

    if (!m_watchfaceTimer) {
        m_watchfaceTimer = new QTimer(this);
        connect(m_watchfaceTimer, &QTimer::timeout, this, [this]() {
            m_watchfaceEngine->renderFrame(QDateTime::currentDateTime());
        });
    }
    // Was 10000 (10s) - anything bound to "second" (or otherwise time-sensitive)
    // only got redrawn once every 10 ticks, which looked like the watchface
    // "didn't always update". Match the 1s cadence used by the clock timer.
    m_watchfaceTimer->start(1000);
    ui->pushButton_9->setEnabled(true);
}


void MainWindow::on_pushButton_9_clicked()
{
    m_watchfaceTimer->stop();
    ui->pushButton_9->setEnabled(false);
}


void MainWindow::on_pushButton_10_clicked()
{
    ui->stackedWidget->setCurrentIndex(5);
}


void MainWindow::on_pushButton_11_clicked()
{
    if (power == true)
    {
        ui->pushButton_11->setText("Power on");
        sendPypixelCommand("set_power", QStringList() << "on=False");
        power = false;
    }
    else
    {
        ui->pushButton_11->setText("Power off");
        sendPypixelCommand("set_power", QStringList() << "on=True");
        power = true;
    }
}


void MainWindow::on_pushButton_5_clicked()
{
    startBridge(mac_add);
}


void MainWindow::on_pushButton_14_clicked()
{
    ui->stackedWidget->setCurrentIndex(9);
}


void MainWindow::on_pushButton_26_clicked()
{
    ui->stackedWidget->setCurrentIndex(5);
}


void MainWindow::on_pushButton_25_clicked()
{
    sendPypixelCommand("set_brightness", QStringList()
                       << QString("level=%1").arg(ui->horizontalSlider_2->value()));
    sendPypixelCommand("set_orientation", QStringList()
                                              << QString("orientation=%1").arg(ui->comboBox_3->currentIndex()));
}


void MainWindow::on_pushButton_28_clicked()
{
    ui->stackedWidget->setCurrentIndex(10);
}


void MainWindow::on_pushButton_29_clicked()
{
    ui->stackedWidget->setCurrentIndex(9);
}


void MainWindow::on_pushButton_30_clicked()
{
    if (m_visualizerActive) {
        stopVisualizer();
        ui->pushButton_30->setText("Start Visualizer");
    } else {
        startVisualizer();
        ui->pushButton_30->setText("Stop Visualizer");
    }
}

void MainWindow::startVisualizer()
{
    if (m_visualizerActive)
        return;

    m_frameSendPending = false;

    if (!m_audioCapture) {
        m_audioCapture = createLoopbackCapture(this);
        if (!m_audioCapture) {
            ui->outputTextEdit->append("No system-audio loopback backend is available on this platform.");
            return;
        }
        connect(m_audioCapture, &AudioLoopbackCapture::samplesReady,
                this, &MainWindow::onLoopbackSamplesReady);
        connect(m_audioCapture, &AudioLoopbackCapture::errorOccurred,
                this, &MainWindow::onLoopbackErrorOccurred);
    }

    if (!m_spectrumAnalyzer) {
        m_spectrumAnalyzer = new SpectrumAnalyzer(this);
        m_spectrumAnalyzer->setFftSize(1024);
        m_spectrumAnalyzer->setBandCount(11);
        m_spectrumAnalyzer->setMaxLevel(15);
        connect(m_spectrumAnalyzer, &SpectrumAnalyzer::bandsReady,
                this, &MainWindow::onSpectrumBandsReady);
    }

    if (!m_visualizerEngine) {
        m_visualizerEngine = new SpectrumVisualizerEngine(this);
        // m_deviceWidth/Height come from get_device_info once the bridge
        // connects (see onBridgeReadyReadStandardOutput); 32x32 is only
        // the fallback until that reply arrives.
        m_visualizerEngine->setMatrixSize(m_deviceWidth, m_deviceHeight);
        m_visualizerEngine->setBandCount(11);
        m_visualizerEngine->setMaxLevel(15);

        // Same pattern as the watchface engine's sendFrame: encode the
        // whole matrix as one PNG and send it through the acknowledged
        // send_image_hex transfer, rather than per-pixel writes.
        m_visualizerEngine->sendFrame = [this](const QImage &frame) {
            QByteArray pngBytes;
            QBuffer buffer(&pngBytes);
            buffer.open(QIODevice::WriteOnly);
            if (!frame.save(&buffer, "PNG")) {
                ui->outputTextEdit->append("Failed to encode visualizer frame.");
                return;
            }
            m_frameSendPending = true;
            m_rhythmSendTimestampMs = QDateTime::currentMSecsSinceEpoch();
            sendPypixelCommand("send_image_hex", QStringList()
                                                     << QString::fromLatin1(pngBytes.toHex())
                                                     << ".png");
        };
    }

    if (!m_rhythmSendTimer) {
        m_rhythmSendTimer = new QTimer(this);
        connect(m_rhythmSendTimer, &QTimer::timeout, this, &MainWindow::onRhythmSendTimerTimeout);
    }

    if (!m_audioCapture->start(44100)) {
        ui->outputTextEdit->append("Failed to start audio capture.");
        return;
    }

    // Safety-net only - the real driver of send cadence is
    // trySendVisualizerFrame(), called from onSpectrumBandsReady() and
    // again as soon as each frame's ack comes back. This just re-tries in
    // case a bandsReady/ack event goes missing. 150ms, matching the
    // in-code comment this used to have (the old code actually started
    // this timer at 1000ms, which is why updates looked stuck at ~1/sec).
    m_rhythmSendTimer->start(150);
    m_visualizerActive = true;
    qDebug() << "Visualizer started.";
}

void MainWindow::stopVisualizer()
{
    if (m_rhythmSendTimer)
        m_rhythmSendTimer->stop();
    if (m_audioCapture)
        m_audioCapture->stop();
    m_visualizerActive = false;
    m_frameSendPending = false;
}

void MainWindow::onLoopbackSamplesReady(QVector<float> samples, int sampleRate)
{
    if (m_spectrumAnalyzer)
        m_spectrumAnalyzer->addSamples(samples, sampleRate);
}

void MainWindow::onLoopbackErrorOccurred(QString message)
{
    stopVisualizer();
    ui->pushButton_30->setText("Start Visualizer");
}

void MainWindow::onSpectrumBandsReady(QVector<int> levels)
{
    m_latestBandLevels = levels;
    // Try to render/send right away with these fresher levels instead of
    // waiting for the next timer tick - if nothing's in flight, this is
    // what actually gets us updates as fast as the BLE link can ack them.
    trySendVisualizerFrame();
}

void MainWindow::onRhythmSendTimerTimeout()
{
    // This timer is now just a safety net (in case bandsReady/ack events
    // stop arriving for some reason) - the real driver of send cadence is
    // trySendVisualizerFrame(), called from onSpectrumBandsReady() and again
    // as soon as each ack comes back in onBridgeReadyReadStandardOutput().
    trySendVisualizerFrame();
}

void MainWindow::trySendVisualizerFrame()
{
    if (!m_visualizerActive || !m_visualizerEngine)
        return;

    if (m_latestBandLevels.size() < 11) {
        qDebug() << "⚠️ [Visualizer] Waiting for band levels...";
        return;
    }

    if (bridgeProcess == nullptr || !bridgeReady) {
        qDebug() << "⚠️ [Visualizer] Bridge process not connected.";
        return;
    }

    if (m_frameSendPending) {
        // Normal behavior: previous frame is still waiting for ACK from the
        // screen. Deliberately NOT calling render() here - the engine only
        // diffs against frames it actually sent, so skipping the call
        // entirely (rather than rendering and discarding the result) keeps
        // that bookkeeping correct.
        return;
    }

    // render() draws the bars, diffs them against the last frame actually
    // sent, and only calls sendFrame() (which sets m_frameSendPending) if
    // something changed - most ticks where the bars haven't moved send
    // nothing at all.
    m_visualizerEngine->render(m_latestBandLevels);
}

void MainWindow::on_pushButton_12_clicked()
{
    ui->stackedWidget->setCurrentIndex(11);
}