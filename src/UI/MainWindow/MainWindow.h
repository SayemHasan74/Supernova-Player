#pragma once

#include <QMainWindow>
#include <QList>
#include <QUrl>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class MpvVideoSurface;
class PlayerCore;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(PlayerCore *playerCore, QWidget *parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool isRenderContextReady() const noexcept;

signals:
    void renderContextReady();
    void openUrlsRequested(const QList<QUrl> &urls);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void beginShutdown();
    void openFiles();
    void openFolder();
    void requestOpen(const QList<QUrl> &urls);
    void setupMenus();
    void setupWindowChrome();

    PlayerCore *m_playerCore = nullptr;
    MpvVideoSurface *m_videoSurface = nullptr;
    QString m_lastOpenDirectory;
    bool m_closePending = false;
};
