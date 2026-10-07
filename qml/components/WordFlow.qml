import QtQuick

Item {
    id: root

    default property alias content: contentItem.data

    property real wordSpacing: 8
    property real lineSpacing: 0
    property bool rtl: false
    property int horizontalAlignment: Text.AlignLeft

    implicitHeight: contentHeight
    property real contentHeight: 0

    Item {
        id: contentItem
        anchors.fill: parent
    }

    function scheduleLayout() {
        Qt.callLater(relayout)
    }

    function lineStartX(lineWidth) {
        if (horizontalAlignment === Text.AlignHCenter)
            return (width - lineWidth) / 2

        if (horizontalAlignment === Text.AlignRight)
            return width - lineWidth

        return 0
    }

    function relayout() {
        if (width <= 0)
            return

        var items = contentItem.children.filter(function(item) {
            return item.visible && item.implicitWidth > 0
        })

        var lines = []
        var line = []
        var lineWidth = 0
        var lineHeight = 0

        // Build lines
        for (var i = 0; i < items.length; ++i) {
            var item = items[i]
            var itemWidth = item.implicitWidth

            var requiredWidth = line.length === 0
                ? itemWidth
                : lineWidth + wordSpacing + itemWidth

            if (line.length > 0 && requiredWidth > width) {
                lines.push({
                    items: line,
                    width: lineWidth,
                    height: lineHeight
                })

                line = []
                lineWidth = 0
                lineHeight = 0
            }

            line.push(item)

            lineWidth = line.length === 1
                ? itemWidth
                : lineWidth + wordSpacing + itemWidth

            lineHeight = Math.max(
                lineHeight,
                item.implicitHeight
            )
        }

        // Last line
        if (line.length > 0) {
            lines.push({
                items: line,
                width: lineWidth,
                height: lineHeight
            })
        }

        // Position lines
        var y = 0

        for (var l = 0; l < lines.length; ++l) {
            var current = lines[l]
            var startX = lineStartX(current.width)

            if (rtl) {
                var rtlX = startX + current.width

                for (var r = 0; r < current.items.length; ++r) {
                    var rtlItem = current.items[r]

                    rtlX -= rtlItem.implicitWidth

                    rtlItem.x = rtlX
                    rtlItem.y = y

                    rtlX -= wordSpacing
                }
            } else {
                var ltrX = startX

                for (var j = 0; j < current.items.length; ++j) {
                    var ltrItem = current.items[j]

                    ltrItem.x = ltrX
                    ltrItem.y = y

                    ltrX += ltrItem.implicitWidth + wordSpacing
                }
            }

            y += current.height + lineSpacing
        }

        contentHeight = Math.max(0, y - lineSpacing)
    }

    onWidthChanged: scheduleLayout()
    onRtlChanged: scheduleLayout()
    onHorizontalAlignmentChanged: scheduleLayout()
    onWordSpacingChanged: scheduleLayout()
    onLineSpacingChanged: scheduleLayout()

    Component.onCompleted: scheduleLayout()
}
