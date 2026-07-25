#include "Mpv/MpvVideoSurface.h"

#include "Core/Logger.h"
#include "Mpv/MpvCore.h"

#include <QByteArray>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QWindow>

#include <mpv/render_gl.h>

#include <stdexcept>

MpvVideoSurface::MpvVideoSurface(MpvCore *core, QWidget *parent)
    : QOpenGLWidget(parent),
      m_core(core)
{
    if (!m_core) {
        throw std::invalid_argument("MpvVideoSurface requires an MpvCore");
    }
    setAutoFillBackground(false);
}

MpvVideoSurface::~MpvVideoSurface()
{
    cleanupRenderContext();
}

void MpvVideoSurface::initializeGL()
{
    cleanupRenderContext();

    QOpenGLContext *openGlContext = context();
    if (!openGlContext) {
        throw std::runtime_error("QOpenGLWidget has no OpenGL context");
    }

    QOpenGLFunctions *functions = openGlContext->functions();
    functions->initializeOpenGLFunctions();
    const auto *vendor = functions->glGetString(GL_VENDOR);
    const auto *renderer = functions->glGetString(GL_RENDERER);
    const auto *version = functions->glGetString(GL_VERSION);
    const QSurfaceFormat format = openGlContext->format();
    const auto glString = [](const GLubyte *value) {
        return value
            ? QString::fromLatin1(reinterpret_cast<const char *>(value))
            : QStringLiteral("unknown");
    };
    Logger::info(
        QStringLiteral("OpenGL %1.%2 (%3) — %4 / %5 / %6")
            .arg(format.majorVersion())
            .arg(format.minorVersion())
            .arg(openGlContext->isOpenGLES()
                     ? QStringLiteral("OpenGL ES")
                     : QStringLiteral("desktop"))
            .arg(glString(vendor), glString(renderer), glString(version)));

    // QOpenGLWidget owns this context and can leave diagnostic errors from its
    // own setup. mpv's renderer checks the shared error flag, so start the
    // handoff with a clean error state.
    while (functions->glGetError() != GL_NO_ERROR) {
    }

    m_contextDestroyConnection = connect(
        openGlContext, &QOpenGLContext::aboutToBeDestroyed,
        this, &MpvVideoSurface::cleanupRenderContext,
        Qt::DirectConnection);

    mpv_opengl_init_params glInitialization{
        &MpvVideoSurface::getProcAddress, nullptr};
    mpv_render_param parameters[] = {
        {MPV_RENDER_PARAM_API_TYPE,
         const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitialization},
        {MPV_RENDER_PARAM_INVALID, nullptr}};

    const int result = mpv_render_context_create(
        &m_renderContext, m_core->handle(), parameters);
    if (result < 0) {
        m_renderContext = nullptr;
        throw std::runtime_error(
            QStringLiteral("mpv_render_context_create failed: %1")
                .arg(QString::fromUtf8(mpv_error_string(result)))
                .toStdString());
    }

    mpv_render_context_set_update_callback(
        m_renderContext, &MpvVideoSurface::onUpdateTrampoline, this);
    emit renderContextReady();
}

void MpvVideoSurface::paintGL()
{
    if (!m_renderContext) {
        return;
    }

    const qreal scale = devicePixelRatioF();
    mpv_opengl_fbo frameBuffer{
        static_cast<int>(defaultFramebufferObject()),
        qRound(static_cast<qreal>(width()) * scale),
        qRound(static_cast<qreal>(height()) * scale),
        0};
    int flipVertically = 1;
    mpv_render_param parameters[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &frameBuffer},
        {MPV_RENDER_PARAM_FLIP_Y, &flipVertically},
        {MPV_RENDER_PARAM_INVALID, nullptr}};

    // QOpenGLWidget prepares its framebuffer immediately before paintGL().
    // Do not let an error raised by that host-side setup be attributed to mpv.
    if (QOpenGLFunctions *functions = context()->functions()) {
        while (functions->glGetError() != GL_NO_ERROR) {
        }
    }
    mpv_render_context_render(m_renderContext, parameters);
}

void MpvVideoSurface::maybeUpdate()
{
    if (!m_renderContext) {
        return;
    }

    if (window()->isMinimized()) {
        makeCurrent();
        paintGL();
        if (context() && context()->surface()) {
            context()->swapBuffers(context()->surface());
        }
        doneCurrent();
        return;
    }

    update();
}

void MpvVideoSurface::cleanupRenderContext()
{
    if (!m_renderContext) {
        return;
    }

    if (context()) {
        makeCurrent();
    }
    mpv_render_context_set_update_callback(m_renderContext, nullptr, nullptr);
    mpv_render_context_free(m_renderContext);
    m_renderContext = nullptr;
    if (context()) {
        doneCurrent();
    }

    if (m_contextDestroyConnection) {
        disconnect(m_contextDestroyConnection);
        m_contextDestroyConnection = {};
    }
}

void MpvVideoSurface::onUpdateTrampoline(void *context)
{
    auto *surface = static_cast<MpvVideoSurface *>(context);
    QMetaObject::invokeMethod(
        surface, &MpvVideoSurface::maybeUpdate, Qt::QueuedConnection);
}

void *MpvVideoSurface::getProcAddress(void *context, const char *name)
{
    Q_UNUSED(context);
    QOpenGLContext *openGlContext = QOpenGLContext::currentContext();
    if (!openGlContext) {
        return nullptr;
    }
    return reinterpret_cast<void *>(openGlContext->getProcAddress(name));
}
