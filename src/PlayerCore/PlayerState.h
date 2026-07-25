#pragma once

#include <QMetaType>

// Declaration order is load-bearing. Re-derive isActive() and isLoaded()
// before changing it.
enum class PlayerState {
    Loading,
    Starting,
    Loaded,
    Playing,
    Paused,
    Stopping,
    Idle,
    ShuttingDown,
    ShutDown
};

constexpr bool isActive(PlayerState state)
{
    return static_cast<int>(state)
        < static_cast<int>(PlayerState::Stopping);
}

constexpr bool isLoaded(PlayerState state)
{
    return isActive(state)
        && static_cast<int>(state)
            >= static_cast<int>(PlayerState::Loaded);
}

static_assert(isActive(PlayerState::Loading));
static_assert(isLoaded(PlayerState::Loaded));
static_assert(isLoaded(PlayerState::Paused));
static_assert(!isActive(PlayerState::Stopping));
static_assert(!isActive(PlayerState::Idle));
static_assert(!isLoaded(PlayerState::Idle));
static_assert(!isActive(PlayerState::ShuttingDown));

Q_DECLARE_METATYPE(PlayerState)
