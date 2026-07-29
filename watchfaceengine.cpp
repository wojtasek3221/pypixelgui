// watchfaceengine.cpp
#include "watchfaceengine.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <cstdio>

WatchfaceEngine::WatchfaceEngine(QObject *parent) : QObject(parent)
{
    initBuiltinFont();
}

void WatchfaceEngine::setPixel(int x, int y, const QString &color)
{
    m_currentFrame[QPoint(x, y)] = color;
}

bool WatchfaceEngine::loadWatchface(const QString &jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open watchface file:" << jsonPath;
        return false;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "Watchface JSON error:" << err.errorString();
        return false;
    }
    m_watchface = doc.object();
    m_imageCache.clear();

    // A new face may be a totally different layout - don't diff against the
    // old one, just force a full resend on the next frame.
    m_currentFrame.clear();
    m_lastFrame.clear();
    m_haveLastFrame = false;

    setMatrixSize(m_watchface.value("width").toInt(m_matrixSize.width()),
                  m_watchface.value("height").toInt(m_matrixSize.height()));

    return true;
}

void WatchfaceEngine::setMatrixSize(int width, int height)
{
    m_matrixSize = QSize(qMax(1, width), qMax(1, height));
}

// ---- Resolve "bind" + "format"/"value" into a printable string ----
QString WatchfaceEngine::resolveValue(const QJsonObject &el, const QDateTime &dt) const
{
    if (el.contains("value"))
        return el.value("value").toString();

    const QString bind = el.value("bind").toString();
    int num = 0;

    if (bind == "hour") num = dt.time().hour();
    else if (bind == "hour12") { num = dt.time().hour() % 12; if (num == 0) num = 12; }
    else if (bind == "minute") num = dt.time().minute();
    else if (bind == "second") num = dt.time().second();
    else if (bind == "dow") num = dt.date().dayOfWeek();   // 1=Mon .. 7=Sun
    else if (bind == "dom") num = dt.date().day();
    else if (bind == "month") num = dt.date().month();
    else return QString();

    const QString fmt = el.value("format").toString();
    if (fmt.isEmpty())
        return QString::number(num);

    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt.toUtf8().constData(), num);
    return QString::fromUtf8(buf);
}

// ---- Render one full frame ----
void WatchfaceEngine::renderFrame(const QDateTime &dt)
{
    if (!sendFrame) {
        qWarning() << "sendFrame callback not set";
        return;
    }

    m_currentFrame.clear(); // start a fresh logical frame

    const QJsonArray elements = m_watchface.value("elements").toArray();
    for (const QJsonValue &v : elements) {
        const QJsonObject el = v.toObject();
        const QString type = el.value("type").toString();
        const QString text = resolveValue(el, dt);
        int x = el.value("x").toInt();
        const int y = el.value("y").toInt();

        if (type == "text") {
            const QString color = el.value("color").toString("ffffff");
            drawText(text, x, y, color);
        } else if (type == "image") {
            if (el.contains("src")) {
                drawStaticImage(el.value("src").toString(), x, y);
            } else {
                for (const QChar &ch : text) {
                    int advance = 0;
                    drawImageChar(ch, x, y, el, advance);
                    x += advance;
                }
            }
        }
    }

    flushFrame();
}

// ---- Text rendering via built-in font ----
void WatchfaceEngine::drawText(const QString &text, int x, int y, const QString &color)
{
    int cursorX = x;
    for (const QChar &ch : text) {
        if (!m_font5x7.contains(ch)) { cursorX += 6; continue; }
        const QVector<QString> &rows = m_font5x7[ch];
        for (int row = 0; row < rows.size(); ++row) {
            const QString &line = rows[row];
            for (int col = 0; col < line.size(); ++col) {
                if (line[col] == '1')
                    setPixel(cursorX + col, y + row, color);
            }
        }
        cursorX += 6;
    }
}

// ---- Image-based "digit as picture" rendering ----
QImage WatchfaceEngine::loadImage(const QString &path)
{
    if (m_imageCache.contains(path))
        return m_imageCache[path];
    QImage img(path);
    m_imageCache.insert(path, img);
    return img;
}

void WatchfaceEngine::drawStaticImage(const QString &path, int x, int y)
{
    QImage img = loadImage(path);
    if (img.isNull()) {
        qWarning() << "Missing watchface image:" << path;
        return;
    }
    for (int py = 0; py < img.height(); ++py) {
        for (int px = 0; px < img.width(); ++px) {
            QColor c = img.pixelColor(px, py);
            if (c.alpha() == 0) continue;
            QString hex = c.name(QColor::HexRgb).mid(1);
            setPixel(x + px, y + py, hex);
        }
    }
}

void WatchfaceEngine::drawImageChar(const QChar &ch, int x, int y, const QJsonObject &el, int &advanceX)
{
    QString path;

    if (el.contains("images")) {
        const QJsonObject map = el.value("images").toObject();
        path = map.value(QString(ch)).toString();
    } else if (el.contains("imagePattern")) {
        path = el.value("imagePattern").toString().arg(ch);
    }

    if (path.isEmpty()) { advanceX = 0; return; }

    QImage img = loadImage(path);
    if (img.isNull()) {
        qWarning() << "Missing watchface image:" << path;
        advanceX = 0;
        return;
    }

    for (int py = 0; py < img.height(); ++py) {
        for (int px = 0; px < img.width(); ++px) {
            QColor c = img.pixelColor(px, py);
            if (c.alpha() == 0) continue; // transparent = skip pixel
            QString hex = c.name(QColor::HexRgb).mid(1); // "ffffff" without '#'
            setPixel(x + px, y + py, hex); // was calling sendPixel directly, bypassing the frame buffer
        }
    }
    advanceX = img.width() + 1; // 1px spacing between images
}

void WatchfaceEngine::flushFrame()
{
    // Work out whether anything actually changed since the last frame, so we
    // don't retransmit an identical image every tick.
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

    m_lastFrame = m_currentFrame;
    m_haveLastFrame = true;

    if (!changed || !sendFrame)
        return;

    // IMPORTANT: this used to call sendPixel() once per changed pixel. The
    // underlying pypixelcolor set_pixel command is sent with
    // requires_ack=False (fire-and-forget, unlike send_text/send_image,
    // which use an acknowledged, chunked transfer). A digit change can mean
    // 20-30+ of those unacknowledged writes fired back-to-back with no
    // pacing, which real BLE links will silently drop under load - and we
    // have no way to detect the drop, so the display can permanently fall
    // behind while this engine's internal state keeps advancing every tick.
    // Sending the whole matrix as a single image instead routes through the
    // same acknowledged protocol as the "Send Image" button.
    QImage frame(m_matrixSize, QImage::Format_RGB32);
    frame.fill(QColor("#" + backgroundColor));
    for (auto it = m_currentFrame.constBegin(); it != m_currentFrame.constEnd(); ++it) {
        const QPoint &p = it.key();
        if (p.x() < 0 || p.y() < 0 || p.x() >= frame.width() || p.y() >= frame.height())
            continue;
        frame.setPixelColor(p, QColor("#" + it.value()));
    }
    sendFrame(frame);
}

// ---- Minimal built-in 5x7 font (extend as needed) ----
void WatchfaceEngine::initBuiltinFont()
{
    m_font5x7['0'] = { "01110","10001","10011","10101","11001","10001","01110" };
    m_font5x7['1'] = { "00100","01100","00100","00100","00100","00100","01110" };
    m_font5x7['2'] = { "01110","10001","00001","00010","00100","01000","11111" };
    m_font5x7['3'] = { "11111","00010","00100","00010","00001","10001","01110" };
    m_font5x7['4'] = { "00010","00110","01010","10010","11111","00010","00010" };
    m_font5x7['5'] = { "11111","10000","11110","00001","00001","10001","01110" };
    m_font5x7['6'] = { "00110","01000","10000","11110","10001","10001","01110" };
    m_font5x7['7'] = { "11111","00001","00010","00100","01000","01000","01000" };
    m_font5x7['8'] = { "01110","10001","10001","01110","10001","10001","01110" };
    m_font5x7['9'] = { "01110","10001","10001","01111","00001","00010","01100" };
    m_font5x7[':'] = { "00000","00100","00000","00000","00100","00000","00000" };
    // add letters/space as needed for day names, etc.
}