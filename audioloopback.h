#pragma once
#include <QObject>
#include <QVector>

// Common interface for "system audio" (loopback) capture.
// Implemented differently per platform - see linuxloopbackcapture.*
// and windowsloopbackcapture.* - but consumers only need this interface.
class AudioLoopbackCapture : public QObject
{
    Q_OBJECT
public:
    explicit AudioLoopbackCapture(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioLoopbackCapture() override = default;

    virtual bool start(int sampleRate = 44100) = 0;
    virtual void stop() = 0;

signals:
    // Mono float samples in range [-1.0, 1.0]. sampleRate is the actual
    // rate the backend ended up using (may differ from the requested one).
    void samplesReady(QVector<float> samples, int sampleRate);
    void errorOccurred(QString message);
};

// Factory - returns the right backend for the current OS.
// Implemented in loopbackfactory.cpp
AudioLoopbackCapture *createLoopbackCapture(QObject *parent = nullptr);