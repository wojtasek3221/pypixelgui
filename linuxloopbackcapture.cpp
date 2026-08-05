#include "linuxloopbackcapture.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QDebug>
#include <cstring>

LinuxLoopbackCapture::LinuxLoopbackCapture(QObject *parent)
    : AudioLoopbackCapture(parent)
{
}

LinuxLoopbackCapture::~LinuxLoopbackCapture()
{
    stop();
}

bool LinuxLoopbackCapture::start(int sampleRate)
{
    // QMediaDevices::audioInputs() does not reliably enumerate PulseAudio/
    // PipeWire *monitor* sources on all Qt Multimedia backends (notably the
    // native "pipewire" QPA plugin) - even when a monitor source exists and
    // is RUNNING according to `pactl list sources short`. Rather than
    // depend on Qt's enumeration, we find the monitor source name via
    // `pactl` directly and capture it with `parec`, which talks straight to
    // the PulseAudio/PipeWire server and always sees monitor sources.

    const QString monitorName = findMonitorSourceName();
    if (monitorName.isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "No PulseAudio/PipeWire monitor source found. Make sure "
            "PulseAudio or PipeWire (with pipewire-pulse) is running. "
            "You can check available sources with: pactl list sources short"));
        return false;
    }

    qDebug() << "Using monitor source:" << monitorName;

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_format = format;

    // Ask parec to hand us raw interleaved mono 16-bit PCM directly, so no
    // manual downmixing/format-detection is needed on our end.
    m_parecProcess = new QProcess(this);
    const QStringList args = {
        QStringLiteral("--device=%1").arg(monitorName),
        QStringLiteral("--format=s16le"),
        QStringLiteral("--rate=%1").arg(sampleRate),
        QStringLiteral("--channels=1"),
        QStringLiteral("--raw"),
    };

    connect(m_parecProcess, &QProcess::readyReadStandardOutput,
            this, &LinuxLoopbackCapture::readAvailableData);
    connect(m_parecProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                emit errorOccurred(QStringLiteral("parec process error: %1")
                                       .arg(m_parecProcess->errorString()));
            });

    m_parecProcess->start(QStringLiteral("parec"), args);
    if (!m_parecProcess->waitForStarted(3000)) {
        emit errorOccurred(QStringLiteral(
            "Failed to start 'parec'. Make sure pulseaudio-utils "
            "(or pipewire-pulse's parec) is installed."));
        m_parecProcess->deleteLater();
        m_parecProcess = nullptr;
        return false;
    }

    return true;
}

void LinuxLoopbackCapture::stop()
{
    if (m_parecProcess) {
        disconnect(m_parecProcess, nullptr, this, nullptr);
        m_parecProcess->terminate();
        if (!m_parecProcess->waitForFinished(1000))
            m_parecProcess->kill();
        m_parecProcess->deleteLater();
        m_parecProcess = nullptr;
    }
    m_leftover.clear();
}

QString LinuxLoopbackCapture::findMonitorSourceName() const
{
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sources"), QStringLiteral("short")});
    if (!pactl.waitForStarted(2000) || !pactl.waitForFinished(3000))
        return QString();

    const QString output = QString::fromUtf8(pactl.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QString fallback;
    for (const QString &line : lines) {
        // Format: <index>\t<name>\t<driver>\t<sample spec>\t<state>
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 2)
            continue;
        const QString &name = fields.at(1);
        if (!name.contains(QStringLiteral("monitor"), Qt::CaseInsensitive))
            continue;

        // Prefer a monitor that's currently RUNNING (i.e. tied to the
        // active default output) over an idle one.
        if (line.contains(QStringLiteral("RUNNING")))
            return name;
        if (fallback.isEmpty())
            fallback = name;
    }
    return fallback;
}

void LinuxLoopbackCapture::readAvailableData()
{
    if (!m_parecProcess)
        return;

    QByteArray data = m_leftover + m_parecProcess->readAllStandardOutput();
    if (data.isEmpty())
        return;

    // We requested s16le mono from parec directly, so each frame is exactly
    // one 16-bit sample - no channel downmixing or format detection needed.
    const int bytesPerFrame = 2;
    const int frameCount = data.size() / bytesPerFrame;
    const int usableBytes = frameCount * bytesPerFrame;

    // Keep any trailing partial frame for the next read.
    m_leftover = data.mid(usableBytes);

    const char *raw = data.constData();
    QVector<float> floatSamples(frameCount);
    for (int i = 0; i < frameCount; ++i) {
        qint16 sample;
        memcpy(&sample, raw + i * bytesPerFrame, sizeof(sample));
        floatSamples[i] = sample / 32768.0f;
    }

    emit samplesReady(floatSamples, m_format.sampleRate());
}