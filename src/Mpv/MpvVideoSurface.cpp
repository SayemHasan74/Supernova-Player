#include "Mpv/MpvVideoSurface.h"

#include "Core/Logger.h"
#include "Mpv/MpvCore.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QThread>

#include <mpv/render_gl.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>

class SharedFramePool final {
public:
    enum class State {
        Available,
        Rendering,
        Ready,
        Displaying,
        Presented,
    };

    struct Slot {
        unsigned int framebuffer = 0;
        unsigned int texture = 0;
        State state = State::Available;
        std::uint64_t serial = 0;
    };

    static constexpr std::size_t BufferCount = 3;

    std::mutex mutex;
    std::condition_variable condition;
    std::array<Slot, BufferCount> buffers;
    int width = 0;
    int height = 0;
    std::uint64_t nextSerial = 0;
    std::uint64_t generation = 0;
    bool rebuilding = false;
};

class MpvRenderThread final : public QThread {
public:
    MpvRenderThread(
        MpvVideoSurface *videoSurface,
        QOpenGLContext *shareContext,
        const QSize &initialPixelSize)
        : m_videoSurface(videoSurface),
          m_targetPixelSize(initialPixelSize),
          m_context(std::make_unique<QOpenGLContext>()),
          m_surface(std::make_unique<QOffscreenSurface>())
    {
        m_context->setFormat(shareContext->format());
        m_context->setShareContext(shareContext);
        if (!m_context->create()) {
            throw std::runtime_error(
                "Could not create the video render OpenGL context");
        }

        m_surface->setFormat(m_context->format());
        m_surface->create();
        if (!m_surface->isValid()) {
            throw std::runtime_error(
                "Could not create the video render surface");
        }
    }

    void prepareForStart()
    {
        m_context->moveToThread(this);
        m_surface->moveToThread(this);
    }

    void requestFrame()
    {
        {
            const std::scoped_lock lock(m_requestMutex);
            m_framePending = true;
        }
        m_requestCondition.notify_one();
    }

    void requestResize(const QSize &pixelSize)
    {
        {
            const std::scoped_lock lock(m_requestMutex);
            m_targetPixelSize = pixelSize;
            m_resizePending = true;
            m_framePending = true;
        }
        m_requestCondition.notify_one();
    }

    void requestSwapReport()
    {
        {
            const std::scoped_lock lock(m_requestMutex);
            m_swapReportPending = true;
        }
        m_requestCondition.notify_one();
    }

    void requestStop()
    {
        {
            const std::scoped_lock lock(m_requestMutex);
            m_stopRequested = true;
        }
        m_requestCondition.notify_one();
    }

protected:
    void run() override
    {
        if (!m_context->makeCurrent(m_surface.get())) {
            const QString message =
                QStringLiteral(
                    "Could not activate the background video render context");
            Logger::error(message);
            QMetaObject::invokeMethod(
                m_videoSurface,
                [surface = m_videoSurface, message] {
                    emit surface->renderInitializationFailed(message);
                },
                Qt::QueuedConnection);
            returnObjectsToGuiThread();
            return;
        }

        m_videoSurface->createRenderContextOnWorker();
        if (!m_videoSurface->isRenderContextReady()) {
            m_context->doneCurrent();
            returnObjectsToGuiThread();
            return;
        }

        for (;;) {
            QSize targetSize;
            bool shouldResize = false;
            bool shouldRender = false;
            bool shouldReportSwap = false;
            {
                std::unique_lock lock(m_requestMutex);
                m_requestCondition.wait(
                    lock, [this] {
                        return m_framePending || m_resizePending
                               || m_swapReportPending
                               || m_stopRequested;
                    });
                if (m_stopRequested) {
                    break;
                }

                targetSize = m_targetPixelSize;
                shouldResize = m_resizePending;
                shouldRender = m_framePending;
                shouldReportSwap = m_swapReportPending;
                m_resizePending = false;
                m_framePending = false;
                m_swapReportPending = false;
            }

            if (shouldReportSwap) {
                m_videoSurface->reportSwapOnWorker();
            }
            if (shouldResize) {
                m_videoSurface->ensureWorkerFramebuffer(targetSize);
            }
            if (shouldRender) {
                m_videoSurface->renderFrameOnWorker(targetSize);
            }
        }

        m_videoSurface->destroyRenderContextOnWorker();
        m_context->doneCurrent();
        returnObjectsToGuiThread();
    }

private:
    void returnObjectsToGuiThread()
    {
        QThread *guiThread = QCoreApplication::instance()->thread();
        m_surface->moveToThread(guiThread);
        m_context->moveToThread(guiThread);
    }

    MpvVideoSurface *m_videoSurface = nullptr;
    QSize m_targetPixelSize;
    std::unique_ptr<QOpenGLContext> m_context;
    std::unique_ptr<QOffscreenSurface> m_surface;
    std::mutex m_requestMutex;
    std::condition_variable m_requestCondition;
    bool m_framePending = true;
    bool m_resizePending = true;
    bool m_swapReportPending = false;
    bool m_stopRequested = false;
};

MpvVideoSurface::MpvVideoSurface(MpvCore *core, QWidget *parent)
    : QOpenGLWidget(parent),
      m_core(core),
      m_framePool(std::make_unique<SharedFramePool>())
{
    if (!m_core) {
        throw std::invalid_argument("MpvVideoSurface requires an MpvCore");
    }

    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
}

MpvVideoSurface::~MpvVideoSurface()
{
    cleanupRenderContext();
}

void MpvVideoSurface::initializeGL()
{
    cleanupRenderContext();
    m_cleanupInProgress.store(false, std::memory_order_release);
    makeCurrent();

    QOpenGLContext *widgetContext = context();
    if (!widgetContext) {
        throw std::runtime_error("QOpenGLWidget has no OpenGL context");
    }

    QOpenGLFunctions *functions = widgetContext->functions();
    functions->initializeOpenGLFunctions();
    const auto *vendor = functions->glGetString(GL_VENDOR);
    const auto *renderer = functions->glGetString(GL_RENDERER);
    const auto *version = functions->glGetString(GL_VERSION);
    const QSurfaceFormat format = widgetContext->format();
    const auto glString = [](const GLubyte *value) {
        return value
            ? QString::fromLatin1(reinterpret_cast<const char *>(value))
            : QStringLiteral("unknown");
    };
    Logger::info(
        QStringLiteral("OpenGL %1.%2 (%3) — %4 / %5 / %6")
            .arg(format.majorVersion())
            .arg(format.minorVersion())
            .arg(widgetContext->isOpenGLES()
                     ? QStringLiteral("OpenGL ES")
                     : QStringLiteral("desktop"))
            .arg(glString(vendor), glString(renderer), glString(version)));

    m_contextDestroyConnection = connect(
        widgetContext, &QOpenGLContext::aboutToBeDestroyed,
        this, &MpvVideoSurface::cleanupRenderContext,
        Qt::DirectConnection);
    m_frameSwappedConnection = connect(
        this, &QOpenGLWidget::frameSwapped,
        this, &MpvVideoSurface::requestSwapReport,
        Qt::DirectConnection);

    const qreal scale = devicePixelRatioF();
    const QSize pixelSize(
        qMax(1, qRound(width() * scale)),
        qMax(1, qRound(height() * scale)));
    m_renderThread = std::make_unique<MpvRenderThread>(
        this, widgetContext, pixelSize);
    m_renderThread->prepareForStart();
    m_renderThread->start(QThread::HighPriority);
}

void MpvVideoSurface::paintGL()
{
    std::size_t selectedIndex = SharedFramePool::BufferCount;
    unsigned int texture = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(m_framePool->mutex);
        if (m_framePool->rebuilding) {
            return;
        }

        std::uint64_t newestSerial = 0;
        for (std::size_t index = 0;
             index < SharedFramePool::BufferCount; ++index) {
            const auto &slot = m_framePool->buffers[index];
            if (slot.state == SharedFramePool::State::Ready
                && (selectedIndex == SharedFramePool::BufferCount
                    || slot.serial > newestSerial)) {
                selectedIndex = index;
                newestSerial = slot.serial;
            }
        }

        if (selectedIndex != SharedFramePool::BufferCount) {
            for (auto &slot : m_framePool->buffers) {
                if (slot.state == SharedFramePool::State::Presented) {
                    slot.state = SharedFramePool::State::Available;
                }
            }
        } else {
            for (std::size_t index = 0;
                 index < SharedFramePool::BufferCount; ++index) {
                if (m_framePool->buffers[index].state
                    == SharedFramePool::State::Presented) {
                    selectedIndex = index;
                    break;
                }
            }
        }

        if (selectedIndex == SharedFramePool::BufferCount) {
            return;
        }

        auto &selected = m_framePool->buffers[selectedIndex];
        selected.state = SharedFramePool::State::Displaying;
        texture = selected.texture;
        sourceWidth = m_framePool->width;
        sourceHeight = m_framePool->height;
        generation = m_framePool->generation;
    }

    QOpenGLExtraFunctions *functions = context()->extraFunctions();
    functions->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    functions->glClear(GL_COLOR_BUFFER_BIT);

    if (texture == 0 || sourceWidth <= 0 || sourceHeight <= 0) {
        std::scoped_lock lock(m_framePool->mutex);
        auto &selected = m_framePool->buffers[selectedIndex];
        if (generation == m_framePool->generation
            && selected.state == SharedFramePool::State::Displaying) {
            selected.state = SharedFramePool::State::Available;
        }
        m_framePool->condition.notify_all();
        return;
    }

    if (m_readFramebuffer == 0) {
        functions->glGenFramebuffers(1, &m_readFramebuffer);
    }
    functions->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFramebuffer);
    functions->glFramebufferTexture2D(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, texture, 0);
    if (functions->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)
        != GL_FRAMEBUFFER_COMPLETE) {
        functions->glBindFramebuffer(
            GL_FRAMEBUFFER, defaultFramebufferObject());
    } else {
        const qreal scale = devicePixelRatioF();
        const int destinationWidth =
            qMax(1, qRound(width() * scale));
        const int destinationHeight =
            qMax(1, qRound(height() * scale));
        functions->glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
        functions->glBlitFramebuffer(
            0, 0, sourceWidth, sourceHeight,
            0, 0, destinationWidth, destinationHeight,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
        functions->glBindFramebuffer(
            GL_FRAMEBUFFER, defaultFramebufferObject());

        // The texture belongs to a shared worker context. Complete this small
        // copy before the slot becomes reusable so the worker can never
        // overwrite a texture that the GUI is still presenting.
        functions->glFinish();
    }

    {
        const std::scoped_lock lock(m_framePool->mutex);
        if (generation == m_framePool->generation) {
            auto &selected = m_framePool->buffers[selectedIndex];
            if (selected.state == SharedFramePool::State::Displaying) {
                selected.state = SharedFramePool::State::Presented;
            }
        }
    }
    m_framePool->condition.notify_all();
}

void MpvVideoSurface::resizeGL(int width, int height)
{
    if (!m_renderThread) {
        return;
    }
    const qreal scale = devicePixelRatioF();
    m_renderThread->requestResize(
        QSize(qMax(1, qRound(width * scale)),
              qMax(1, qRound(height * scale))));
}

void MpvVideoSurface::cleanupRenderContext()
{
    if (m_cleanupInProgress.exchange(
            true, std::memory_order_acq_rel)) {
        return;
    }

    if (m_frameSwappedConnection) {
        disconnect(m_frameSwappedConnection);
        m_frameSwappedConnection = {};
    }
    if (m_renderThread) {
        m_renderThread->requestStop();
        m_renderThread->wait();
        m_renderThread.reset();
    }

    if (context() && m_readFramebuffer != 0) {
        makeCurrent();
        context()->extraFunctions()->glDeleteFramebuffers(
            1, &m_readFramebuffer);
        m_readFramebuffer = 0;
        doneCurrent();
    }
    if (m_contextDestroyConnection) {
        disconnect(m_contextDestroyConnection);
        m_contextDestroyConnection = {};
    }

    m_renderContextReady.store(false, std::memory_order_release);
    m_cleanupInProgress.store(false, std::memory_order_release);
}

void MpvVideoSurface::createRenderContextOnWorker()
{
    mpv_opengl_init_params glInitialization{
        &MpvVideoSurface::getProcAddress, nullptr};
    int advancedControl = 1;
    mpv_render_param parameters[] = {
        {MPV_RENDER_PARAM_API_TYPE,
         const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitialization},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
        {MPV_RENDER_PARAM_INVALID, nullptr}};

    const int result = mpv_render_context_create(
        &m_renderContext, m_core->handle(), parameters);
    if (result < 0) {
        m_renderContext = nullptr;
        const QString message =
            QStringLiteral("mpv_render_context_create failed: %1")
                .arg(QString::fromUtf8(mpv_error_string(result)));
        Logger::error(message);
        QMetaObject::invokeMethod(
            this,
            [this, message] {
                emit renderInitializationFailed(message);
            },
            Qt::QueuedConnection);
        return;
    }

    mpv_render_context_set_update_callback(
        m_renderContext, &MpvVideoSurface::onUpdateTrampoline, this);
    m_renderContextReady.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(
        this,
        [this] {
            emit renderContextReady();
        },
        Qt::QueuedConnection);
}

void MpvVideoSurface::destroyRenderContextOnWorker()
{
    if (m_renderContext) {
        mpv_render_context_set_update_callback(
            m_renderContext, nullptr, nullptr);
        mpv_render_context_free(m_renderContext);
        m_renderContext = nullptr;
    }

    QOpenGLFunctions *functions =
        QOpenGLContext::currentContext()->functions();
    {
        const std::scoped_lock lock(m_framePool->mutex);
        m_framePool->rebuilding = true;
        for (auto &slot : m_framePool->buffers) {
            if (slot.framebuffer != 0) {
                functions->glDeleteFramebuffers(
                    1, &slot.framebuffer);
            }
            if (slot.texture != 0) {
                functions->glDeleteTextures(1, &slot.texture);
            }
            slot = {};
        }
        m_framePool->width = 0;
        m_framePool->height = 0;
        ++m_framePool->generation;
        m_framePool->rebuilding = false;
    }
    m_framePool->condition.notify_all();
    m_renderContextReady.store(false, std::memory_order_release);
}

void MpvVideoSurface::ensureWorkerFramebuffer(const QSize &pixelSize)
{
    const QSize safeSize(
        qMax(1, pixelSize.width()),
        qMax(1, pixelSize.height()));
    {
        const std::scoped_lock lock(m_framePool->mutex);
        if (m_framePool->width == safeSize.width()
            && m_framePool->height == safeSize.height()
            && m_framePool->buffers.front().texture != 0) {
            return;
        }
    }

    QOpenGLFunctions *functions =
        QOpenGLContext::currentContext()->functions();
    std::unique_lock poolLock(m_framePool->mutex);
    m_framePool->rebuilding = true;
    m_framePool->condition.wait(
        poolLock, [this] {
            for (const auto &slot : m_framePool->buffers) {
                if (slot.state == SharedFramePool::State::Displaying) {
                    return false;
                }
            }
            return true;
        });

    for (auto &slot : m_framePool->buffers) {
        if (slot.framebuffer != 0) {
            functions->glDeleteFramebuffers(
                1, &slot.framebuffer);
        }
        if (slot.texture != 0) {
            functions->glDeleteTextures(1, &slot.texture);
        }
        slot = {};
    }

    bool complete = true;
    for (auto &slot : m_framePool->buffers) {
        functions->glGenTextures(1, &slot.texture);
        functions->glBindTexture(GL_TEXTURE_2D, slot.texture);
        functions->glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        functions->glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        functions->glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        functions->glTexParameteri(
            GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        functions->glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8,
            safeSize.width(), safeSize.height(), 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        functions->glGenFramebuffers(1, &slot.framebuffer);
        functions->glBindFramebuffer(
            GL_FRAMEBUFFER, slot.framebuffer);
        functions->glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, slot.texture, 0);
        if (functions->glCheckFramebufferStatus(GL_FRAMEBUFFER)
            != GL_FRAMEBUFFER_COMPLETE) {
            complete = false;
            break;
        }
        functions->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        functions->glClear(GL_COLOR_BUFFER_BIT);
    }

    functions->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    functions->glFinish();
    m_framePool->width = complete ? safeSize.width() : 0;
    m_framePool->height = complete ? safeSize.height() : 0;
    ++m_framePool->generation;
    m_framePool->rebuilding = false;
    poolLock.unlock();
    m_framePool->condition.notify_all();

    if (!complete) {
        Logger::error(
            QStringLiteral(
                "Background video framebuffer pool is incomplete"));
    }
}

void MpvVideoSurface::renderFrameOnWorker(const QSize &pixelSize)
{
    if (!m_renderContext) {
        return;
    }

    const std::uint64_t updateFlags =
        mpv_render_context_update(m_renderContext);
    if ((updateFlags & MPV_RENDER_UPDATE_FRAME) == 0) {
        return;
    }

    ensureWorkerFramebuffer(pixelSize);
    std::size_t selectedIndex = SharedFramePool::BufferCount;
    unsigned int framebuffer = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(m_framePool->mutex);
        for (std::size_t index = 0;
             index < SharedFramePool::BufferCount; ++index) {
            if (m_framePool->buffers[index].state
                == SharedFramePool::State::Available) {
                selectedIndex = index;
                break;
            }
        }

        if (selectedIndex == SharedFramePool::BufferCount) {
            std::uint64_t oldestSerial =
                std::numeric_limits<std::uint64_t>::max();
            for (std::size_t index = 0;
                 index < SharedFramePool::BufferCount; ++index) {
                const auto &slot = m_framePool->buffers[index];
                if (slot.state == SharedFramePool::State::Ready
                    && slot.serial < oldestSerial) {
                    selectedIndex = index;
                    oldestSerial = slot.serial;
                }
            }
        }

        if (selectedIndex == SharedFramePool::BufferCount
            || m_framePool->width <= 0
            || m_framePool->height <= 0) {
            return;
        }

        auto &slot = m_framePool->buffers[selectedIndex];
        slot.state = SharedFramePool::State::Rendering;
        framebuffer = slot.framebuffer;
        targetWidth = m_framePool->width;
        targetHeight = m_framePool->height;
        generation = m_framePool->generation;
    }

    mpv_opengl_fbo frameBuffer{
        static_cast<int>(framebuffer),
        targetWidth,
        targetHeight,
        0};
    int flipVertically = 1;
    mpv_render_param parameters[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &frameBuffer},
        {MPV_RENDER_PARAM_FLIP_Y, &flipVertically},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(m_renderContext, parameters);
    // Finish on the worker, never the GUI, before publishing the texture to
    // another context. This removes cross-context partial/black frames.
    QOpenGLContext::currentContext()->functions()->glFinish();

    {
        const std::scoped_lock lock(m_framePool->mutex);
        if (generation != m_framePool->generation) {
            return;
        }
        for (auto &slot : m_framePool->buffers) {
            if (slot.state == SharedFramePool::State::Ready) {
                slot.state = SharedFramePool::State::Available;
            }
        }
        auto &selected = m_framePool->buffers[selectedIndex];
        selected.serial = ++m_framePool->nextSerial;
        selected.state = SharedFramePool::State::Ready;
    }

    QMetaObject::invokeMethod(
        this,
        [this] {
            if (!m_cleanupInProgress.load(
                    std::memory_order_acquire)) {
                update();
            }
        },
        Qt::QueuedConnection);
}

void MpvVideoSurface::requestSwapReport()
{
    if (m_renderThread
        && !m_cleanupInProgress.load(std::memory_order_acquire)) {
        m_renderThread->requestSwapReport();
    }
}

void MpvVideoSurface::reportSwapOnWorker()
{
    if (m_renderContext) {
        mpv_render_context_report_swap(m_renderContext);
    }
}

void MpvVideoSurface::onUpdateTrampoline(void *context)
{
    auto *surface = static_cast<MpvVideoSurface *>(context);
    if (surface->m_renderThread
        && !surface->m_cleanupInProgress.load(
            std::memory_order_acquire)) {
        surface->m_renderThread->requestFrame();
    }
}

void *MpvVideoSurface::getProcAddress(void *context, const char *name)
{
    Q_UNUSED(context)
    QOpenGLContext *openGlContext =
        QOpenGLContext::currentContext();
    if (!openGlContext) {
        return nullptr;
    }
    return reinterpret_cast<void *>(
        openGlContext->getProcAddress(name));
}
