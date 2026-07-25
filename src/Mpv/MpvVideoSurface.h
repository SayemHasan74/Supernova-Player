#pragma once

#include <QMetaObject>
#include <QOpenGLWidget>

#include <mpv/render.h>

class MpvCore;

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
        return m_renderContext != nullptr;
    }

signals:
    void renderContextReady();

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void maybeUpdate();
    void cleanupRenderContext();

private:
    static void onUpdateTrampoline(void *context);
    static void *getProcAddress(void *context, const char *name);

    MpvCore *m_core = nullptr;
    mpv_render_context *m_renderContext = nullptr;
    QMetaObject::Connection m_contextDestroyConnection;
};
