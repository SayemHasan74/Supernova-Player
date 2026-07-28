#include "UI/Subtitles/OnlineSubtitleDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

OnlineSubtitleDialog::OnlineSubtitleDialog(QWidget *parent)
    : QDialog(parent),
      m_service(new OnlineSubtitleService(this))
{
    setObjectName(QStringLiteral("onlineSubtitleDialog"));
    setWindowTitle(tr("Find Online Subtitles"));
    setModal(false);
    resize(780, 470);

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_provider = new QComboBox(this);
    m_provider->addItem(
        QStringLiteral("OpenSubtitles.com"),
        static_cast<int>(OnlineSubtitleProvider::OpenSubtitles));
    m_provider->addItem(
        QStringLiteral("Assrt.net"),
        static_cast<int>(OnlineSubtitleProvider::Assrt));
    m_provider->addItem(
        QStringLiteral("Shooter.cn"),
        static_cast<int>(OnlineSubtitleProvider::Shooter));
    const int savedProvider = QSettings().value(
        QStringLiteral("onlineSubtitles/provider"), 0).toInt();
    m_provider->setCurrentIndex(
        std::clamp(savedProvider, 0, m_provider->count() - 1));
    m_query = new QLineEdit(this);
    m_query->setClearButtonEnabled(true);
    m_languages = new QLineEdit(
        QSettings().value(
            QStringLiteral("onlineSubtitles/languages"),
            QStringLiteral("en")).toString(), this);
    m_languages->setPlaceholderText(tr("en,fr,de"));
    m_search = new QPushButton(tr("Search"), this);
    auto *queryRow = new QHBoxLayout;
    queryRow->addWidget(m_query, 1);
    queryRow->addWidget(m_search);
    form->addRow(tr("Provider"), m_provider);
    form->addRow(tr("Title / filename"), queryRow);
    form->addRow(tr("Languages"), m_languages);
    root->addLayout(form);

    m_results = new QTableWidget(0, 6, this);
    m_results->setHorizontalHeaderLabels({
        tr("Subtitle"), tr("Language"), tr("Format"),
        tr("Details"), tr("Downloads"), tr("Uploaded")});
    m_results->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1; column < 6; ++column) {
        m_results->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setAlternatingRowColors(true);
    root->addWidget(m_results, 1);

    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(tr("Choose a provider and search."), this);
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);
    m_download = new QPushButton(tr("Download and Load"), this);
    m_download->setEnabled(false);
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_download);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(m_search, &QPushButton::clicked,
            this, &OnlineSubtitleDialog::startSearch);
    connect(m_query, &QLineEdit::returnPressed,
            this, &OnlineSubtitleDialog::startSearch);
    connect(m_download, &QPushButton::clicked,
            this, &OnlineSubtitleDialog::startDownload);
    connect(m_results, &QTableWidget::itemDoubleClicked,
            this, [this] { startDownload(); });
    connect(m_results, &QTableWidget::itemSelectionChanged,
            this, [this] {
        m_download->setEnabled(
            !m_results->selectionModel()->selectedRows().isEmpty());
    });
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    connect(m_service, &OnlineSubtitleService::statusChanged,
            m_status, &QLabel::setText);
    connect(m_service, &OnlineSubtitleService::searchFinished,
            this, [this](const QList<OnlineSubtitleResult> &results) {
        setBusy(false);
        showResults(results);
    });
    connect(m_service, &OnlineSubtitleService::failed,
            this, [this](const QString &message) {
        setBusy(false);
        m_status->setText(message);
        QMessageBox::warning(this, tr("Online Subtitles"), message);
    });
    connect(m_service, &OnlineSubtitleService::downloadFinished,
            this, [this](const QStringList &paths) {
        setBusy(false);
        emit subtitlesReady(paths);
    });

    setStyleSheet(QStringLiteral(
        "#onlineSubtitleDialog { background: rgb(29,30,34); color: rgb(239,239,244); }"
        "QLineEdit, QComboBox, QTableWidget { background: rgb(22,23,27);"
        " color: rgb(239,239,244); border: 1px solid rgba(255,255,255,32); }"
        "QHeaderView::section { background: rgb(43,44,50); color: rgb(225,225,232);"
        " border: 0; padding: 6px; }"
        "QTableWidget { alternate-background-color: rgb(34,35,40); }"
        "QPushButton { color: rgb(239,239,244); padding: 6px 12px;"
        " border: 1px solid rgba(255,255,255,34); border-radius: 5px;"
        " background: rgb(48,49,55); }"
        "QPushButton:hover { background: rgb(61,62,69); }"));
}

void OnlineSubtitleDialog::showForMedia(
    const QUrl &mediaUrl, const QString &mediaTitle)
{
    m_mediaUrl = mediaUrl;
    QString query = mediaUrl.isLocalFile()
        ? QFileInfo(mediaUrl.toLocalFile()).completeBaseName()
        : mediaTitle;
    if (query.isEmpty()) {
        query = mediaUrl.fileName();
    }
    m_query->setText(query);
    show();
    raise();
    activateWindow();
    m_query->setFocus();
    m_query->selectAll();
}

void OnlineSubtitleDialog::startSearch()
{
    const auto provider = static_cast<OnlineSubtitleProvider>(
        m_provider->currentData().toInt());
    QSettings settings;
    settings.setValue(
        QStringLiteral("onlineSubtitles/provider"),
        m_provider->currentIndex());
    settings.setValue(
        QStringLiteral("onlineSubtitles/languages"),
        m_languages->text().trimmed());
    QStringList languages = m_languages->text().split(
        QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &language : languages) {
        language = language.trimmed().toLower();
    }
    setBusy(true);
    m_items.clear();
    m_results->setRowCount(0);
    m_service->search(
        provider, m_mediaUrl, m_query->text().trimmed(), languages);
}

void OnlineSubtitleDialog::startDownload()
{
    const QModelIndexList selected =
        m_results->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;
    const int row = selected.constFirst().row();
    if (row < 0 || row >= m_items.size()) return;
    setBusy(true);
    m_service->download(m_items.at(row));
}

void OnlineSubtitleDialog::setBusy(bool busy)
{
    m_provider->setEnabled(!busy);
    m_query->setEnabled(!busy);
    m_languages->setEnabled(!busy);
    m_search->setEnabled(!busy);
    m_results->setEnabled(!busy);
    m_download->setEnabled(
        !busy && !m_results->selectionModel()->selectedRows().isEmpty());
}

void OnlineSubtitleDialog::showResults(
    const QList<OnlineSubtitleResult> &results)
{
    m_items = results;
    m_results->setRowCount(results.size());
    for (int row = 0; row < results.size(); ++row) {
        const OnlineSubtitleResult &result = results.at(row);
        const QStringList values{
            result.title, result.language, result.format, result.details,
            result.downloads > 0 ? QString::number(result.downloads)
                                 : QString(),
            result.uploaded};
        for (int column = 0; column < values.size(); ++column) {
            m_results->setItem(
                row, column, new QTableWidgetItem(values.at(column)));
        }
    }
    if (!results.isEmpty()) {
        m_results->selectRow(0);
    }
}
