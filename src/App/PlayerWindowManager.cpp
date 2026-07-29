#include "App/PlayerWindowManager.h"

#include "PlayerCore/PlayerCore.h"
#include "UI/MainWindow/MainWindow.h"

#include <QApplication>
#include <QSize>
#include <QWidget>

#include <algorithm>
#include <utility>

struct PlayerWindowManager::Session {
    std::unique_ptr<PlayerCore> player;
    MainWindow *window = nullptr;
    QList<QUrl> pendingUrls;
};

PlayerWindowManager::PlayerWindowManager(QObject *parent)
    : QObject(parent)
{
}

PlayerWindowManager::~PlayerWindowManager() = default;

MainWindow *PlayerWindowManager::createPlayer(
    const QList<QUrl> &urls)
{
    auto session = std::make_unique<Session>();
    session->player = std::make_unique<PlayerCore>();
    session->window = new MainWindow(session->player.get());
    session->window->setAttribute(Qt::WA_DeleteOnClose);
    Session *sessionPtr = session.get();
    MainWindow *window = session->window;
    m_sessions.push_back(std::move(session));

    connect(
        window, &MainWindow::renderContextReady,
        this, [this, sessionPtr] {
            if (sessionPtr->pendingUrls.isEmpty()) {
                return;
            }
            openInSession(
                sessionPtr,
                std::exchange(sessionPtr->pendingUrls, {}));
        });
    connect(
        window, &MainWindow::openUrlsRequested,
        this, [this, sessionPtr](const QList<QUrl> &requested) {
            openInSession(sessionPtr, requested);
        });
    connect(
        window, &MainWindow::newPlayerRequested,
        this, [this](const QList<QUrl> &requested) {
            createPlayer(requested);
        });
    connect(
        window, &QObject::destroyed,
        this, [this, window] {
            removeSession(window);
        });

    window->resize(urls.isEmpty() ? QSize(640, 400)
                                  : QSize(1280, 720));
    window->show();
    if (!urls.isEmpty()) {
        openInSession(sessionPtr, urls);
    }
    return window;
}

void PlayerWindowManager::open(
    const QList<QUrl> &urls, bool forceNewWindow)
{
    if (forceNewWindow || m_sessions.empty()) {
        createPlayer(urls);
        return;
    }
    Session *session = activeSession();
    if (!session) {
        createPlayer(urls);
        return;
    }
    session->window->show();
    session->window->raise();
    session->window->activateWindow();
    openInSession(session, urls);
}

PlayerWindowManager::Session *
PlayerWindowManager::activeSession() const
{
    QWidget *active = QApplication::activeWindow();
    if (active) {
        const auto found = std::find_if(
            m_sessions.begin(), m_sessions.end(),
            [active](const std::unique_ptr<Session> &session) {
                return session->window == active;
            });
        if (found != m_sessions.end()) {
            return found->get();
        }
    }
    return m_sessions.empty() ? nullptr : m_sessions.back().get();
}

void PlayerWindowManager::openInSession(
    Session *session, const QList<QUrl> &urls)
{
    if (!session || urls.isEmpty()) {
        return;
    }
    if (!session->window->isRenderContextReady()) {
        session->pendingUrls = urls;
        return;
    }
    session->player->openUrls(urls);
}

void PlayerWindowManager::removeSession(MainWindow *window)
{
    std::erase_if(
        m_sessions,
        [window](const std::unique_ptr<Session> &session) {
            return session->window == window;
        });
}
