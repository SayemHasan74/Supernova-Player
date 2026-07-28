#pragma once

#include "Network/OnlineSubtitleService.h"

#include <QDialog>
#include <QList>
#include <QUrl>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class OnlineSubtitleDialog final : public QDialog {
    Q_OBJECT

public:
    explicit OnlineSubtitleDialog(QWidget *parent = nullptr);
    void showForMedia(const QUrl &mediaUrl, const QString &mediaTitle);

signals:
    void subtitlesReady(const QStringList &paths);

private:
    void startSearch();
    void startDownload();
    void setBusy(bool busy);
    void showResults(const QList<OnlineSubtitleResult> &results);

    OnlineSubtitleService *m_service = nullptr;
    QComboBox *m_provider = nullptr;
    QLineEdit *m_query = nullptr;
    QLineEdit *m_languages = nullptr;
    QPushButton *m_search = nullptr;
    QTableWidget *m_results = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_download = nullptr;
    QUrl m_mediaUrl;
    QList<OnlineSubtitleResult> m_items;
};
