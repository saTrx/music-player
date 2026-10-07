pragma ComponentBehavior: Bound

import Karaoke 1.0
import QtQuick
import QtQuick.Layouts

// The now-playing screen.
//
// Three columns: the queue on the left, the cover/transport in the middle and
// the lyrics on the right. The two side panes are mutually exclusive - opening
// one closes the other - and each animates its width in or out.
Item {
    id: page

    property var stackView
    property Song song: Player.currentSong

    property bool lyricsOpen: true
    property bool queueOpen: false
    // When true the lyrics pane shows the raw dump of the parsed document.
    property bool showRaw: false

    // Animated 0..1 open fractions. Driving layout from these (rather than
    // animating widths directly) keeps resizes correct.
    property real lyricsFrac: lyricsOpen ? 3 : 0
    property real queueFrac: queueOpen ? 1 : 0

    readonly property real sideWidth: Math.min(380, layout.width * 0.5)
    readonly property bool repeatActive: Player.repeatMode !== Player.RepeatOff

    Behavior on lyricsFrac {
        NumberAnimation {
            duration: 250
            easing.type: Easing.InOutQuad
        }
    }

    Behavior on queueFrac {
        NumberAnimation {
            duration: 250
            easing.type: Easing.InOutQuad
        }
    }

    function toggleLyrics() {
        lyricsOpen = !lyricsOpen;
        if (lyricsOpen)
            queueOpen = false;
    }

    function toggleQueue() {
        queueOpen = !queueOpen;
        if (queueOpen)
            lyricsOpen = false;
    }

    function formatTime(seconds) {
        var s = Math.max(0, seconds);
        var m = Math.floor(s / 60);
        var r = Math.floor(s % 60);
        return m + ":" + (r < 10 ? "0" : "") + r;
    }

    Rectangle {
        anchors.fill: parent
        color: "#08080c"
    }

    // A wash of the song's colour behind everything.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                color: page.song ? page.song.colorB : "#08080c"
                position: 0.0
            }
            GradientStop {
                color: "#08080c"
                position: 0.55
            }
        }
        opacity: 0.22
    }

    // ---- back ------------------------------------------------------------
    Text {
        id: back

        color: "#9b9ba4"
        font.pixelSize: 15
        text: "\u2039  Songs"
        x: 48
        y: 40

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: page.stackView.pop()
        }
    }

    // ---- queue | cover | lyrics -----------------------------------------
    RowLayout {
        id: layout

        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        anchors.left: parent.left
        anchors.leftMargin: 48
        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.top: back.bottom
        anchors.topMargin: 20
        spacing: 36

        // Left: the queue.
        Item {
            id: queuePanel

            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: page.sideWidth * page.queueFrac
            clip: true
            opacity: page.queueFrac

            Behavior on opacity {
                NumberAnimation {
                    duration: 200
                }
            }

            QueuePanel {
                anchors.fill: parent

                onSongChosen: function (index) {
                    Player.playQueueIndex(index);
                }
            }
        }

        // Centre: cover, metadata and controls.
        Item {
            id: infoPanel

            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            Column {
                id: info

                anchors.centerIn: parent
                spacing: 16
                width: Math.min(280, infoPanel.width)

                Artwork {
                    id: cover

                    height: width
                    song: page.song
                    width: Math.min(parent.width, infoPanel.height * 0.42)
                }

                Text {
                    color: "white"
                    elide: Text.ElideRight
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    horizontalAlignment: Text.AlignHCenter
                    text: page.song ? page.song.title : ""
                    width: parent.width
                }

                Text {
                    color: "#a0a0ac"
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    text: page.song ? page.song.artist : ""
                    width: parent.width
                }

                Text {
                    color: "#ff8a8a"
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    text: Player.errorString
                    visible: Player.errorString.length > 0
                    width: parent.width
                }

                // ---- seek bar ----
                Item {
                    id: seekbar

                    height: 20
                    width: parent.width

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#23232e"
                        height: 4
                        radius: 2
                        width: parent.width
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        color: "white"
                        height: 4
                        radius: 2
                        width: parent.width * (Player.duration > 0 ? Math.min(1, Player.position / Player.duration) : 0)
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor

                        onClicked: function (mouse) {
                            if (Player.duration > 0)
                                Player.seek(mouse.x / width * Player.duration);
                        }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: "#5c5c68"
                    font.family: "monospace"
                    font.pixelSize: 13
                    text: page.formatTime(Player.position) + " / " + page.formatTime(Player.duration)
                }

                // ---- previous / play / next ----
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 28

                    Text {
                        color: "#cfcfd8"
                        font.pixelSize: 15
                        text: "Prev"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Player.previous()
                        }
                    }

                    Text {
                        color: "white"
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        text: Player.playing ? "Pause" : "Play"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Player.toggle()
                        }
                    }

                    Text {
                        color: "#cfcfd8"
                        font.pixelSize: 15
                        text: "Next"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Player.next()
                        }
                    }
                }

                // ---- volume ----
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#8b8b96"
                        font.pixelSize: 12
                        text: "Vol"
                    }

                    Item {
                        id: volumeSlider

                        anchors.verticalCenter: parent.verticalCenter
                        height: 20
                        width: 140

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "#23232e"
                            height: 4
                            radius: 2
                            width: parent.width
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            color: "white"
                            height: 4
                            radius: 2
                            width: parent.width * Player.volume
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor

                            function setVolume(x) {
                                Player.volume = Math.max(0, Math.min(1, x / width));
                            }

                            onPressed: function (mouse) {
                                setVolume(mouse.x);
                            }
                            onPositionChanged: function (mouse) {
                                if (pressed)
                                    setVolume(mouse.x);
                            }
                        }
                    }
                }

                // ---- lyrics / queue / repeat / shuffle / raw ----
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 14

                    Text {
                        color: page.lyricsOpen ? "white" : "#8b8b96"
                        font.pixelSize: 13
                        text: "Lyrics"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.toggleLyrics()
                        }
                    }

                    Text {
                        color: page.queueOpen ? "white" : "#8b8b96"
                        font.pixelSize: 13
                        text: "Queue"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.toggleQueue()
                        }
                    }

                    Text {
                        color: page.repeatActive ? "white" : "#8b8b96"
                        font.pixelSize: 13
                        text: Player.repeatMode === Player.RepeatOne ? "Repeat 1" : (Player.repeatMode === Player.RepeatAll ? "Repeat all" : "Repeat")

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Player.cycleRepeat()
                        }
                    }

                    Text {
                        color: Player.shuffle ? "white" : "#8b8b96"
                        font.pixelSize: 13
                        text: "Shuffle"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Player.toggleShuffle();
                                
                                
                            }
                        }
                    }

                    Text {
                        color: page.showRaw ? "white" : "#8b8b96"
                        font.pixelSize: 13
                        text: "Raw"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.showRaw = !page.showRaw
                        }
                    }
                }
            }
        }

        // Right: the lyrics.
        Item {
            id: lyricsPanel

            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: page.sideWidth * page.lyricsFrac
            clip: true
            opacity: page.lyricsFrac

            Behavior on opacity {
                NumberAnimation {
                    duration: 200
                }
            }

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                color: "#7d7d88"
                font.pixelSize: 13
                text: page.showRaw ? "Lyrics \u00b7 raw" : "Lyrics"
            }

            LyricsView {
                anchors.fill: parent
                anchors.topMargin: 26
                song: page.song
                visible: !page.showRaw
                
            }

            // Raw lyrics for debug
            RawLyricsView {
                anchors.fill: parent
                anchors.topMargin: 26
                emptyText: "(no parsed lyrics document)"
                monospace: true
                text: page.song && page.song.document ? page.song.document.debugText : ""
                visible: page.showRaw
            }
        }
    }
}
