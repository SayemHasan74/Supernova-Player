#include "UI/MainWindow/MainWindow.h"

#include "Core/Logger.h"
#include "Mpv/MpvCore.h"
#include "Mpv/MpvVideoSurface.h"

#include <QCloseEvent>
#include <QIcon>
#include <QKeyEvent>

#include <utility>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_mpvCore(std::make_unique<MpvCore>())
{
    setupWindowChrome();

    connect(m_mpvCore.get(), &MpvCore::mpvLogMessage, this,
            [](const QString &prefix, const QString &level,
               const QString &text) {
                const QString message =
                    QStringLiteral("mpv[%1/%2] %3")
                        .arg(prefix, level, text.trimmed());
                if (level == QStringLiteral("fatal")
                    || level == QStringLiteral("error")) {
                    Logger::error(message);
                } else {
                    Logger::warn(message);
                }
            });
    connect(m_mpvCore.get(), &MpvCore::mpvShutdown,
            this, &QWidget::close);
}

MainWindow::~MainWindow()
{
    if (QWidget *surface = takeCentralWidget()) {
        delete surface;
    }
    m_videoSurface = nullptr;
    m_mpvCore.reset();
}

void MainWindow::openMedia(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_pendingMediaPath = path;
    loadPendingMedia();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        m_mpvCore->command(
            {QStringLiteral("cycle"), QStringLiteral("pause")});
        event->accept();
        return;
    case Qt::Key_Left:
        m_mpvCore->command(
            {QStringLiteral("seek"), QStringLiteral("-5"),
             QStringLiteral("relative+exact")});
        event->accept();
        return;
    case Qt::Key_Right:
        m_mpvCore->command(
            {QStringLiteral("seek"), QStringLiteral("5"),
             QStringLiteral("relative+exact")});
        event->accept();
        return;
    default:
        QMainWindow::keyPressEvent(event);
        return;
    }
}

void MainWindow::setupWindowChrome()
{
    setWindowTitle(QStringLiteral("Supernova"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/supernova.ico")));
    setMinimumSize(480, 270);

    m_videoSurface = new MpvVideoSurface(m_mpvCore.get(), this);
    connect(m_videoSurface, &MpvVideoSurface::renderContextReady,
            this, &MainWindow::loadPendingMedia);
    setCentralWidget(m_videoSurface);
}

void MainWindow::loadPendingMedia()
{
    if (m_pendingMediaPath.isEmpty()
        || !m_videoSurface
        || !m_videoSurface->isRenderContextReady()) {
        return;
    }

    const QString path = std::exchange(m_pendingMediaPath, {});
    m_mpvCore->command(
        {QStringLiteral("loadfile"), path, QStringLiteral("replace")});
    Logger::info(QStringLiteral("Loading media: %1").arg(path));
}
