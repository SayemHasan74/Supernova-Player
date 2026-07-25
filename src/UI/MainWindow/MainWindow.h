#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

class QCloseEvent;
class QKeyEvent;
class MpvCore;
class MpvVideoSurface;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openMedia(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupWindowChrome();
    void loadPendingMedia();

    std::unique_ptr<MpvCore> m_mpvCore;
    MpvVideoSurface *m_videoSurface = nullptr;
    QString m_pendingMediaPath;
};
