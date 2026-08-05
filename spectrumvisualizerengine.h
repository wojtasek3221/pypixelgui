// spectrumvisualizerengine.h
#pragma once
#include <QObject>
#include <QVector>
#include <QHash>
#include <QPoint>
#include <QImage>
#include <QSize>
#include <functional>

// Renders spectrum band levels (from SpectrumAnalyzer) into a small
// framebuffer as vertical bars, diffs the result against the last frame
// that was actually SENT, and only calls sendFrame() when something
// changed - same approach as WatchfaceEngine, and for the same reason:
//
// pypixelcolor's set_pixel is fire-and-forget over BLE (requires_ack=False).
// A visualizer redrawing 11 bars every tick would mean dozens of unacked
// writes per frame, which real BLE links silently drop under load - the
// display would fall behind with no way to detect it. Sending the whole
// matrix as one image goes through the acknowledged send_image_hex
// transfer instead, and the diff means most bandsReady() ticks where the
// bars haven't visibly moved don't transmit anything at all.
class SpectrumVisualizerEngine : public QObject
{
    Q_OBJECT
public:
    explicit SpectrumVisualizerEngine(QObject *parent = nullptr);

    // Size of the physical LED matrix. Defaults to 32x32, same default as
    // WatchfaceEngine - call this with the real dimensions if you have them
    // (e.g. from get_device_info) before the first render().
    void setMatrixSize(int width, int height);

    void setBandCount(int bands);  // must match the size of the levels vector passed to render()
    void setMaxLevel(int level);   // the level value (e.g. 15) that fills a bar to full height
    void setBarColors(const QString &lowColor, const QString &midColor, const QString &highColor);

    // How much of the remaining distance to the target level each bar
    // closes per render() call (0..1). Lower = smoother/slower fade,
    // higher = snappier. This does NOT add any delay between sends - it
    // only changes how far a bar moves on each frame that was going to be
    // sent anyway, so a burst of quick sends reads as a fade instead of a
    // jump. Default 0.45 settles within ~4-5 sends.
    void setEaseFactor(float factor);

    // Draws bars for the given target levels into the framebuffer, easing
    // the actually-displayed level toward them (see setEaseFactor), diffs
    // the result against the last frame actually sent, and calls
    // sendFrame() only if the pixels differ. Safe to call on every
    // bandsReady tick - most calls will be no-ops on the wire.
    void render(const QVector<int> &levels);

    // Called at most once per render(), and only when the drawn pixels
    // differ from the last frame that was sent. Delivers the WHOLE matrix
    // as one image (see class comment above for why).
    std::function<void(const QImage &frame)> sendFrame;

    // Color used behind the bars.
    QString backgroundColor = "000000";

private:
    int m_bandCount = 11;
    int m_maxLevel = 15;
    QSize m_matrixSize{32, 32};
    QString m_lowColor = "00ff00";
    QString m_midColor = "ffff00";
    QString m_highColor = "ff0000";

    // Framebuffer state - same shape as WatchfaceEngine's, deliberately.
    QHash<QPoint, QString> m_currentFrame;
    QHash<QPoint, QString> m_lastFrame;
    bool m_haveLastFrame = false;

    // Per-band level actually drawn/sent so far, eased toward the latest
    // target level on each render() call rather than snapping to it.
    QVector<float> m_displayLevels;
    float m_easeFactor = 0.45f;

    QString colorForBar(int barHeightPx) const;
    void flushFrame(); // diffs m_currentFrame against m_lastFrame; if changed, builds one image and calls sendFrame()
};