// spectrumvisualizerengine.cpp
#include "spectrumvisualizerengine.h"
#include <QColor>
#include <QtMath>

SpectrumVisualizerEngine::SpectrumVisualizerEngine(QObject *parent)
    : QObject(parent)
{
}

void SpectrumVisualizerEngine::setMatrixSize(int width, int height)
{
    m_matrixSize = QSize(qMax(1, width), qMax(1, height));
}

void SpectrumVisualizerEngine::setBandCount(int bands)
{
    m_bandCount = qMax(1, bands);
}

void SpectrumVisualizerEngine::setMaxLevel(int level)
{
    m_maxLevel = qMax(1, level);
}

void SpectrumVisualizerEngine::setBarColors(const QString &lowColor, const QString &midColor, const QString &highColor)
{
    m_lowColor = lowColor;
    m_midColor = midColor;
    m_highColor = highColor;
}

void SpectrumVisualizerEngine::setEaseFactor(float factor)
{
    m_easeFactor = qBound(0.05f, factor, 1.0f);
}

QString SpectrumVisualizerEngine::colorForBar(int barHeightPx) const
{
    // Colour the whole bar by how full it is overall (classic EQ look -
    // green body, yellow as it climbs, red only near the very top) rather
    // than per-pixel row, so a bar shifts colour as one piece as it grows.
    const float fraction = m_matrixSize.height() > 0
                               ? float(barHeightPx) / float(m_matrixSize.height())
                               : 0.0f;
    if (fraction > 0.85f) return m_highColor;
    if (fraction > 0.6f) return m_midColor;
    return m_lowColor;
}

void SpectrumVisualizerEngine::render(const QVector<int> &levels)
{
    m_currentFrame.clear();

    const int width = m_matrixSize.width();
    const int height = m_matrixSize.height();
    const int bands = qMin(m_bandCount, levels.size());
    if (bands <= 0 || width <= 0 || height <= 0)
        return;

    if (m_displayLevels.size() != m_bandCount)
        m_displayLevels.fill(0.0f, m_bandCount);

    // Evenly divide the matrix width across bands, leaving a 1px gap
    // between bars so they read as separate columns.
    const int barSlot = qMax(1, width / bands);

    for (int b = 0; b < bands; ++b) {
        const float target = float(qBound(0, levels[b], m_maxLevel));
        float &displayed = m_displayLevels[b];

        // Close a fraction of the remaining gap rather than snapping
        // straight to target - see setEaseFactor(). Snap once close enough
        // so it actually settles (and sends stop) instead of asymptotically
        // crawling forever.
        displayed += (target - displayed) * m_easeFactor;
        if (qAbs(target - displayed) < 0.15f)
            displayed = target;

        const int barHeight = qRound((displayed / float(m_maxLevel)) * height);
        const QString color = colorForBar(barHeight);

        const int x0 = b * barSlot;
        const int x1 = qMin(width, x0 + barSlot) - 1; // stop one column short - that's the gap
        if (x1 < x0)
            continue;

        for (int y = height - barHeight; y < height; ++y) {
            if (y < 0)
                continue;
            for (int x = x0; x < x1; ++x)
                m_currentFrame[QPoint(x, y)] = color;
        }
    }

    flushFrame();
}

void SpectrumVisualizerEngine::flushFrame()
{
    // Work out whether anything actually changed since the last frame we
    // sent, so a held or silent note doesn't retransmit an identical image
    // every tick.
    bool changed = !m_haveLastFrame || m_currentFrame.size() != m_lastFrame.size();
    if (!changed) {
        for (auto it = m_currentFrame.constBegin(); it != m_currentFrame.constEnd(); ++it) {
            auto prev = m_lastFrame.constFind(it.key());
            if (prev == m_lastFrame.constEnd() || prev.value() != it.value()) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        for (auto it = m_lastFrame.constBegin(); it != m_lastFrame.constEnd(); ++it) {
            if (!m_currentFrame.contains(it.key())) {
                changed = true;
                break;
            }
        }
    }

    if (!changed || !sendFrame)
        return;

    // Only commit m_lastFrame once we're actually about to send - if the
    // caller is throttling (e.g. a previous frame is still awaiting ack)
    // it should skip calling render() entirely for this tick rather than
    // let us diff-and-drop, otherwise a later identical frame would wrongly
    // look "unchanged" even though the device never received it.
    m_lastFrame = m_currentFrame;
    m_haveLastFrame = true;

    // The palette is always this fixed set (background + the 3 bar tiers) -
    // fixed indices, known up front, rather than discovered while looping.
    // IMPORTANT: the color table has to be set BEFORE any setPixel() calls -
    // QImage validates each index against whatever table size exists at
    // call time, which is 0 until setColorTable() runs, so calling it last
    // (as before) rejected every non-zero index with "Index out of range".
    QImage frame(m_matrixSize, QImage::Format_Indexed8);

    QHash<QString, int> paletteIndex;
    QVector<QRgb> palette;
    auto addColor = [&](const QString &hex) {
        if (!paletteIndex.contains(hex)) {
            paletteIndex.insert(hex, palette.size());
            palette.append(QColor("#" + hex).rgb());
        }
    };
    addColor(backgroundColor);
    addColor(m_lowColor);
    addColor(m_midColor);
    addColor(m_highColor);

    frame.setColorTable(palette);
    frame.fill(paletteIndex.value(backgroundColor));

    for (auto it = m_currentFrame.constBegin(); it != m_currentFrame.constEnd(); ++it) {
        const QPoint &p = it.key();
        if (p.x() < 0 || p.y() < 0 || p.x() >= frame.width() || p.y() >= frame.height())
            continue;

        // Any color not in the fixed set above falls back to the
        // background index rather than being silently dropped.
        frame.setPixel(p, paletteIndex.value(it.value(), 0));
    }
    sendFrame(frame);
}