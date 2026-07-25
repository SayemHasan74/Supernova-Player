#pragma once

#include <QMetaObject>
#include <QOpenGLWidget>

#include <mpv/render.h>

#include <atomic>
#include <memory>

class MpvCore;
class MpvRenderThread;
class SharedFramePool;

class MpvVideoSurface final : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit MpvVideoSurface(MpvCore *core, QWidget *parent = nullptr);
    ~MpvVideoSurface() override;

    MpvVideoSurface(const MpvVideoSurface &) = delete;
    MpvVideoSurface &operator=(const MpvVideoSurface &) = delete;

    [[nodiscard]] QSize sizeHint() const override { return {480, 270}; }
    [[nodiscard]] bool isRenderContextReady() const noexcept
    {
        return m_renderContextReady.load(std::memory_order_acquire);
    }
    void setLiveResize(bool active);

signals:
    void renderContextReady();
    void renderInitializationFailed(const QString &message);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

private slots:
    void cleanupRenderContext();

private:
    friend class MpvRenderThread;

    void createRenderContextOnWorker();
    void destroyRenderContextOnWorker();
    void ensureWorkerFramebuffer(const QSize &pixelSize);
    void renderFrameOnWorker(const QSize &pixelSize);
    void requestSwapReport();
    void reportSwapOnWorker();
    static void onUpdateTrampoline(void *context);
    static void *getProcAddress(void *context, const char *name);

    MpvCore *m_core = nullptr;
    mpv_render_context *m_renderContext = nullptr;
    std::unique_ptr<MpvRenderThread> m_renderThread;
    std::unique_ptr<SharedFramePool> m_framePool;
    std::atomic_bool m_renderContextReady = false;
    std::atomic_bool m_cleanupInProgress = false;

    unsigned int m_readFramebuffer = 0;

    QMetaObject::Connection m_contextDestroyConnection;
    QMetaObject::Connection m_frameSwappedConnection;
};
