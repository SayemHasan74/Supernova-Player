#pragma once

#include <QColor>

namespace Supernova::Ui {

// IINA's default floating OSC is 460 points wide and approximately 67 points
// tall. Keeping these values centralized prevents windowed and fullscreen
// layouts from drifting apart as controls are added.
inline constexpr int floatingControlWidth = 460;
inline constexpr int floatingControlMinWidth = 200;
inline constexpr int floatingControlHeight = 67;
inline constexpr int floatingControlRadius = 7;
inline constexpr int floatingControlEdgeMargin = 10;
inline constexpr double floatingControlVerticalPosition = 0.10;

inline constexpr int compactButtonExtent = 24;
inline constexpr int primaryButtonExtent = 28;
inline constexpr int timeLabelWidth = 48;
inline constexpr int volumeSliderWidth = 70;
inline constexpr int controlFadeDurationMs = 250;
inline constexpr int controlAutoHideMs = 2500;

inline const QColor panelFill{23, 23, 25, 218};
inline const QColor panelHighlight{255, 255, 255, 28};
inline const QColor panelBorder{255, 255, 255, 38};
inline const QColor primaryText{245, 245, 247, 242};
inline const QColor secondaryText{235, 235, 245, 154};
inline const QColor controlHover{255, 255, 255, 28};
inline const QColor controlPressed{255, 255, 255, 48};
inline const QColor sliderPlayed{242, 242, 247, 235};
inline const QColor sliderRemaining{235, 235, 245, 68};
inline const QColor sliderKnob{255, 255, 255, 245};

} // namespace Supernova::Ui
