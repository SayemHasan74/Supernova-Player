#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>

#include <functional>

enum class SubtitleAutoLoadMode {
    Disabled = 0,
    Filename = 1,
    Smart = 2
};

struct AutomaticMatchOptions {
    bool addSiblingsToPlaylist = true;
    SubtitleAutoLoadMode subtitleMode = SubtitleAutoLoadMode::Smart;
    QString subtitleSearchPaths = QStringLiteral("./*");
    QString subtitlePriorityStrings;
    int maximumFuzzyComparisons = 10'000;
};

struct AutomaticMatchResult {
    QList<QUrl> playlist;
    QHash<QString, QList<QUrl>> subtitlesByMedia;
    bool cancelled = false;
};

class AutomaticFileMatcher final {
public:
    using CancellationCheck = std::function<bool()>;

    [[nodiscard]] static AutomaticMatchResult match(
        const QUrl &mediaUrl,
        const AutomaticMatchOptions &options = {},
        const CancellationCheck &cancelled = {});

    [[nodiscard]] static QString mediaKey(const QUrl &url);
};
