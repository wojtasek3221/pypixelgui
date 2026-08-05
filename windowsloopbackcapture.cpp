#include "windowsloopbackcapture.h"

#ifdef Q_OS_WIN

#include <atomic>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <combaseapi.h>

// Owns the WASAPI loopback session and runs the capture loop.
// Lives on its own QThread because COM objects are thread-affine.
class WindowsLoopbackCapture::CaptureWorker : public QThread
{
public:
    explicit CaptureWorker(WindowsLoopbackCapture *owner) : m_owner(owner) {}

    void requestStop() { m_stopRequested.store(true); }

protected:
    void run() override
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitializedHere = SUCCEEDED(hr);

        IMMDeviceEnumerator *enumerator = nullptr;
        IMMDevice *device = nullptr;
        IAudioClient *audioClient = nullptr;
        IAudioCaptureClient *captureClient = nullptr;
        WAVEFORMATEX *mixFormat = nullptr;
        HANDLE audioEvent = nullptr;

        auto cleanup = [&]() {
            if (captureClient) captureClient->Release();
            if (audioClient) audioClient->Release();
            if (device) device->Release();
            if (enumerator) enumerator->Release();
            if (mixFormat) CoTaskMemFree(mixFormat);
            if (audioEvent) CloseHandle(audioEvent);
            if (comInitializedHere) CoUninitialize();
        };

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void **>(&enumerator));
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to create device enumerator."));
            cleanup();
            return;
        }

        // eRender + eConsole = the current default playback device -
        // i.e. "system audio" / "what you hear".
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("No default playback device found."));
            cleanup();
            return;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&audioClient));
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to activate audio client."));
            cleanup();
            return;
        }

        hr = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to get mix format."));
            cleanup();
            return;
        }

        audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!audioEvent) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to create audio event."));
            cleanup();
            return;
        }

        // AUDCLNT_STREAMFLAGS_LOOPBACK is what turns a playback endpoint
        // into something you can read from as if it were a microphone.
        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            10000000, // 1 second buffer, in 100ns units
            0, mixFormat, nullptr);
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to initialize audio client in loopback mode."));
            cleanup();
            return;
        }

        hr = audioClient->SetEventHandle(audioEvent);
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to set audio event handle."));
            cleanup();
            return;
        }

        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                     reinterpret_cast<void **>(&captureClient));
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to get capture client."));
            cleanup();
            return;
        }

        const int channels = mixFormat->nChannels;
        const int sampleRate = static_cast<int>(mixFormat->nSamplesPerSec);
        // WASAPI's shared-mode mix format is float32 on essentially every
        // modern Windows install; the 16-bit branch below is a fallback.
        const bool isFloat = (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
                             || (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE);

        hr = audioClient->Start();
        if (FAILED(hr)) {
            emit m_owner->errorOccurred(QStringLiteral("Failed to start audio client."));
            cleanup();
            return;
        }

        while (!m_stopRequested.load()) {
            const DWORD waitResult = WaitForSingleObject(audioEvent, 200);
            if (waitResult != WAIT_OBJECT_0)
                continue; // timeout - loop back around and check the stop flag

            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr))
                break;

            while (packetLength != 0) {
                BYTE *data = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
                if (FAILED(hr))
                    break;

                QVector<float> mono(static_cast<int>(numFrames));
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

                if (silent || !data) {
                    mono.fill(0.0f);
                } else if (isFloat) {
                    const auto *src = reinterpret_cast<const float *>(data);
                    for (UINT32 i = 0; i < numFrames; ++i) {
                        float sum = 0.0f;
                        for (int c = 0; c < channels; ++c)
                            sum += src[i * channels + c];
                        mono[static_cast<int>(i)] = sum / channels;
                    }
                } else {
                    const auto *src = reinterpret_cast<const int16_t *>(data);
                    for (UINT32 i = 0; i < numFrames; ++i) {
                        int sum = 0;
                        for (int c = 0; c < channels; ++c)
                            sum += src[i * channels + c];
                        mono[static_cast<int>(i)] = (sum / static_cast<float>(channels)) / 32768.0f;
                    }
                }

                emit m_owner->samplesReady(mono, sampleRate);

                hr = captureClient->ReleaseBuffer(numFrames);
                if (FAILED(hr))
                    break;

                hr = captureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr))
                    break;
            }
        }

        audioClient->Stop();
        cleanup();
    }

private:
    WindowsLoopbackCapture *m_owner;
    std::atomic<bool> m_stopRequested{false};
};

WindowsLoopbackCapture::WindowsLoopbackCapture(QObject *parent)
    : AudioLoopbackCapture(parent)
{
}

WindowsLoopbackCapture::~WindowsLoopbackCapture()
{
    stop();
}

bool WindowsLoopbackCapture::start(int /*sampleRate*/)
{
    // Note: WASAPI loopback always uses the device's own mix format/rate
    // (read from GetMixFormat() in the worker), so the requested rate is
    // not forced here - the SpectrumAnalyzer downstream just uses
    // whatever sampleRate comes through with each samplesReady signal.
    if (m_worker)
        return true;

    m_worker = new CaptureWorker(this);
    m_worker->start();
    return true;
}

void WindowsLoopbackCapture::stop()
{
    if (!m_worker)
        return;

    m_worker->requestStop();
    m_worker->wait(1000);
    delete m_worker;
    m_worker = nullptr;
}

#endif // Q_OS_WIN