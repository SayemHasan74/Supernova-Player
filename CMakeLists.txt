cmake_minimum_required(VERSION 3.24)
project(Supernova VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

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
    src/Core/Logger.h
    src/Core/Logger.cpp
    src/Mpv/MpvCore.h
    src/Mpv/MpvCore.cpp
    src/Mpv/MpvVideoSurface.h
    src/Mpv/MpvVideoSurface.cpp
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
