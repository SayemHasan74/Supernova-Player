#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

struct PlaybackChapter {
    int index = 0;
    QString title;
    double startTimeSec = 0.0;

    friend bool operator==(
        const PlaybackChapter &, const PlaybackChapter &) = default;
};

enum class AbLoopStatus {
    Cleared,
    ASet,
    BSet,
};

struct AbLoopState {
    AbLoopStatus status = AbLoopStatus::Cleared;
    double pointA = 0.0;
    double pointB = 0.0;

    friend bool operator==(
        const AbLoopState &, const AbLoopState &) = default;
};

Q_DECLARE_METATYPE(PlaybackChapter)
Q_DECLARE_METATYPE(QList<PlaybackChapter>)
Q_DECLARE_METATYPE(AbLoopState)

