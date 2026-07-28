#pragma once

#include <QDialog>

class PlayerCore;
class QComboBox;
class QTableWidget;
class QTabWidget;
class QTimer;

class MediaInspector final : public QDialog {
    Q_OBJECT

public:
    explicit MediaInspector(
        PlayerCore *playerCore, QWidget *parent = nullptr);
    void showInspector(int tab = 0);

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void refreshAll();
    void refreshGeneral();
    void refreshMedia();
    void refreshTracks();
    void refreshSelectedTrack();
    void refreshStatistics();
    void refreshWatch();
    void addWatchProperty();
    void removeWatchProperties();
    [[nodiscard]] QString propertyText(const QString &name) const;
    static void setRows(
        QTableWidget *table,
        const QList<QPair<QString, QString>> &rows);

    PlayerCore *m_playerCore = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_general = nullptr;
    QTableWidget *m_media = nullptr;
    QComboBox *m_track = nullptr;
    QTableWidget *m_trackDetails = nullptr;
    QTableWidget *m_statistics = nullptr;
    QTableWidget *m_watch = nullptr;
    QTimer *m_timer = nullptr;
};

