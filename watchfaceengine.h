// watchfaceengine.h
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QImage>
#include <QMap>
#include <QPoint>
#include <QSize>
#include <functional>
#include <QHash>   // instead of / in addition to QMap

class WatchfaceEngine : public QObject
{
    Q_OBJECT
public:
    explicit WatchfaceEngine(QObject *parent = nullptr);

    bool loadWatchface(const QString &jsonPath);
    void renderFrame(const QDateTime &dt);

    // Called at most once per tick, and only when the rendered content
    // actually changed since the last frame. Delivers the WHOLE matrix as
    // one image, so it can go over an acknowledged transfer (e.g.
    // send_image_hex) instead of a burst of individual unacknowledged
    // pixel writes - see the comment in flushFrame() for why that matters.
    std::function<void(const QImage &frame)> sendFrame;

    // Size of the physical LED matrix. Defaults to 32x32; loadWatchface()
    // will pick this up from the JSON's top-level "width"/"height" if present.
    void setMatrixSize(int width, int height);

    // Color used to "clear" a pixel that's no longer part of the drawing.
    // Match whatever your display treats as off/background.
    QString backgroundColor = "000000";

private:
    QJsonObject m_watchface;
    QMap<QChar, QVector<QString>> m_font5x7;
    QMap<QString, QImage> m_imageCache;

    // framebuffer state
    QHash<QPoint, QString> m_currentFrame;
    QHash<QPoint, QString> m_lastFrame;
    bool m_haveLastFrame = false;
    QSize m_matrixSize{32, 32};

    void initBuiltinFont();
    QString resolveValue(const QJsonObject &el, const QDateTime &dt) const;

    // These write into m_currentFrame - never call sendFrame/sendPixel directly
    void drawText(const QString &text, int x, int y, const QString &color);
    void drawImageChar(const QChar &ch, int x, int y, const QJsonObject &el, int &advanceX);
    void setPixel(int x, int y, const QString &color); // writes to m_currentFrame
    void drawStaticImage(const QString &path, int x, int y);

    QImage loadImage(const QString &path);
    void flushFrame(); // diffs m_currentFrame against m_lastFrame; if changed, builds one image and calls sendFrame()
};