pragma ComponentBehavior: Bound

import Karaoke 1.0
import QtQuick
import QtQuick.Controls

// The lyric sheet for one Song, driven by its structured lyrics document.
//
// Three cases, chosen per line:
//   * untimed            -> plain normal text, no highlight
//   * line-level timing  -> the line fades between unsung and sung colours
//   * word/syllable      -> KaraokeLine, the synced GPU sweep
//
// Background vocals and translations are their own smaller rows rather than
// being hidden under the main line. Clicking a timed row seeks to its start.
Item {
    id: root

    property Song song
    property int transitionDuration: 1000
    property int transitionTiming: Easing.InOutQuart
    // Start a line's transition this many milliseconds before its real start, so
    // the fade is complete by the time it is actually sung. The same look-ahead
    // makes the previous line start fading out 500 ms early. Defaults to half the
    // transition duration. This only shifts *which* line is active; the synced
    // word sweep still follows the real playback position.
    property int activationLeadMs: Math.round(transitionDuration / 2)

    readonly property var document: root.song ? root.song.document : null
    readonly property real positionMs: Player.position * 1000
    readonly property bool timed: root.document ? root.document.timed : false
    // The active line is picked slightly ahead of the clock so the transition
    // finishes on the beat; the karaoke sweep still follows the real position.
    readonly property real effectivePositionMs: positionMs + activationLeadMs
    // Rows can overlap, and more than one can be active at once; this is the
    // topmost one, and the view scrolls to it.
    readonly property int firstActiveRow: root.document ? root.document.firstActiveRow(effectivePositionMs) : -1
    readonly property real lineSize: Math.max(20, Math.min(32, width * 0.075))

    property real sizeReduce: 0.94           // how much smaller the unsung line gets

    onFirstActiveRowChanged: if (root.firstActiveRow >= 0)
        scroller.centerOn(root.firstActiveRow)

    // Colour of one plain row, by kind and whether it is the current line. With
    // no timing at all every line is simply normal text.
    function rowColor(item) {
        var row = item.modelData;
        if (row.isSection || row.isInstrumental)
            return "#5c5c68";
        if (!root.timed) {
            if (row.isMain)
                return "#c9c9d2";
            return "#8b8b96";
        }
        if (item.active) {
            if (row.isMain)
                return "#ffffff";
            if (row.isBackground)
                return "#dcdce6";
            return "#c9c9d2";
        }
        if (row.isMain)
            return "#9b9ba4";
        return "#6f6f7c";
    }

    Flickable {
        id: scroller

        readonly property real edgeSlack: Math.max(0, (height - column.height) / 2)

        function centerOn(index) {
            if (index < 0)
                return;
            var item = repeater.itemAt(index);
            if (!item)
                return;
            var target = column.y + item.y - height / 4;
            smoothscrolling.to = Math.max(0, Math.min(target, contentHeight - height));
            smoothscrolling.restart();
        }

        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: column.height + 2 * edgeSlack
        contentWidth: width
        flickableDirection: Flickable.VerticalFlick

        ScrollBar.vertical: ScrollBar {}

        onDraggingChanged: if (dragging)
            smoothscrolling.stop()
        onHeightChanged: contentY = Math.min(contentY, Math.max(0, contentHeight - height))

        NumberAnimation {
            id: smoothscrolling

            duration: root.transitionDuration
            easing.type: root.transitionTiming
            property: "contentY"
            target: scroller
        }

        Column {
            id: column

            property Item lastItem: repeater.itemAt(repeater.count - 1)

            spacing: 0
            width: scroller.width
            y: scroller.edgeSlack
            topPadding: scroller.height / 4
            bottomPadding: scroller.height / 4 * 3 - (lastItem ? lastItem.height : 0)

            Repeater {
                id: repeater

                model: root.document ? root.document.rows : null

                delegate: Item {
                    id: rowItem

                    required property int index
                    required property var modelData

                    readonly property bool active: root.timed && modelData.activeStartMs >= 0 && root.effectivePositionMs >= modelData.activeStartMs && (modelData.activeEndMs < 0 || root.effectivePositionMs < modelData.activeEndMs)
                    readonly property real textSize: modelData.isMain ? root.lineSize : (modelData.isSection ? root.lineSize * 0.6 : (modelData.isInstrumental ? root.lineSize * 0.62 : root.lineSize * 0.7))
                    readonly property real topGap: modelData.groupStart ? 18 : 4

                    height: topGap + (modelData.karaoke ? karaokeLine.height : label.height)
                    width: scroller.width

                    Text {
                        id: label

                        color: root.rowColor(rowItem)
                        font.italic: rowItem.modelData.isTranslation
                        font.pixelSize: rowItem.textSize
                        font.weight: rowItem.modelData.isMain ? Font.DemiBold : Font.Normal
                        lineHeight: 1.0
                        horizontalAlignment: rowItem.modelData.alignEnd !== rowItem.modelData.isRtl ? Text.AlignRight : Text.AlignLeft
                        text: rowItem.modelData.text
                        visible: !rowItem.modelData.karaoke
                        width: parent.width
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        y: rowItem.topGap

                        scale: rowItem.active ? 1.0 : root.sizeReduce
                        transformOrigin: rowItem.modelData.alignEnd !== rowItem.modelData.isRtl ? Item.Right : Item.Left

                        Behavior on color {
                            ColorAnimation {
                                duration: root.transitionDuration
                                easing.type: root.transitionTiming
                            }
                        }

                        Behavior on scale {
                            NumberAnimation {
                                duration: root.transitionDuration
                                easing.type: root.transitionTiming
                            }
                        }
                    }

                    KaraokeLine {
                        id: karaokeLine

                        active: rowItem.active
                        alignEnd: rowItem.modelData.alignEnd
                        lineEnd: rowItem.modelData.lineEnd
                        lineStart: rowItem.modelData.lineStart
                        position: Player.position
                        textFont: Qt.font({
                            pixelSize: rowItem.textSize,
                            weight: Font.DemiBold
                        })
                        transitionDuration: root.transitionDuration
                        transitionTiming: root.transitionTiming
                        visible: rowItem.modelData.karaoke
                        words: rowItem.modelData ? rowItem.modelData.words : []
                        wrapWidth: rowItem.width
                        rtl: rowItem.modelData.isRtl
                        y: rowItem.topGap
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: rowItem.modelData.startMs >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor

                        onClicked: if (rowItem.modelData.startMs >= 0)
                            Player.seek(rowItem.modelData.startMs / 1000.0)
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        color: "#5c5c68"
        font.pixelSize: 15
        text: "No lyrics available"
        visible: !root.document || root.document.empty
    }
}
