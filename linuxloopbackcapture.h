#ifndef LINUXLOOPBACKCAPTURE_H
#define LINUXLOOPBACKCAPTURE_H

#include "audioloopback.h"
#include <QAudioFormat>
#include <QProcess>

// Captures "what you hear" on Linux via the PulseAudio/PipeWire "monitor"
// source. See linuxloopbackcapture.cpp for details.
class LinuxLoopbackCapture : public AudioLoopbackCapture
{
    Q_OBJECT
public:
    explicit LinuxLoopbackCapture(QObject *parent = nullptr);
    ~LinuxLoopbackCapture() override;

    bool start(int sampleRate = 44100) override;
    void stop() override;

private slots:
    void readAvailableData();

private:
    QString findMonitorSourceName() const;

    QProcess *m_parecProcess = nullptr;
    QByteArray m_leftover;
    QAudioFormat m_format;
};

#endif // LINUXLOOPBACKCAPTURE_H