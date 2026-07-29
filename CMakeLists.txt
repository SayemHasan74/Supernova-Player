cmake_minimum_required(VERSION 3.24)
project(Supernova VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

set(SUPERNOVA_CHANNEL "Dev" CACHE STRING "Build channel: Dev, Beta, Nightly, Release")
set_property(CACHE SUPERNOVA_CHANNEL PROPERTY STRINGS Dev Beta Nightly Release)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Gui OpenGLWidgets Network Svg)
qt_standard_project_setup()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindMPV)

qt_add_executable(Supernova WIN32
    src/main.cpp
    src/App/Application.h
    src/App/Application.cpp
    src/App/SingleInstanceGuard.h
    src/App/SingleInstanceGuard.cpp
    src/App/CommandLineArgs.h
    src/App/CommandLineArgs.cpp
    src/App/PlayerWindowManager.h
    src/App/PlayerWindowManager.cpp
    src/App/MediaSourceResolver.h
    src/App/MediaSourceResolver.cpp
    src/App/AutomaticFileMatcher.h
    src/App/AutomaticFileMatcher.cpp
    src/Core/Logger.h
    src/Core/Logger.cpp
    src/Network/NetworkConfiguration.h
    src/Network/NetworkConfiguration.cpp
    src/Network/SecureCredentialStore.h
    src/Network/SecureCredentialStore.cpp
    src/Network/OnlineSubtitleService.h
    src/Network/OnlineSubtitleService.cpp
    src/Preferences/PlayerConfiguration.h
    src/Preferences/PlayerConfiguration.cpp
    src/Mpv/MpvCore.h
    src/Mpv/MpvCore.cpp
    src/Mpv/MpvEvent.h
    src/Mpv/MpvVideoSurface.h
    src/Mpv/MpvVideoSurface.cpp
    src/PlayerCore/PlayerState.h
    src/PlayerCore/BufferingInfo.h
    src/PlayerCore/PlaybackInfo.h
    src/PlayerCore/MediaTrack.h
    src/PlayerCore/MediaTrack.cpp
    src/PlayerCore/QuickSettings.h
    src/PlayerCore/QuickSettings.cpp
    src/PlayerCore/NavigationState.h
    src/PlayerCore/PlaybackHistory.h
    src/PlayerCore/PlaybackHistory.cpp
    src/PlayerCore/RecentMedia.h
    src/PlayerCore/RecentMedia.cpp
    src/PlayerCore/ThumbnailProvider.h
    src/PlayerCore/ThumbnailProvider.cpp
    src/PlayerCore/PlaylistState.h
    src/PlayerCore/PlaylistIO.h
    src/PlayerCore/PlaylistIO.cpp
    src/PlayerCore/PlayerCore.h
    src/PlayerCore/PlayerCore.cpp
    src/UI/Design/DesignTokens.h
    src/UI/Commands/PlayerCommand.h
    src/UI/Commands/PlayerCommand.cpp
    src/UI/Controls/IinaPlayerChrome.h
    src/UI/Controls/IinaPlayerChrome.cpp
    src/UI/Controls/PlaybackFeedback.h
    src/UI/Controls/PlaybackFeedback.cpp
    src/UI/Playlist/PlaylistPanel.h
    src/UI/Playlist/PlaylistPanel.cpp
    src/UI/Media/MediaSettingsPanel.h
    src/UI/Media/MediaSettingsPanel.cpp
    src/UI/Welcome/WelcomeView.h
    src/UI/Welcome/WelcomeView.cpp
    src/UI/History/HistoryWindow.h
    src/UI/History/HistoryWindow.cpp
    src/UI/Inspector/MediaInspector.h
    src/UI/Inspector/MediaInspector.cpp
    src/UI/Subtitles/OnlineSubtitleDialog.h
    src/UI/Subtitles/OnlineSubtitleDialog.cpp
    src/UI/Preferences/PreferencesDialog.h
    src/UI/Preferences/PreferencesDialog.cpp
    src/UI/MainWindow/MainWindow.h
    src/UI/MainWindow/MainWindow.cpp
    resources/app.rc
    resources/app.manifest
    resources/resources.qrc
)

target_include_directories(Supernova PRIVATE src)

target_link_libraries(Supernova PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Svg
    Supernova::mpv
)
if(WIN32)
    target_link_libraries(Supernova PRIVATE Advapi32)
endif()

target_compile_definitions(Supernova PRIVATE
    SUPERNOVA_VERSION="${PROJECT_VERSION}"
    SUPERNOVA_CHANNEL="${SUPERNOVA_CHANNEL}"
)

if(MSVC)
    target_compile_options(Supernova PRIVATE /W4 /permissive-)
endif()

set_target_properties(Supernova PROPERTIES
    OUTPUT_NAME "Supernova"
    WIN32_EXECUTABLE TRUE
)

install(TARGETS Supernova
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES "${MPV_RUNTIME_DLL}" DESTINATION "${CMAKE_INSTALL_BINDIR}")

qt_generate_deploy_app_script(
    TARGET Supernova
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT "${deploy_script}")

add_custom_command(TARGET Supernova POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${MPV_RUNTIME_DLL}"
        "$<TARGET_FILE_DIR:Supernova>"
    VERBATIM
)

if(WIN32)
    # Keep the development build self-contained and directly runnable. This
    # is the Windows equivalent of IINA's build phase that copies its runtime
    # dylibs into the application bundle.
    add_custom_command(TARGET Supernova POST_BUILD
        COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
            --dir "$<TARGET_FILE_DIR:Supernova>"
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:Supernova>"
        COMMENT "Deploying Qt runtime beside Supernova"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

    qt_add_executable(SupernovaMediaSourceResolverTests
        tests/MediaSourceResolverTests.cpp
        src/App/MediaSourceResolver.h
        src/App/MediaSourceResolver.cpp
        src/App/AutomaticFileMatcher.h
        src/App/AutomaticFileMatcher.cpp
        src/PlayerCore/PlaylistIO.h
        src/PlayerCore/PlaylistIO.cpp
        src/PlayerCore/PlaylistState.h
    )
    target_include_directories(
        SupernovaMediaSourceResolverTests PRIVATE src)
    target_link_libraries(
        SupernovaMediaSourceResolverTests PRIVATE Qt6::Core)
    add_test(
        NAME MediaSourceResolver
        COMMAND SupernovaMediaSourceResolverTests)

    qt_add_executable(SupernovaMpvCoreTests
        tests/MpvCoreTests.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/Mpv/MpvCore.h
        src/Mpv/MpvCore.cpp
        src/Mpv/MpvEvent.h
        src/Network/NetworkConfiguration.h
        src/Network/NetworkConfiguration.cpp
        src/Preferences/PlayerConfiguration.h
        src/Preferences/PlayerConfiguration.cpp
    )
    target_include_directories(SupernovaMpvCoreTests PRIVATE src)
    target_link_libraries(
        SupernovaMpvCoreTests PRIVATE
        Qt6::Core
        Qt6::Test
        Supernova::mpv)
    set_target_properties(
        SupernovaMpvCoreTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_custom_command(TARGET SupernovaMpvCoreTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MPV_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:SupernovaMpvCoreTests>"
        VERBATIM
    )
    add_test(NAME MpvCore COMMAND SupernovaMpvCoreTests)

    qt_add_executable(SupernovaPlayerCoreLifecycleTests
        tests/PlayerCoreLifecycleTests.cpp
        src/App/MediaSourceResolver.h
        src/App/MediaSourceResolver.cpp
        src/App/AutomaticFileMatcher.h
        src/App/AutomaticFileMatcher.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/Mpv/MpvCore.h
        src/Mpv/MpvCore.cpp
        src/Mpv/MpvEvent.h
        src/Network/NetworkConfiguration.h
        src/Network/NetworkConfiguration.cpp
        src/Preferences/PlayerConfiguration.h
        src/Preferences/PlayerConfiguration.cpp
        src/PlayerCore/BufferingInfo.h
        src/PlayerCore/PlaybackInfo.h
        src/PlayerCore/MediaTrack.h
        src/PlayerCore/MediaTrack.cpp
        src/PlayerCore/QuickSettings.h
        src/PlayerCore/QuickSettings.cpp
        src/PlayerCore/NavigationState.h
        src/PlayerCore/PlaybackHistory.h
        src/PlayerCore/PlaybackHistory.cpp
        src/PlayerCore/RecentMedia.h
        src/PlayerCore/RecentMedia.cpp
        src/PlayerCore/ThumbnailProvider.h
        src/PlayerCore/ThumbnailProvider.cpp
        src/PlayerCore/PlaylistState.h
        src/PlayerCore/PlayerCore.h
        src/PlayerCore/PlayerCore.cpp
        src/PlayerCore/PlayerState.h
    )
    target_include_directories(
        SupernovaPlayerCoreLifecycleTests PRIVATE src)
    target_link_libraries(
        SupernovaPlayerCoreLifecycleTests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Supernova::mpv)
    set_target_properties(
        SupernovaPlayerCoreLifecycleTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_custom_command(
        TARGET SupernovaPlayerCoreLifecycleTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MPV_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:SupernovaPlayerCoreLifecycleTests>"
        VERBATIM
    )
    add_test(
        NAME PlayerCoreLifecycle
        COMMAND SupernovaPlayerCoreLifecycleTests)

    qt_add_executable(SupernovaMediaTrackTests
        tests/MediaTrackTests.cpp
        src/PlayerCore/MediaTrack.h
        src/PlayerCore/MediaTrack.cpp
        src/PlayerCore/QuickSettings.h
        src/PlayerCore/QuickSettings.cpp
    )
    target_include_directories(SupernovaMediaTrackTests PRIVATE src)
    target_link_libraries(
        SupernovaMediaTrackTests PRIVATE Qt6::Core Qt6::Test)
    set_target_properties(
        SupernovaMediaTrackTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME MediaTrack COMMAND SupernovaMediaTrackTests)

    qt_add_executable(SupernovaIinaPlayerChromeTests
        tests/IinaPlayerChromeTests.cpp
        src/App/MediaSourceResolver.h
        src/App/MediaSourceResolver.cpp
        src/App/AutomaticFileMatcher.h
        src/App/AutomaticFileMatcher.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/Mpv/MpvCore.h
        src/Mpv/MpvCore.cpp
        src/Mpv/MpvEvent.h
        src/Network/NetworkConfiguration.h
        src/Network/NetworkConfiguration.cpp
        src/Preferences/PlayerConfiguration.h
        src/Preferences/PlayerConfiguration.cpp
        src/PlayerCore/BufferingInfo.h
        src/PlayerCore/PlaybackInfo.h
        src/PlayerCore/MediaTrack.h
        src/PlayerCore/MediaTrack.cpp
        src/PlayerCore/QuickSettings.h
        src/PlayerCore/QuickSettings.cpp
        src/PlayerCore/NavigationState.h
        src/PlayerCore/PlaybackHistory.h
        src/PlayerCore/PlaybackHistory.cpp
        src/PlayerCore/RecentMedia.h
        src/PlayerCore/RecentMedia.cpp
        src/PlayerCore/ThumbnailProvider.h
        src/PlayerCore/ThumbnailProvider.cpp
        src/PlayerCore/PlaylistState.h
        src/PlayerCore/PlayerCore.h
        src/PlayerCore/PlayerCore.cpp
        src/PlayerCore/PlayerState.h
        src/UI/Design/DesignTokens.h
        src/UI/Controls/IinaPlayerChrome.h
        src/UI/Controls/IinaPlayerChrome.cpp
    )
    target_include_directories(
        SupernovaIinaPlayerChromeTests PRIVATE src)
    target_link_libraries(
        SupernovaIinaPlayerChromeTests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
        Supernova::mpv)
    set_target_properties(
        SupernovaIinaPlayerChromeTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_custom_command(
        TARGET SupernovaIinaPlayerChromeTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MPV_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:SupernovaIinaPlayerChromeTests>"
        VERBATIM
    )
    add_test(
        NAME IinaPlayerChrome
        COMMAND SupernovaIinaPlayerChromeTests)

    qt_add_executable(SupernovaPlayerCommandTests
        tests/PlayerCommandTests.cpp
        src/UI/Commands/PlayerCommand.h
        src/UI/Commands/PlayerCommand.cpp
    )
    target_include_directories(
        SupernovaPlayerCommandTests PRIVATE src)
    target_link_libraries(
        SupernovaPlayerCommandTests PRIVATE
        Qt6::Core
        Qt6::Test)
    set_target_properties(
        SupernovaPlayerCommandTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_test(
        NAME PlayerCommand
        COMMAND SupernovaPlayerCommandTests)

    qt_add_executable(SupernovaPlaybackHistoryTests
        tests/PlaybackHistoryTests.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/PlayerCore/PlaybackHistory.h
        src/PlayerCore/PlaybackHistory.cpp
        src/PlayerCore/RecentMedia.h
        src/PlayerCore/RecentMedia.cpp
    )
    target_include_directories(
        SupernovaPlaybackHistoryTests PRIVATE src)
    target_link_libraries(
        SupernovaPlaybackHistoryTests PRIVATE
        Qt6::Core
        Qt6::Test)
    set_target_properties(
        SupernovaPlaybackHistoryTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_test(
        NAME PlaybackHistory
        COMMAND SupernovaPlaybackHistoryTests)

    qt_add_executable(SupernovaPlayerConfigurationTests
        tests/PlayerConfigurationTests.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/Preferences/PlayerConfiguration.h
        src/Preferences/PlayerConfiguration.cpp
    )
    target_include_directories(
        SupernovaPlayerConfigurationTests PRIVATE src)
    target_link_libraries(
        SupernovaPlayerConfigurationTests PRIVATE
        Qt6::Core
        Qt6::Test)
    set_target_properties(
        SupernovaPlayerConfigurationTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_test(
        NAME PlayerConfiguration
        COMMAND SupernovaPlayerConfigurationTests)

    qt_add_executable(SupernovaWelcomeViewTests
        tests/WelcomeViewTests.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/PlayerCore/PlaybackHistory.h
        src/PlayerCore/PlaybackHistory.cpp
        src/UI/Welcome/WelcomeView.h
        src/UI/Welcome/WelcomeView.cpp
    )
    target_include_directories(
        SupernovaWelcomeViewTests PRIVATE src)
    target_link_libraries(
        SupernovaWelcomeViewTests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test)
    set_target_properties(
        SupernovaWelcomeViewTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_test(
        NAME WelcomeView
        COMMAND SupernovaWelcomeViewTests)

    qt_add_executable(SupernovaThumbnailProviderTests
        tests/ThumbnailProviderTests.cpp
        src/Core/Logger.h
        src/Core/Logger.cpp
        src/PlayerCore/ThumbnailProvider.h
        src/PlayerCore/ThumbnailProvider.cpp
    )
    target_include_directories(
        SupernovaThumbnailProviderTests PRIVATE src)
    target_link_libraries(
        SupernovaThumbnailProviderTests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Supernova::mpv)
    set_target_properties(
        SupernovaThumbnailProviderTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_custom_command(
        TARGET SupernovaThumbnailProviderTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MPV_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:SupernovaThumbnailProviderTests>"
        VERBATIM
    )
    add_test(
        NAME ThumbnailProvider
        COMMAND SupernovaThumbnailProviderTests)

    qt_add_executable(SupernovaOnlineSubtitleServiceTests
        tests/OnlineSubtitleServiceTests.cpp
        src/Network/OnlineSubtitleService.h
        src/Network/OnlineSubtitleService.cpp
        src/Network/SecureCredentialStore.h
        src/Network/SecureCredentialStore.cpp
    )
    target_include_directories(
        SupernovaOnlineSubtitleServiceTests PRIVATE src)
    target_link_libraries(
        SupernovaOnlineSubtitleServiceTests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test)
    if(WIN32)
        target_link_libraries(
            SupernovaOnlineSubtitleServiceTests PRIVATE Advapi32)
    endif()
    set_target_properties(
        SupernovaOnlineSubtitleServiceTests PROPERTIES
        WIN32_EXECUTABLE FALSE)
    add_test(
        NAME OnlineSubtitleService
        COMMAND SupernovaOnlineSubtitleServiceTests)

    if(WIN32)
        set_tests_properties(
            MediaSourceResolver MpvCore PlayerCoreLifecycle
            MediaTrack IinaPlayerChrome PlayerCommand PlaybackHistory
            PlayerConfiguration WelcomeView PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        set_tests_properties(
            ThumbnailProvider PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        set_tests_properties(
            OnlineSubtitleService PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()
