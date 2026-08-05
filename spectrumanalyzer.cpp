#include "spectrumanalyzer.h"
#include <QtMath>
#include <algorithm>

SpectrumAnalyzer::SpectrumAnalyzer(QObject *parent)
    : QObject(parent)
{
    m_smoothed.fill(0.0f, m_bandCount);
}

void SpectrumAnalyzer::setFftSize(int size)
{
    m_fftSize = size;
}

void SpectrumAnalyzer::setBandCount(int bands)
{
    m_bandCount = bands;
    m_smoothed.fill(0.0f, m_bandCount);
}

void SpectrumAnalyzer::setMaxLevel(int level)
{
    m_maxLevel = level;
}

void SpectrumAnalyzer::addSamples(QVector<float> samples, int sampleRate)
{
    m_buffer += samples;

    while (m_buffer.size() >= m_fftSize) {
        processBuffer(sampleRate);
        // 50% overlap between windows - smoother output than
        // consuming the whole buffer and starting from zero each time.
        const int hop = m_fftSize / 2;
        m_buffer.remove(0, hop);
    }
}

void SpectrumAnalyzer::processBuffer(int sampleRate)
{
    QVector<std::complex<float>> data(m_fftSize);

    // Hann window to reduce spectral leakage between bins.
    for (int i = 0; i < m_fftSize; ++i) {
        const float w = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * i / (m_fftSize - 1)));
        data[i] = std::complex<float>(m_buffer[i] * w, 0.0f);
    }

    fft(data);

    const int usableBins = m_fftSize / 2; // real input -> symmetric spectrum

    // Bands are spaced logarithmically, since musical content (and how
    // we perceive it) is distributed logarithmically across frequency -
    // linear spacing would give you 10 bass-only bands and 1 for everything else.
    QVector<float> bandEnergy(m_bandCount, 0.0f);
    const float minFreq = 40.0f;
    const float maxFreq = qMin(16000.0f, sampleRate / 2.0f);
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(maxFreq);

    for (int b = 0; b < m_bandCount; ++b) {
        const float f0 = std::pow(10.0f, logMin + (logMax - logMin) * b / m_bandCount);
        const float f1 = std::pow(10.0f, logMin + (logMax - logMin) * (b + 1) / m_bandCount);

        int bin0 = qMax(1, int(f0 * m_fftSize / sampleRate));
        int bin1 = qMin(usableBins - 1, int(f1 * m_fftSize / sampleRate));
        if (bin1 <= bin0)
            bin1 = bin0 + 1;

        float sum = 0.0f;
        int count = 0;
        for (int i = bin0; i <= bin1 && i < usableBins; ++i) {
            sum += std::abs(data[i]);
            ++count;
        }
        bandEnergy[b] = count > 0 ? sum / count : 0.0f;
    }

    float peak = *std::max_element(bandEnergy.begin(), bandEnergy.end());
    if (peak < 1e-6f)
        peak = 1e-6f;

    QVector<int> levels(m_bandCount);
    for (int b = 0; b < m_bandCount; ++b) {
        const float normalized = qBound(0.0f, bandEnergy[b] / peak, 1.0f);
        // Fast attack / slow decay so bars snap up on a hit and settle
        // down gradually, instead of flickering every frame.
        m_smoothed[b] = qMax(normalized, m_smoothed[b] * 0.2f);
        levels[b] = qRound(m_smoothed[b] * m_maxLevel);
    }

    emit bandsReady(levels);
}

void SpectrumAnalyzer::fft(QVector<std::complex<float>> &data)
{
    const int n = data.size();
    if (n <= 1)
        return;

    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // Iterative Cooley-Tukey radix-2 FFT.
    for (int len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * float(M_PI) / len;
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<float> u = data[i + j];
                const std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}