#pragma once
#include "audioloopback.h"

#ifdef Q_OS_WIN

// Captures "what you hear" on Windows using raw WASAPI loopback mode.
// This works regardless of whether "Stereo Mix" is enabled, since it
// opens the default *playback* device directly in a special capture
// mode (AUDCLNT_STREAMFLAGS_LOOPBACK) rather than relying on a
// user-configured recording device.
//
// All COM/WASAPI calls happen on a dedicated worker thread (see the
// .cpp file), since COM objects must be created and used from the
// same thread.
class WindowsLoopbackCapture : public AudioLoopbackCapture
{
    Q_OBJECT
public:
    explicit WindowsLoopbackCapture(QObject *parent = nullptr);
    ~WindowsLoopbackCapture() override;

    bool start(int sampleRate = 44100) override;
    void stop() override;

private:
    class CaptureWorker;
    CaptureWorker *m_worker = nullptr;
};

#endif // Q_OS_WIN