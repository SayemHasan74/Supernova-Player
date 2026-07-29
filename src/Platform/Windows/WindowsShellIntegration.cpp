#include "Platform/Windows/WindowsShellIntegration.h"

#include "App/MediaSourceResolver.h"
#include "Core/Logger.h"
#include "PlayerCore/PlayerCore.h"
#include "UI/MainWindow/MainWindow.h"

#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <appmodel.h>
#include <propkey.h>
#include <propvarutil.h>
#include <roapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <systemmediatransportcontrolsinterop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#endif

namespace {
constexpr wchar_t applicationId[] =
    L"SupernovaProject.Supernova.Player";
constexpr UINT taskbarPrevious = 0x5301;
constexpr UINT taskbarPlayPause = 0x5302;
constexpr UINT taskbarNext = 0x5303;

QString mediaTitle(const PlayerCore *player)
{
    QString title =
        player->mpvPropertyString(QStringLiteral("media-title")).trimmed();
    if (!title.isEmpty()) {
        return title;
    }
    const QUrl url = player->info().currentUrl;
    return url.isLocalFile()
        ? QFileInfo(url.toLocalFile()).completeBaseName()
        : !url.fileName().isEmpty() ? url.fileName() : url.host();
}

bool systemMediaControlsEnabled()
{
    return QSettings().value(
        QStringLiteral("windows/systemMediaControls"), true).toBool();
}

bool preventSleepEnabled()
{
    return QSettings().value(
        QStringLiteral("windows/preventSleep"), true).toBool();
}

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;
namespace wf = winrt::Windows::Foundation;
namespace wm = winrt::Windows::Media;
namespace ws = winrt::Windows::Storage::Streams;

wf::TimeSpan timeSpan(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    return std::chrono::duration_cast<wf::TimeSpan>(
        std::chrono::duration<double>(seconds));
}

HICON createTransportIcon(int kind)
{
    constexpr int extent = 32;
    QImage image(extent, extent, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 255, 255), 2.4,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 255, 255));
    QPainterPath path;
    if (kind == 0 || kind == 3) {
        const bool next = kind == 3;
        const qreal direction = next ? 1.0 : -1.0;
        path.moveTo(16.0 - direction * 6.0, 9.0);
        path.lineTo(16.0 + direction * 3.5, 16.0);
        path.lineTo(16.0 - direction * 6.0, 23.0);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawLine(
            QPointF(16.0 + direction * 7.0, 9.0),
            QPointF(16.0 + direction * 7.0, 23.0));
    } else if (kind == 1) {
        path.moveTo(11.0, 8.0);
        path.lineTo(24.0, 16.0);
        path.lineTo(11.0, 24.0);
        path.closeSubpath();
        painter.drawPath(path);
    } else {
        painter.drawRoundedRect(QRectF(9, 8, 5, 16), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(18, 8, 5, 16), 1.5, 1.5);
    }
    painter.end();

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = extent;
    header.bV5Height = -extent;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(
        screen, reinterpret_cast<BITMAPINFO *>(&header),
        DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) {
        if (color) {
            DeleteObject(color);
        }
        return nullptr;
    }
    std::memcpy(
        bits, image.constBits(),
        static_cast<size_t>(image.sizeInBytes()));
    HBITMAP mask = CreateBitmap(extent, extent, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void setLinkTitle(IShellLinkW *link, const QString &title)
{
    ComPtr<IPropertyStore> store;
    if (!link || FAILED(link->QueryInterface(IID_PPV_ARGS(&store)))) {
        return;
    }
    PROPVARIANT value;
    if (SUCCEEDED(InitPropVariantFromString(
            reinterpret_cast<const wchar_t *>(title.utf16()), &value))) {
        store->SetValue(PKEY_Title, value);
        store->Commit();
        PropVariantClear(&value);
    }
}

ComPtr<IShellLinkW> shellLink(
    const QString &title, const QString &arguments)
{
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&link)))) {
        return {};
    }
    const QString executable = QCoreApplication::applicationFilePath();
    link->SetPath(
        reinterpret_cast<const wchar_t *>(executable.utf16()));
    link->SetArguments(
        reinterpret_cast<const wchar_t *>(arguments.utf16()));
    link->SetIconLocation(
        reinterpret_cast<const wchar_t *>(executable.utf16()), 0);
    link->SetDescription(
        reinterpret_cast<const wchar_t *>(title.utf16()));
    setLinkTitle(link.Get(), title);
    return link;
}

bool setRegistryString(
    HKEY root, const QString &path, const QString &name,
    const QString &value, QString *error)
{
    HKEY key = nullptr;
    const LSTATUS opened = RegCreateKeyExW(
        root, reinterpret_cast<const wchar_t *>(path.utf16()),
        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
        nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        if (error) {
            *error = QStringLiteral("Could not create registry key %1 (%2)")
                         .arg(path).arg(opened);
        }
        return false;
    }
    const wchar_t *nameValue = name.isEmpty()
        ? nullptr : reinterpret_cast<const wchar_t *>(name.utf16());
    const DWORD bytes =
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS written = RegSetValueExW(
        key, nameValue, 0, REG_SZ,
        reinterpret_cast<const BYTE *>(value.utf16()), bytes);
    RegCloseKey(key);
    if (written != ERROR_SUCCESS && error) {
        *error = QStringLiteral("Could not write registry key %1 (%2)")
                     .arg(path).arg(written);
    }
    return written == ERROR_SUCCESS;
}
#endif
}

class WindowsShellIntegration::Implementation : public QObject {
public:
    explicit Implementation(WindowsShellIntegration *owner)
        : QObject(owner), m_owner(owner)
    {
#ifdef Q_OS_WIN
        const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
        m_uninitializeRuntime =
            initialized == S_OK || initialized == S_FALSE;
        SetCurrentProcessExplicitAppUserModelID(applicationId);
        CoCreateInstance(
            CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_taskbar));
        if (m_taskbar) {
            m_taskbar->HrInit();
        }
        m_taskbarCreatedMessage =
            RegisterWindowMessageW(L"TaskbarButtonCreated");

        REASON_CONTEXT reason{};
        reason.Version = POWER_REQUEST_CONTEXT_VERSION;
        reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        reason.Reason.SimpleReasonString =
            const_cast<PWSTR>(L"Supernova playback is in progress");
        m_powerRequest = PowerCreateRequest(&reason);

        m_systemMediaSetting = systemMediaControlsEnabled();
        m_preventSleepSetting = preventSleepEnabled();
        m_allowDisplaySleepForAudioSetting =
            QSettings().value(
                QStringLiteral("windows/allowDisplaySleepForAudio"),
                true).toBool();
        m_jumpListSetting = QSettings().value(
            QStringLiteral("windows/jumpList"), true).toBool();
        auto *settingsTimer = new QTimer(this);
        settingsTimer->setInterval(1000);
        connect(settingsTimer, &QTimer::timeout,
                this, &Implementation::refreshSettings);
        settingsTimer->start();
#endif
    }

    ~Implementation() override
    {
#ifdef Q_OS_WIN
        clearPowerRequests();
        if (m_powerRequest && m_powerRequest != INVALID_HANDLE_VALUE) {
            CloseHandle(m_powerRequest);
        }
        for (const auto &session : m_sessions) {
            disableSystemMedia(*session);
            destroyIcons(*session);
        }
        if (m_uninitializeRuntime) {
            RoUninitialize();
        }
#endif
    }

    struct Session {
        QPointer<PlayerCore> player;
        QPointer<MainWindow> window;
#ifdef Q_OS_WIN
        HWND hwnd = nullptr;
        wm::SystemMediaTransportControls controls{nullptr};
        winrt::event_token buttonToken{};
        winrt::event_token positionToken{};
        bool systemMediaReady = false;
        bool taskbarButtonsAdded = false;
        HICON previousIcon = nullptr;
        HICON playIcon = nullptr;
        HICON pauseIcon = nullptr;
        HICON nextIcon = nullptr;
        bool taskbarError = false;
        quint64 artworkGeneration = 0;
#endif
    };

    void registerPlayer(PlayerCore *player, MainWindow *window)
    {
        if (!player || !window) {
            return;
        }
        auto session = std::make_unique<Session>();
        session->player = player;
        session->window = window;
#ifdef Q_OS_WIN
        session->hwnd = reinterpret_cast<HWND>(window->winId());
        initializeSystemMedia(*session);
        initializeTaskbar(*session);
#endif
        Session *registered = session.get();
        m_sessions.push_back(std::move(session));
        window->installEventFilter(this);

        const auto refresh = [this, registered] {
            if (!contains(registered)) {
                return;
            }
            updatePowerRequests();
#ifdef Q_OS_WIN
            updateTaskbar(*registered);
            if (registered == m_active) {
                updateSystemMedia(*registered, false);
            }
#endif
        };
        connect(player, &PlayerCore::stateChanged,
                this, [refresh](PlayerState) { refresh(); });
        connect(player, &PlayerCore::positionChanged,
                this, [refresh](double) { refresh(); });
        connect(player, &PlayerCore::durationChanged,
                this, [refresh](double) { refresh(); });
        connect(player, &PlayerCore::bufferingChanged,
                this, [refresh](const BufferingInfo &) { refresh(); });
        connect(player, &PlayerCore::playlistChanged,
                this, [this, registered](const PlaylistState &) {
                    if (!contains(registered)) {
                        return;
                    }
#ifdef Q_OS_WIN
                    if (registered == m_active) {
                        updateSystemMedia(*registered, true);
                    }
#endif
                });
        connect(player, &PlayerCore::currentUrlChanged,
                this, [this, registered](const QUrl &) {
                    if (!contains(registered)) {
                        return;
                    }
#ifdef Q_OS_WIN
                    if (registered == m_active) {
                        updateSystemMedia(*registered, true);
                    }
#endif
                });
        connect(player, &PlayerCore::mediaLoaded,
                this, [this, registered](const QUrl &) {
                    if (!contains(registered)) {
                        return;
                    }
                    setActive(registered);
#ifdef Q_OS_WIN
                    updateSystemMedia(*registered, true);
#endif
                    updatePowerRequests();
                });
        connect(player, &PlayerCore::thumbnailsChanged,
                this, [this, registered] {
                    if (!contains(registered)) {
                        return;
                    }
#ifdef Q_OS_WIN
                    if (registered == m_active) {
                        updateSystemMedia(*registered, true);
                    }
#endif
                });
        connect(player, &PlayerCore::playbackError,
                this, [this, registered](
                          const QString &, bool) {
                    if (!contains(registered)) {
                        return;
                    }
#ifdef Q_OS_WIN
                    registered->taskbarError = true;
                    updateTaskbar(*registered);
                    QTimer::singleShot(3000, this, [this, registered] {
                        if (contains(registered)) {
                            registered->taskbarError = false;
                            updateTaskbar(*registered);
                        }
                    });
#endif
                });
        connect(player, &PlayerCore::recentMediaChanged,
                this, [this](const QList<RecentMediaEntry> &recent) {
#ifdef Q_OS_WIN
                    updateJumpList(recent);
#else
                    Q_UNUSED(recent)
#endif
                });
        connect(window, &QObject::destroyed, this, [this, registered] {
            remove(registered);
        });

        setActive(registered);
#ifdef Q_OS_WIN
        updateJumpList(player->recentMedia());
#endif
        QTimer::singleShot(250, this, [this, registered] {
            if (contains(registered)) {
#ifdef Q_OS_WIN
                initializeTaskbar(*registered);
                updateTaskbar(*registered);
#endif
            }
        });
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::WindowActivate) {
            for (const auto &session : m_sessions) {
                if (session->window == watched) {
                    setActive(session.get());
                    break;
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

    bool nativeEvent(
        void *message, qintptr *result)
    {
#ifdef Q_OS_WIN
        auto *native = static_cast<MSG *>(message);
        if (!native) {
            return false;
        }
        Session *session = forWindow(native->hwnd);
        if (!session) {
            return false;
        }
        if (native->message == m_taskbarCreatedMessage) {
            session->taskbarButtonsAdded = false;
            initializeTaskbar(*session);
            updateTaskbar(*session);
            return false;
        }
        if (native->message == WM_COMMAND
            && HIWORD(native->wParam) == THBN_CLICKED) {
            const UINT command = LOWORD(native->wParam);
            if (command == taskbarPrevious) {
                session->player->navigateInPlaylist(false);
            } else if (command == taskbarPlayPause) {
                session->player->togglePause();
            } else if (command == taskbarNext) {
                session->player->navigateInPlaylist(true);
            } else {
                return false;
            }
            if (result) {
                *result = 0;
            }
            return true;
        }
        if (native->message == WM_APPCOMMAND
            && (!session->systemMediaReady
                || !systemMediaControlsEnabled())) {
            const int command = GET_APPCOMMAND_LPARAM(native->lParam);
            switch (command) {
            case APPCOMMAND_MEDIA_PLAY_PAUSE:
                session->player->togglePause();
                break;
            case APPCOMMAND_MEDIA_PLAY:
                session->player->resume();
                break;
            case APPCOMMAND_MEDIA_PAUSE:
                session->player->pause();
                break;
            case APPCOMMAND_MEDIA_STOP:
                session->player->stop();
                break;
            case APPCOMMAND_MEDIA_NEXTTRACK:
                session->player->navigateInPlaylist(true);
                break;
            case APPCOMMAND_MEDIA_PREVIOUSTRACK:
                session->player->navigateInPlaylist(false);
                break;
            case APPCOMMAND_MEDIA_FAST_FORWARD:
                session->player->seekRelative(15.0, true);
                break;
            case APPCOMMAND_MEDIA_REWIND:
                session->player->seekRelative(-15.0, true);
                break;
            default:
                return false;
            }
            if (result) {
                *result = 1;
            }
            return true;
        }
#else
        Q_UNUSED(message)
        Q_UNUSED(result)
#endif
        return false;
    }

private:
    bool contains(const Session *candidate) const
    {
        return std::any_of(
            m_sessions.cbegin(), m_sessions.cend(),
            [candidate](const auto &session) {
                return session.get() == candidate;
            });
    }

    void setActive(Session *session)
    {
        if (!session || m_active == session) {
            return;
        }
#ifdef Q_OS_WIN
        if (m_active) {
            disableSystemMedia(*m_active);
        }
#endif
        m_active = session;
#ifdef Q_OS_WIN
        initializeSystemMedia(*session);
        updateSystemMedia(*session, true);
#endif
    }

    void remove(Session *session)
    {
        if (!contains(session)) {
            return;
        }
#ifdef Q_OS_WIN
        disableSystemMedia(*session);
        destroyIcons(*session);
#endif
        if (m_active == session) {
            m_active = nullptr;
        }
        std::erase_if(
            m_sessions,
            [session](const auto &entry) {
                return entry.get() == session;
            });
        if (!m_active && !m_sessions.empty()) {
            setActive(m_sessions.back().get());
        }
        updatePowerRequests();
    }

    void updatePowerRequests()
    {
#ifdef Q_OS_WIN
        bool requireSystem = false;
        bool requireDisplay = false;
        if (preventSleepEnabled()) {
            for (const auto &session : m_sessions) {
                if (!session->player
                    || session->player->info().state
                           != PlayerState::Playing) {
                    continue;
                }
                requireSystem = true;
                const bool audioOnly =
                    session->player->info().hasAudio
                    && !session->player->info().hasVideo;
                const bool allowDisplaySleepForAudio =
                    QSettings().value(
                        QStringLiteral(
                            "windows/allowDisplaySleepForAudio"),
                        true).toBool();
                if (!audioOnly || !allowDisplaySleepForAudio) {
                    requireDisplay = true;
                }
            }
        }
        setPowerRequest(
            PowerRequestSystemRequired, requireSystem,
            m_systemPowerRequested);
        setPowerRequest(
            PowerRequestDisplayRequired, requireDisplay,
            m_displayPowerRequested);
#endif
    }

#ifdef Q_OS_WIN
    void refreshSettings()
    {
        const bool systemMedia = systemMediaControlsEnabled();
        const bool preventSleep = preventSleepEnabled();
        const bool allowDisplaySleepForAudio =
            QSettings().value(
                QStringLiteral("windows/allowDisplaySleepForAudio"),
                true).toBool();
        const bool jumpList = QSettings().value(
            QStringLiteral("windows/jumpList"), true).toBool();
        if (systemMedia != m_systemMediaSetting) {
            m_systemMediaSetting = systemMedia;
            if (m_active) {
                updateSystemMedia(*m_active, true);
            }
        }
        if (preventSleep != m_preventSleepSetting
            || allowDisplaySleepForAudio
                   != m_allowDisplaySleepForAudioSetting) {
            m_preventSleepSetting = preventSleep;
            m_allowDisplaySleepForAudioSetting =
                allowDisplaySleepForAudio;
            updatePowerRequests();
        }
        if (jumpList != m_jumpListSetting) {
            m_jumpListSetting = jumpList;
            updateJumpList(
                m_active && m_active->player
                    ? m_active->player->recentMedia()
                    : QList<RecentMediaEntry>{});
        }
    }

    Session *forWindow(HWND hwnd) const
    {
        const auto found = std::find_if(
            m_sessions.cbegin(), m_sessions.cend(),
            [hwnd](const auto &session) {
                return session->hwnd == hwnd;
            });
        return found == m_sessions.cend() ? nullptr : found->get();
    }

    void initializeSystemMedia(Session &session)
    {
        if (session.systemMediaReady || !session.hwnd) {
            return;
        }
        try {
            auto interop =
                winrt::get_activation_factory<
                    wm::SystemMediaTransportControls,
                    ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(interop->GetForWindow(
                session.hwnd,
                winrt::guid_of<wm::SystemMediaTransportControls>(),
                winrt::put_abi(session.controls)));
            QPointer<PlayerCore> player = session.player;
            session.buttonToken = session.controls.ButtonPressed(
                [player](
                    const wm::SystemMediaTransportControls &,
                    const wm::SystemMediaTransportControlsButtonPressedEventArgs
                        &args) {
                    QMetaObject::invokeMethod(
                        qApp, [player, button = args.Button()] {
                            if (!player) {
                                return;
                            }
                            switch (button) {
                            case wm::SystemMediaTransportControlsButton::Play:
                                player->resume();
                                break;
                            case wm::SystemMediaTransportControlsButton::Pause:
                                player->pause();
                                break;
                            case wm::SystemMediaTransportControlsButton::Stop:
                                player->stop();
                                break;
                            case wm::SystemMediaTransportControlsButton::Next:
                                player->navigateInPlaylist(true);
                                break;
                            case wm::SystemMediaTransportControlsButton::Previous:
                                player->navigateInPlaylist(false);
                                break;
                            case wm::SystemMediaTransportControlsButton::FastForward:
                                player->seekRelative(15.0, true);
                                break;
                            case wm::SystemMediaTransportControlsButton::Rewind:
                                player->seekRelative(-15.0, true);
                                break;
                            default:
                                break;
                            }
                        }, Qt::QueuedConnection);
                });
            session.positionToken =
                session.controls.PlaybackPositionChangeRequested(
                    [player](
                        const wm::SystemMediaTransportControls &,
                        const wm::PlaybackPositionChangeRequestedEventArgs
                            &args) {
                        const double seconds =
                            std::chrono::duration<double>(
                                args.RequestedPlaybackPosition()).count();
                        QMetaObject::invokeMethod(
                            qApp, [player, seconds] {
                                if (player) {
                                    player->seekAbsolute(seconds);
                                }
                            }, Qt::QueuedConnection);
                    });
            session.systemMediaReady = true;
        } catch (const winrt::hresult_error &error) {
            Logger::warn(QStringLiteral(
                "Windows system media controls unavailable: 0x%1")
                .arg(static_cast<quint32>(error.code()), 8, 16,
                     QLatin1Char('0')));
            session.controls = nullptr;
        }
    }

    void disableSystemMedia(Session &session)
    {
        if (!session.systemMediaReady || !session.controls) {
            return;
        }
        try {
            session.controls.IsEnabled(false);
            session.controls.PlaybackStatus(
                wm::MediaPlaybackStatus::Closed);
        } catch (...) {
        }
    }

    void updateSystemMedia(Session &session, bool updateMetadata)
    {
        if (!session.player || !session.controls
            || !session.systemMediaReady) {
            return;
        }
        const PlaybackInfo &info = session.player->info();
        const bool active = isActive(info.state)
            && !info.currentUrl.isEmpty();
        try {
            session.controls.IsEnabled(
                systemMediaControlsEnabled() && active);
            if (!systemMediaControlsEnabled() || !active) {
                ++session.artworkGeneration;
                session.controls.PlaybackStatus(
                    wm::MediaPlaybackStatus::Closed);
                session.controls.DisplayUpdater().ClearAll();
                session.controls.DisplayUpdater().Update();
                return;
            }
            session.controls.IsPlayEnabled(true);
            session.controls.IsPauseEnabled(true);
            session.controls.IsStopEnabled(true);
            session.controls.IsNextEnabled(
                info.playlist.currentIndex >= 0
                && info.playlist.currentIndex + 1
                       < info.playlist.size());
            session.controls.IsPreviousEnabled(
                info.playlist.currentIndex > 0);
            session.controls.IsFastForwardEnabled(
                info.videoDurationSec > 0.0);
            session.controls.IsRewindEnabled(
                info.videoDurationSec > 0.0);
            session.controls.PlaybackStatus(
                info.state == PlayerState::Playing
                    ? wm::MediaPlaybackStatus::Playing
                    : info.state == PlayerState::Paused
                    ? wm::MediaPlaybackStatus::Paused
                    : info.state == PlayerState::Loading
                          || info.state == PlayerState::Starting
                    ? wm::MediaPlaybackStatus::Changing
                    : wm::MediaPlaybackStatus::Stopped);

            wm::SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(timeSpan(0.0));
            timeline.MinSeekTime(timeSpan(0.0));
            timeline.Position(timeSpan(info.videoPositionSec));
            timeline.MaxSeekTime(timeSpan(info.videoDurationSec));
            timeline.EndTime(timeSpan(info.videoDurationSec));
            session.controls.UpdateTimelineProperties(timeline);

            if (updateMetadata) {
                const quint64 artworkGeneration =
                    ++session.artworkGeneration;
                auto updater = session.controls.DisplayUpdater();
                updater.ClearAll();
                const bool audioOnly =
                    info.hasAudio && !info.hasVideo;
                updater.Type(
                    audioOnly
                        ? wm::MediaPlaybackType::Music
                        : wm::MediaPlaybackType::Video);
                const QString title = mediaTitle(session.player);
                if (audioOnly) {
                    auto music = updater.MusicProperties();
                    music.Title(title.toStdWString());
                    music.Artist(
                        session.player->mpvPropertyString(
                            QStringLiteral(
                                "metadata/by-key/artist"))
                            .toStdWString());
                    music.AlbumTitle(
                        session.player->mpvPropertyString(
                            QStringLiteral(
                                "metadata/by-key/album"))
                            .toStdWString());
                } else {
                    updater.VideoProperties().Title(
                        title.toStdWString());
                }
                const QImage artwork =
                    session.player->thumbnailAt(0.0);
                updater.Update();
                if (!artwork.isNull()) {
                    QByteArray bytes;
                    QBuffer buffer(&bytes);
                    if (buffer.open(QIODevice::WriteOnly)
                        && artwork.save(&buffer, "PNG")) {
                        updateArtworkAsync(
                            &session, artworkGeneration,
                            std::move(bytes));
                    }
                }
            }
        } catch (const winrt::hresult_error &error) {
            Logger::warn(QStringLiteral(
                "Could not update Windows media session: 0x%1")
                .arg(static_cast<quint32>(error.code()), 8, 16,
                     QLatin1Char('0')));
        }
    }

    winrt::fire_and_forget updateArtworkAsync(
        Session *session, quint64 generation, QByteArray bytes)
    {
        QPointer<Implementation> guarded(this);
        try {
            ws::InMemoryRandomAccessStream stream;
            ws::DataWriter writer(stream);
            writer.WriteBytes(winrt::array_view<const uint8_t>(
                reinterpret_cast<const uint8_t *>(bytes.constData()),
                reinterpret_cast<const uint8_t *>(
                    bytes.constData() + bytes.size())));
            co_await writer.StoreAsync();
            writer.DetachStream();
            stream.Seek(0);
            const auto reference =
                ws::RandomAccessStreamReference::CreateFromStream(
                    stream);
            if (!guarded || !guarded->contains(session)
                || guarded->m_active != session
                || session->artworkGeneration != generation
                || !session->controls
                || !systemMediaControlsEnabled()) {
                co_return;
            }
            auto updater = session->controls.DisplayUpdater();
            updater.Thumbnail(reference);
            updater.Update();
        } catch (const winrt::hresult_error &error) {
            if (guarded) {
                Logger::warn(QStringLiteral(
                    "Could not update Windows media artwork: 0x%1")
                    .arg(static_cast<quint32>(error.code()), 8, 16,
                         QLatin1Char('0')));
            }
        }
    }

    void initializeTaskbar(Session &session)
    {
        if (!m_taskbar || !session.hwnd
            || session.taskbarButtonsAdded) {
            return;
        }
        if (!session.previousIcon) {
            session.previousIcon = createTransportIcon(0);
            session.playIcon = createTransportIcon(1);
            session.pauseIcon = createTransportIcon(2);
            session.nextIcon = createTransportIcon(3);
        }
        THUMBBUTTON buttons[3]{};
        const UINT ids[] = {
            taskbarPrevious, taskbarPlayPause, taskbarNext};
        HICON icons[] = {
            session.previousIcon, session.playIcon, session.nextIcon};
        const wchar_t *tips[] = {
            L"Previous media", L"Play or pause", L"Next media"};
        for (int index = 0; index < 3; ++index) {
            buttons[index].dwMask =
                THB_ICON | THB_TOOLTIP | THB_FLAGS;
            buttons[index].iId = ids[index];
            buttons[index].hIcon = icons[index];
            buttons[index].dwFlags = THBF_ENABLED;
            wcsncpy_s(
                buttons[index].szTip, tips[index],
                _TRUNCATE);
        }
        const HRESULT added = m_taskbar->ThumbBarAddButtons(
            session.hwnd, 3, buttons);
        session.taskbarButtonsAdded =
            SUCCEEDED(added) || added == E_INVALIDARG;
    }

    void updateTaskbar(Session &session)
    {
        if (!m_taskbar || !session.hwnd || !session.player) {
            return;
        }
        const PlaybackInfo &info = session.player->info();
        if (session.taskbarError) {
            m_taskbar->SetProgressState(
                session.hwnd, TBPF_ERROR);
        } else if (info.buffering.active) {
            m_taskbar->SetProgressState(
                session.hwnd, TBPF_INDETERMINATE);
        } else if (info.videoDurationSec > 0.0
                   && isLoaded(info.state)) {
            m_taskbar->SetProgressValue(
                session.hwnd,
                static_cast<ULONGLONG>(
                    std::max(0.0, info.videoPositionSec) * 1000.0),
                static_cast<ULONGLONG>(
                    info.videoDurationSec * 1000.0));
            m_taskbar->SetProgressState(
                session.hwnd,
                info.state == PlayerState::Paused
                    ? TBPF_PAUSED : TBPF_NORMAL);
        } else {
            m_taskbar->SetProgressState(
                session.hwnd, TBPF_NOPROGRESS);
        }
        if (!session.taskbarButtonsAdded) {
            initializeTaskbar(session);
        }
        if (session.taskbarButtonsAdded) {
            THUMBBUTTON buttons[3]{};
            const bool playing = info.state == PlayerState::Playing;
            const bool loaded = isLoaded(info.state);
            const bool hasPrevious =
                info.playlist.currentIndex > 0;
            const bool hasNext =
                info.playlist.currentIndex >= 0
                && info.playlist.currentIndex + 1
                       < info.playlist.size();
            const UINT ids[] = {
                taskbarPrevious, taskbarPlayPause, taskbarNext};
            HICON icons[] = {
                session.previousIcon,
                playing ? session.pauseIcon : session.playIcon,
                session.nextIcon};
            const wchar_t *tips[] = {
                L"Previous media",
                playing ? L"Pause" : L"Play",
                L"Next media"};
            const bool enabled[] = {
                hasPrevious, loaded, hasNext};
            for (int index = 0; index < 3; ++index) {
                buttons[index].dwMask =
                    THB_ICON | THB_TOOLTIP | THB_FLAGS;
                buttons[index].iId = ids[index];
                buttons[index].hIcon = icons[index];
                buttons[index].dwFlags =
                    enabled[index] ? THBF_ENABLED : THBF_DISABLED;
                wcsncpy_s(
                    buttons[index].szTip, tips[index],
                    _TRUNCATE);
            }
            m_taskbar->ThumbBarUpdateButtons(
                session.hwnd, 3, buttons);
        }
    }

    void destroyIcons(Session &session)
    {
        for (HICON icon : {
                 session.previousIcon, session.playIcon,
                 session.pauseIcon, session.nextIcon}) {
            if (icon) {
                DestroyIcon(icon);
            }
        }
        session.previousIcon = nullptr;
        session.playIcon = nullptr;
        session.pauseIcon = nullptr;
        session.nextIcon = nullptr;
    }

    void updateJumpList(const QList<RecentMediaEntry> &recent)
    {
        if (!QSettings().value(
                QStringLiteral("windows/jumpList"), true).toBool()) {
            ComPtr<ICustomDestinationList> oldList;
            if (SUCCEEDED(CoCreateInstance(
                    CLSID_DestinationList, nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&oldList)))) {
                oldList->DeleteList(applicationId);
            }
            return;
        }
        ComPtr<ICustomDestinationList> destination;
        if (FAILED(CoCreateInstance(
                CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&destination)))) {
            return;
        }
        destination->SetAppID(applicationId);
        UINT destinationSlots = 0;
        ComPtr<IObjectArray> removed;
        if (FAILED(destination->BeginList(
                &destinationSlots, IID_PPV_ARGS(&removed)))) {
            return;
        }
        QStringList removedArguments;
        if (removed) {
            UINT removedCount = 0;
            removed->GetCount(&removedCount);
            for (UINT index = 0; index < removedCount; ++index) {
                ComPtr<IShellLinkW> removedLink;
                if (FAILED(removed->GetAt(
                        index, IID_PPV_ARGS(&removedLink)))) {
                    continue;
                }
                wchar_t arguments[4096]{};
                if (SUCCEEDED(removedLink->GetArguments(
                        arguments, static_cast<int>(
                            std::size(arguments))))) {
                    removedArguments.push_back(
                        QString::fromWCharArray(arguments).trimmed());
                }
            }
        }
        const auto wasRemoved =
            [&removedArguments](const QString &arguments) {
                return removedArguments.contains(
                    arguments.trimmed(), Qt::CaseInsensitive);
            };

        ComPtr<IObjectCollection> recentItems;
        CoCreateInstance(
            CLSID_EnumerableObjectCollection, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&recentItems));
        const int maximum =
            std::min<int>(
                static_cast<int>(destinationSlots), recent.size());
        for (int index = 0; index < maximum; ++index) {
            const QUrl &url = recent[index].url;
            if (!url.isLocalFile()
                || !QFileInfo::exists(url.toLocalFile())) {
                continue;
            }
            const QString argument =
                QStringLiteral("\"%1\"").arg(
                    QDir::toNativeSeparators(url.toLocalFile()));
            if (wasRemoved(argument)) {
                continue;
            }
            auto link = shellLink(
                QFileInfo(url.toLocalFile()).fileName(), argument);
            if (link) {
                recentItems->AddObject(link.Get());
            }
            SHAddToRecentDocs(
                SHARD_PATHW,
                reinterpret_cast<const wchar_t *>(
                    QDir::toNativeSeparators(
                        url.toLocalFile()).utf16()));
        }
        ComPtr<IObjectArray> recentArray;
        recentItems.As(&recentArray);
        UINT recentCount = 0;
        recentArray->GetCount(&recentCount);
        if (recentCount > 0) {
            destination->AppendCategory(
                L"Recent Media", recentArray.Get());
        }

        ComPtr<IObjectCollection> tasks;
        CoCreateInstance(
            CLSID_EnumerableObjectCollection, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tasks));
        for (const auto &[title, arguments] :
             std::initializer_list<std::pair<QString, QString>>{
                 {QStringLiteral("Open Supernova"), QString()},
                 {QStringLiteral("New Player"),
                  QStringLiteral("--new-window")}}) {
            if (wasRemoved(arguments)) {
                continue;
            }
            auto link = shellLink(title, arguments);
            if (link) {
                tasks->AddObject(link.Get());
            }
        }
        ComPtr<IObjectArray> taskArray;
        tasks.As(&taskArray);
        destination->AddUserTasks(taskArray.Get());
        if (FAILED(destination->CommitList())) {
            destination->AbortList();
        }
    }

    void setPowerRequest(
        POWER_REQUEST_TYPE type, bool enabled, bool &current)
    {
        if (!m_powerRequest || m_powerRequest == INVALID_HANDLE_VALUE
            || enabled == current) {
            return;
        }
        const BOOL succeeded = enabled
            ? PowerSetRequest(m_powerRequest, type)
            : PowerClearRequest(m_powerRequest, type);
        if (succeeded) {
            current = enabled;
        } else {
            Logger::warn(QStringLiteral(
                "Windows power request change failed (%1)")
                .arg(GetLastError()));
        }
    }

    void clearPowerRequests()
    {
        setPowerRequest(
            PowerRequestDisplayRequired, false,
            m_displayPowerRequested);
        setPowerRequest(
            PowerRequestSystemRequired, false,
            m_systemPowerRequested);
    }
#endif

    WindowsShellIntegration *m_owner = nullptr;
    std::vector<std::unique_ptr<Session>> m_sessions;
    Session *m_active = nullptr;
#ifdef Q_OS_WIN
    ComPtr<ITaskbarList3> m_taskbar;
    UINT m_taskbarCreatedMessage = 0;
    HANDLE m_powerRequest = nullptr;
    bool m_systemPowerRequested = false;
    bool m_displayPowerRequested = false;
    bool m_uninitializeRuntime = false;
    bool m_systemMediaSetting = true;
    bool m_preventSleepSetting = true;
    bool m_allowDisplaySleepForAudioSetting = true;
    bool m_jumpListSetting = true;
#endif
};

WindowsShellIntegration::WindowsShellIntegration(QObject *parent)
    : QObject(parent),
      m_implementation(std::make_unique<Implementation>(this))
{
    if (qApp) {
        qApp->installNativeEventFilter(this);
    }
}

WindowsShellIntegration::~WindowsShellIntegration()
{
    if (qApp) {
        qApp->removeNativeEventFilter(this);
    }
}

void WindowsShellIntegration::registerPlayer(
    PlayerCore *player, MainWindow *window)
{
    m_implementation->registerPlayer(player, window);
}

bool WindowsShellIntegration::nativeEventFilter(
    const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    return m_implementation->nativeEvent(message, result);
}

bool WindowsFileAssociations::isRegistered()
{
#ifdef Q_OS_WIN
    HKEY key = nullptr;
    const LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\RegisteredApplications", 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[512]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LSTATUS read = RegQueryValueExW(
        key, L"Supernova", nullptr, &type,
        reinterpret_cast<BYTE *>(value), &bytes);
    RegCloseKey(key);
    return read == ERROR_SUCCESS && type == REG_SZ;
#else
    return false;
#endif
}

bool WindowsFileAssociations::registerCurrentExecutable(QString *error)
{
#ifdef Q_OS_WIN
    const QString executable =
        QDir::toNativeSeparators(
            QCoreApplication::applicationFilePath());
    const QString quotedExecutable =
        QStringLiteral("\"%1\"").arg(executable);
    const QString capabilities =
        QStringLiteral(
            "Software\\Supernova Project\\Supernova\\Capabilities");
    bool ok =
        setRegistryString(
            HKEY_CURRENT_USER,
            QStringLiteral("Software\\Classes\\Supernova.Media"),
            {}, QStringLiteral("Supernova media file"), error)
        && setRegistryString(
            HKEY_CURRENT_USER,
            QStringLiteral(
                "Software\\Classes\\Supernova.Media\\DefaultIcon"),
            {}, QStringLiteral("%1,0").arg(quotedExecutable), error)
        && setRegistryString(
            HKEY_CURRENT_USER,
            QStringLiteral(
                "Software\\Classes\\Supernova.Media\\shell\\open\\command"),
            {}, QStringLiteral("%1 \"%2\"")
                    .arg(quotedExecutable, QStringLiteral("%1")),
            error)
        && setRegistryString(
            HKEY_CURRENT_USER, capabilities,
            QStringLiteral("ApplicationName"),
            QStringLiteral("Supernova"), error)
        && setRegistryString(
            HKEY_CURRENT_USER, capabilities,
            QStringLiteral("ApplicationDescription"),
            QStringLiteral("Supernova media player"), error)
        && setRegistryString(
            HKEY_CURRENT_USER,
            QStringLiteral("Software\\RegisteredApplications"),
            QStringLiteral("Supernova"), capabilities, error);
    if (!ok) {
        return false;
    }
    QStringList extensions =
        MediaSourceResolver::supportedMediaExtensions();
    extensions.append(
        MediaSourceResolver::supportedPlaylistExtensions());
    extensions.removeDuplicates();
    for (const QString &extension : std::as_const(extensions)) {
        const QString dotted = extension.startsWith(QLatin1Char('.'))
            ? extension : QStringLiteral(".%1").arg(extension);
        ok = setRegistryString(
                 HKEY_CURRENT_USER,
                 capabilities + QStringLiteral("\\FileAssociations"),
                 dotted, QStringLiteral("Supernova.Media"), error)
             && setRegistryString(
                 HKEY_CURRENT_USER,
                 QStringLiteral(
                     "Software\\Classes\\Applications\\Supernova.exe"
                     "\\SupportedTypes"),
                 dotted, QString(), error);
        if (!ok) {
            return false;
        }
    }
    SHChangeNotify(
        SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#else
    if (error) {
        *error = QStringLiteral(
            "File association registration is available on Windows only.");
    }
    return false;
#endif
}

bool WindowsFileAssociations::unregisterCurrentUser(QString *error)
{
#ifdef Q_OS_WIN
    const QStringList keys{
        QStringLiteral(
            "Software\\Supernova Project\\Supernova\\Capabilities"),
        QStringLiteral("Software\\Classes\\Supernova.Media"),
        QStringLiteral(
            "Software\\Classes\\Applications\\Supernova.exe")};
    for (const QString &path : keys) {
        const LSTATUS removed = RegDeleteTreeW(
            HKEY_CURRENT_USER,
            reinterpret_cast<const wchar_t *>(path.utf16()));
        if (removed != ERROR_SUCCESS
            && removed != ERROR_FILE_NOT_FOUND) {
            if (error) {
                *error = QStringLiteral(
                    "Could not remove registry key %1 (%2)")
                    .arg(path).arg(removed);
            }
            return false;
        }
    }
    HKEY registered = nullptr;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\RegisteredApplications", 0,
            KEY_SET_VALUE, &registered) == ERROR_SUCCESS) {
        RegDeleteValueW(registered, L"Supernova");
        RegCloseKey(registered);
    }
    SHChangeNotify(
        SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#else
    Q_UNUSED(error)
    return false;
#endif
}

void WindowsFileAssociations::openDefaultAppsSettings()
{
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("ms-settings:defaultapps")));
#endif
}
