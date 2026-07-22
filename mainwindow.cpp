#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QProcess>
#include <QMetaEnum>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QMessageBox>
#include <QFileDialog>
static int page = 0;
static bool deps_ok = true;
static QString mac_add;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , pipProcess(nullptr)
{
    ui->setupUi(this);

    ui->stackedWidget->setCurrentIndex(0);
    ui->pushButton_2->setEnabled(false);

    ui->textEdit->insertPlainText("checking dependencies...");
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

MainWindow::~MainWindow()
{
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
//venv shit
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
        return; // already installed, nothing to do

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui->outputTextEdit->append("Failed to write bridge script: " + file.errorString());
        return;
    }

    static const char *script = R"PYEOF(#!/usr/bin/env python3
"""
pypixel_bridge.py - persistent BLE bridge for pypixelcolor.
Connects to the BLE device ONCE and keeps the connection open for the
lifetime of the process. Talks JSON, one object per line, over stdin/stdout.
"""
import sys
import json
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
                    await session.execute_command(command_func, *positional_args, **keyword_args)
                    emit({"status": "success", "command": command_name})
                except Exception as cmd_err:
                    emit({"status": "error", "command": command_name, "message": str(cmd_err)})
            else:
                emit({"status": "error", "message": f"Unknown command: {command_name}"})
        except Exception as e:
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
            ui->outputTextEdit->append("Bridge: " + QString(trimmed));
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString status = obj.value("status").toString();

        if (status == "ready") {
            bridgeReady = true;
            ui->outputTextEdit->append("✅ Connected - session will stay open for further commands.");
            ui->pushButton_2->setEnabled(true);
            flushPendingBridgeCommands();
        } else if (status == "success") {
            ui->outputTextEdit->append("✅ " + obj.value("command").toString() + " sent.");
        } else {
            ui->outputTextEdit->append("❌ " + obj.value("message").toString());
        }
    }
}

void MainWindow::onBridgeReadyReadStandardError()
{
    const QString output = bridgeProcess->readAllStandardError();
    if (!output.trimmed().isEmpty())
        ui->outputTextEdit->append("Bridge stderr: " + output.trimmed());
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
    if (bridgeProcess) {
        bridgeProcess->deleteLater();
        bridgeProcess = nullptr;
    }
}

void MainWindow::on_pushButton_3_clicked()
{
    const QString address = ui->lineEdit->text();
    startBridge(address);
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

