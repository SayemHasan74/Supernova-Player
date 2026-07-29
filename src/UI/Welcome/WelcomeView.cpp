#include "UI/Welcome/WelcomeView.h"

#include "App/MediaSourceResolver.h"
#include "PlayerCore/ThumbnailProvider.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString formatTime(double seconds)
{
    const int value = std::max(0, static_cast<int>(seconds));
    const int hours = value / 3600;
    const int minutes = (value % 3600) / 60;
    const int secs = value % 60;
    return hours > 0
        ? QStringLiteral("%1:%2:%3")
              .arg(hours)
              .arg(minutes, 2, 10, QLatin1Char('0'))
              .arg(secs, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2")
              .arg(minutes)
              .arg(secs, 2, 10, QLatin1Char('0'));
}

QString displayName(const PlaybackHistoryEntry &entry)
{
    return !entry.title.isEmpty() ? entry.title : entry.displayName;
}

QString previewKey(const QUrl &url)
{
    return PlaybackHistoryStore::keyForUrl(url);
}

QPixmap previewPixmap(const QImage &image)
{
    constexpr QSize extent(36, 22);
    QPixmap pixmap(extent);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), extent), 3, 3);
    painter.setClipPath(clip);
    if (!image.isNull()) {
        const QImage scaled = image.scaled(
            extent, Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        painter.drawImage(
            QRect(QPoint(0, 0), extent),
            scaled,
            QRect(
                (scaled.width() - extent.width()) / 2,
                (scaled.height() - extent.height()) / 2,
                extent.width(), extent.height()));
    } else {
        painter.fillRect(
            QRect(QPoint(0, 0), extent),
            QColor(255, 255, 255, 18));
        painter.setPen(QPen(QColor(235, 235, 245, 105), 1));
        painter.drawRoundedRect(
            QRectF(0.5, 0.5, extent.width() - 1,
                   extent.height() - 1),
            3, 3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(235, 235, 245, 150));
        QPainterPath play;
        play.moveTo(15, 6);
        play.lineTo(24, 11);
        play.lineTo(15, 16);
        play.closeSubpath();
        painter.drawPath(play);
    }
    return pixmap;
}

class BrandMark final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient gradient(10, 12, width() - 10, height() - 12);
        gradient.setColorAt(0.0, QColor(52, 102, 255));
        gradient.setColorAt(0.52, QColor(31, 210, 236));
        gradient.setColorAt(1.0, QColor(29, 139, 255));
        QPainterPath triangle;
        triangle.moveTo(width() * 0.32, height() * 0.20);
        triangle.lineTo(width() * 0.78, height() * 0.50);
        triangle.lineTo(width() * 0.32, height() * 0.80);
        triangle.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawPath(triangle);
        painter.setBrush(QColor(79, 55, 255, 210));
        painter.drawRoundedRect(
            QRectF(width() * 0.14, height() * 0.36,
                   width() * 0.08, height() * 0.28),
            3, 3);
        painter.drawRoundedRect(
            QRectF(width() * 0.24, height() * 0.29,
                   width() * 0.07, height() * 0.42),
            3, 3);
    }
};

class RecentRow final : public QWidget {
public:
    RecentRow(
        const PlaybackHistoryEntry &entry, const QImage &preview,
        QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *line = new QHBoxLayout(this);
        line->setContentsMargins(9, 4, 2, 4);
        line->setSpacing(7);
        m_preview = new QLabel(this);
        m_preview->setPixmap(previewPixmap(preview));
        m_preview->setFixedSize(36, 22);
        auto *title = new QLabel(displayName(entry), this);
        title->setStyleSheet(QStringLiteral("color: rgb(241,241,246);"));
        title->setToolTip(entry.url.toDisplayString());
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        line->addWidget(m_preview);
        line->addWidget(title, 1);
    }

    void setPreview(const QImage &preview)
    {
        m_preview->setPixmap(previewPixmap(preview));
    }

private:
    QLabel *m_preview = nullptr;
};

class WelcomeActionButton final : public QPushButton {
public:
    using QPushButton::QPushButton;

    void setLabels(const QString &left, const QString &right)
    {
        setText({});
        setProperty("leftText", left);
        setProperty("rightText", right);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        const QRect textRect = rect().adjusted(10, 0, -8, 0);
        painter.setPen(QColor(243, 243, 247));
        painter.drawText(
            textRect, Qt::AlignLeft | Qt::AlignVCenter,
            property("leftText").toString());
        painter.setPen(QColor(235, 235, 245, 150));
        painter.drawText(
            textRect, Qt::AlignRight | Qt::AlignVCenter,
            property("rightText").toString());
    }
};
} // namespace

WelcomeView::WelcomeView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("welcomeView"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral(
        "#welcomeView { background: rgba(28,30,38,238); }"
        "QPushButton { color: rgb(243,243,247); text-align: left;"
        " padding: 0 10px; border: 0; border-radius: 6px;"
        " background: transparent; }"
        "QPushButton:hover { background: rgba(0,0,0,64); }"
        "QPushButton:pressed { background: rgba(0,0,0,89); }"
        "QListWidget { background: transparent; border: 0; outline: 0; }"
        "QListWidget::item { border: 0; border-radius: 6px; }"
        "QListWidget::item:hover { background: rgba(0,0,0,64); }"
        "QListWidget::item:selected { background: rgba(255,255,255,26); }"));

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *brand = new QFrame(this);
    brand->setFixedWidth(180);
    brand->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(13,15,21,225);"
        " border-right: 1px solid rgba(255,255,255,28); }"));
    auto *brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(25, 45, 25, 35);
    auto *mark = new BrandMark(brand);
    mark->setFixedSize(86, 86);
    brandLayout->addWidget(mark, 0, Qt::AlignHCenter);
    brandLayout->addStretch();
    auto *name = new QLabel(QStringLiteral("SUPERNOVA"), brand);
    name->setAlignment(Qt::AlignCenter);
    QFont nameFont = name->font();
    nameFont.setPixelSize(15);
    nameFont.setWeight(QFont::DemiBold);
    name->setFont(nameFont);
    name->setStyleSheet(QStringLiteral("color: rgb(242,242,247);"));
    auto *version = new QLabel(QStringLiteral("0.1.0"), brand);
    version->setAlignment(Qt::AlignCenter);
    version->setStyleSheet(
        QStringLiteral("color: rgba(235,235,245,150);"));
    brandLayout->addWidget(name);
    brandLayout->addWidget(version);
    brandLayout->addStretch();
    root->addWidget(brand);

    auto *actions = new QWidget(this);
    auto *layout = new QVBoxLayout(actions);
    layout->setContentsMargins(24, 24, 34, 22);
    layout->setSpacing(4);
    auto *openButton = new WelcomeActionButton(actions);
    openButton->setLabels(tr("Open…"), QStringLiteral("Ctrl+O"));
    auto *urlButton = new WelcomeActionButton(actions);
    urlButton->setLabels(
        tr("Open URL…"), QStringLiteral("Ctrl+Shift+O"));
    openButton->setFixedHeight(28);
    openButton->setObjectName(QStringLiteral("welcomeOpenButton"));
    urlButton->setFixedHeight(28);
    urlButton->setObjectName(QStringLiteral("welcomeUrlButton"));
    m_resumeButton = new WelcomeActionButton(actions);
    m_resumeButton->setObjectName(QStringLiteral("welcomeResumeButton"));
    m_resumeButton->setFixedHeight(32);
    m_resumeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: rgb(243,243,247); text-align: left;"
        " padding: 0 10px; border: 0; border-radius: 6px;"
        " background: rgba(255,255,255,26); }"
        "QPushButton:hover { background: rgba(128,128,128,26); }"
        "QPushButton:pressed { background: rgba(0,0,0,26); }"));
    m_resumeButton->hide();
    layout->addWidget(openButton);
    layout->addWidget(urlButton);
    layout->addSpacing(7);
    layout->addWidget(m_resumeButton);

    m_recentList = new QListWidget(actions);
    m_recentList->setObjectName(QStringLiteral("welcomeRecentList"));
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_recentList->installEventFilter(this);
    m_recentList->viewport()->installEventFilter(this);
    layout->addWidget(m_recentList, 1);
    m_historyButton = new WelcomeActionButton(actions);
    m_historyButton->setObjectName(
        QStringLiteral("welcomeHistoryButton"));
    static_cast<WelcomeActionButton *>(m_historyButton)->setLabels(
        tr("Playback History…"), QStringLiteral("Ctrl+H"));
    m_historyButton->setFixedHeight(28);
    m_historyButton->hide();
    layout->addWidget(m_historyButton);
    root->addWidget(actions, 1);

    connect(openButton, &QPushButton::clicked,
            this, &WelcomeView::openFileRequested);
    connect(urlButton, &QPushButton::clicked,
            this, &WelcomeView::openUrlRequested);
    connect(m_resumeButton, &QPushButton::clicked, this, [this] {
        if (!m_visibleHistory.isEmpty()) {
            emit historyRequested(m_visibleHistory.constFirst().url);
        }
    });
    connect(m_recentList, &QListWidget::itemActivated,
            this, [this](QListWidgetItem *item) {
                emit historyRequested(
                    item->data(Qt::UserRole).toUrl());
            });
    connect(m_recentList, &QListWidget::itemClicked,
            this, [this](QListWidgetItem *item) {
                emit historyRequested(
                    item->data(Qt::UserRole).toUrl());
            });
    connect(m_historyButton, &QPushButton::clicked,
            this, &WelcomeView::showHistoryRequested);
}

void WelcomeView::setHistory(
    const QList<PlaybackHistoryEntry> &history)
{
    m_history = history;
    rebuildMedia();
}

void WelcomeView::setRecentMedia(
    const QList<RecentMediaEntry> &recent)
{
    m_recentMedia = recent;
    m_hasRecentMedia = true;
    rebuildMedia();
}

void WelcomeView::rebuildMedia()
{
    m_visibleHistory.clear();
    for (const PlaybackHistoryEntry &entry : m_history) {
        if (entry.url.isLocalFile()
            && !QFileInfo::exists(entry.url.toLocalFile())) {
            continue;
        }
        m_visibleHistory.append(entry);
        if (m_visibleHistory.size() == 10) {
            break;
        }
    }
    m_recentList->clear();
    const bool hasResume = !m_visibleHistory.isEmpty();
    if (!hasResume) {
        m_resumeButton->hide();
    } else {
        const PlaybackHistoryEntry &latest =
            m_visibleHistory.constFirst();
        static_cast<WelcomeActionButton *>(m_resumeButton)->setLabels(
            tr("↶  Resume  %1").arg(displayName(latest)),
            formatTime(latest.positionSec));
        m_resumeButton->setToolTip(latest.url.toDisplayString());
        m_resumeButton->show();
    }
    m_historyButton->setVisible(!m_history.isEmpty());

    QList<PlaybackHistoryEntry> recentRows;
    if (m_hasRecentMedia) {
        const QString resumeKey = hasResume
            ? PlaybackHistoryStore::keyForUrl(
                  m_visibleHistory.constFirst().url)
            : QString();
        for (const RecentMediaEntry &recent : std::as_const(m_recentMedia)) {
            if (PlaybackHistoryStore::keyForUrl(recent.url) == resumeKey
                || (recent.url.isLocalFile()
                    && !QFileInfo::exists(recent.url.toLocalFile()))) {
                continue;
            }
            auto history = std::find_if(
                m_history.cbegin(), m_history.cend(),
                [&recent](const PlaybackHistoryEntry &entry) {
                    return PlaybackHistoryStore::keyForUrl(entry.url)
                        == PlaybackHistoryStore::keyForUrl(recent.url);
                });
            if (history != m_history.cend()) {
                recentRows.append(*history);
            } else {
                PlaybackHistoryEntry entry;
                entry.url = recent.url;
                entry.displayName = recent.url.isLocalFile()
                    ? QFileInfo(recent.url.toLocalFile()).fileName()
                    : recent.url.fileName();
                entry.lastPlayed = recent.openedAt;
                recentRows.append(entry);
            }
            if (recentRows.size() == (hasResume ? 9 : 10)) {
                break;
            }
        }
    } else {
        recentRows = m_visibleHistory.mid(1, 9);
    }

    for (const PlaybackHistoryEntry &entry : std::as_const(recentRows)) {
        auto *item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, entry.url);
        item->setToolTip(entry.url.toDisplayString());
        item->setSizeHint(QSize(0, 32));
        const QString key = previewKey(entry.url);
        const QImage preview = m_previews.value(key);
        m_recentList->setItemWidget(
            item, new RecentRow(entry, preview, m_recentList));
        if (preview.isNull()
            && entry.url.isLocalFile()
            && !MediaSourceResolver::isAudioFile(entry.url)) {
            requestPreview(entry.url);
        }
    }
    m_recentList->setCurrentItem(nullptr);
    m_recentList->clearSelection();
}

void WelcomeView::requestPreview(const QUrl &url)
{
    const QString key = previewKey(url);
    if (m_pendingPreviews.contains(key)
        || m_previews.contains(key)) {
        return;
    }
    m_pendingPreviews.insert(key);
    QPointer<WelcomeView> guarded(this);
    QThreadPool::globalInstance()->start([guarded, url] {
        const QImage image = ThumbnailProvider::previewFor(url, 160);
        if (!guarded) {
            return;
        }
        QMetaObject::invokeMethod(
            guarded,
            [guarded, url, image] {
                if (guarded) {
                    guarded->applyPreview(url, image);
                }
            },
            Qt::QueuedConnection);
    });
}

void WelcomeView::applyPreview(
    const QUrl &url, const QImage &image)
{
    const QString key = previewKey(url);
    m_pendingPreviews.remove(key);
    if (image.isNull()) {
        return;
    }
    m_previews.insert(key, image);
    for (int index = 0; index < m_recentList->count(); ++index) {
        QListWidgetItem *item = m_recentList->item(index);
        if (previewKey(item->data(Qt::UserRole).toUrl()) != key) {
            continue;
        }
        if (auto *row = dynamic_cast<RecentRow *>(
                m_recentList->itemWidget(item))) {
            row->setPreview(image);
        }
    }
}

bool WelcomeView::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_recentList
         || watched == m_recentList->viewport())
        && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return
            || key->key() == Qt::Key_Enter) {
            activateCurrentItem();
            return true;
        }
        if (key->key() == Qt::Key_Down
            && m_recentList->selectedItems().isEmpty()
            && m_recentList->count() > 0) {
            m_recentList->setCurrentRow(0);
            return true;
        }
        if (key->key() == Qt::Key_Up
            && m_resumeButton->isVisible()
            && m_recentList->currentRow() == 0) {
            m_recentList->setCurrentItem(nullptr);
            m_recentList->clearSelection();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WelcomeView::activateCurrentItem()
{
    if (QListWidgetItem *item = m_recentList->currentItem();
        item && item->isSelected()) {
        emit historyRequested(item->data(Qt::UserRole).toUrl());
    } else if (!m_visibleHistory.isEmpty()) {
        emit historyRequested(m_visibleHistory.constFirst().url);
    }
}
