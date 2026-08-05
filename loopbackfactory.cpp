#include "audioloopback.h"

#ifdef Q_OS_WIN
#include "windowsloopbackcapture.h"
#elif defined(Q_OS_LINUX)
#include "linuxloopbackcapture.h"
#endif

AudioLoopbackCapture *createLoopbackCapture(QObject *parent)
{
#ifdef Q_OS_WIN
    return new WindowsLoopbackCapture(parent);
#elif defined(Q_OS_LINUX)
    return new LinuxLoopbackCapture(parent);
#else
    Q_UNUSED(parent);
    return nullptr; // Unsupported platform
#endif
}