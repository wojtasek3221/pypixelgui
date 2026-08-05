#pragma once
#include <QObject>
#include <QVector>
#include <complex>

// Turns a stream of mono float samples into N band levels (0..maxLevel),
// suitable for feeding straight into set_rhythm_mode's l1..l11 parameters.
class SpectrumAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit SpectrumAnalyzer(QObject *parent = nullptr);

    void setFftSize(int size);     // must be a power of two, e.g. 1024
    void setBandCount(int bands);  // 11, to match set_rhythm_mode
    void setMaxLevel(int level);   // 15, to match set_rhythm_mode

public slots:
    void addSamples(QVector<float> samples, int sampleRate);

signals:
    void bandsReady(QVector<int> levels);

private:
    void processBuffer(int sampleRate);
    static void fft(QVector<std::complex<float>> &data);

    QVector<float> m_buffer;
    QVector<float> m_smoothed;
    int m_fftSize = 1024;
    int m_bandCount = 11;
    int m_maxLevel = 15;
};