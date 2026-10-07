cmake_minimum_required(VERSION 3.21)

project(karaoke VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Quick
    Qml
    ShaderTools
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(karaoke
    src/main.cpp
)

# KaraokeLine's GPU sweep, compiled to .qsb at build time. The sources live next
# to the component that uses them; PREFIX keeps the qrc paths as :/shaders/...
qt_add_shaders(karaoke "karaoke_shaders"
    PREFIX "/shaders"
    BATCHABLE
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml/lyrics/shaders"
    FILES
        qml/lyrics/shaders/karaoke.vert
        qml/lyrics/shaders/karaoke.frag
)

# The QML module: everything under `qml/`, plus the C++ types marked with
# QML_ELEMENT(). QML files are grouped by role so the tree reads as
# views -> feature components -> shared components.
qt_add_qml_module(karaoke
    URI Karaoke
    VERSION 1.0

    QML_FILES
        # application root
        qml/Main.qml

        # screens
        qml/views/SongsPage.qml
        qml/views/NowPlayingPage.qml

        # shared visual components
        qml/components/Artwork.qml
        qml/components/WordFlow.qml

        # lyrics feature
        qml/lyrics/LyricsView.qml
        qml/lyrics/KaraokeLine.qml

        # now-playing feature
        qml/nowplaying/QueuePanel.qml
        qml/nowplaying/RawLyricsView.qml

        # playback feature
        qml/playback/MiniPlayer.qml

    SOURCES
        # lyrics: pure model + parsers, then the QML adapter
        src/lyrics/lyrics.h
        src/lyrics/lyrics.cpp
        src/lyrics/lyricsparser.h
        src/lyrics/lrcparser.h
        src/lyrics/lrcparser.cpp
        src/lyrics/lyricsdocument.h
        src/lyrics/lyricsdocument.cpp

        # library: songs, scanning and tag/artwork/lyrics loading
        src/library/song.h
        src/library/song.cpp
        src/library/library.h
        src/library/library.cpp
        src/library/trackloader.h
        src/library/trackloader.cpp

        # playback: the GStreamer player and queue
        src/playback/player.h
        src/playback/player.cpp
)

target_include_directories(karaoke
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lyrics
        ${CMAKE_CURRENT_SOURCE_DIR}/src/library
        ${CMAKE_CURRENT_SOURCE_DIR}/src/playback
)

target_link_libraries(karaoke PRIVATE
    Qt6::Quick
    Qt6::Qml
    PkgConfig::GST
    PkgConfig::TAGLIB
)
