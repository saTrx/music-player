pragma ComponentBehavior: Bound

import QtQuick

// One synced lyric line.
//
//   Flow of Text words      -> ShaderEffectSource  (glyph texture)
//   Repeater of gradients   -> ShaderEffectSource  (reading-order coordinate)
//   ShaderEffect            -> recolours along the reading order
//
// A line may wrap onto several rows. The highlight follows the *reading order*,
// not the screen's x axis: a second texture gives every fragment its position
// along the reading order, and the shader compares that with `progress`. Without
// this, a wrapped line would highlight both rows at the same x at once.
//
// Nothing about the text is rebuilt or recoloured on the CPU: the only value
// that changes per frame is `progress`, a normalised 0..1 position.
//
// Input is a list of timed words (`TimedWord`), so this renderer does not depend
// on any particular lyrics source.

Item {
    id: root

    // ---- supplied by the view -------------------------------------------
    // Word timing for a synced line. Items expose `text`, `start` and `end`.
    property var words: []
    property real lineStart: 0
    property real lineEnd: 0
    property real position: 0        // playback clock, in seconds
    property bool active: false
    property bool rtl: false
    // Align the line to the reading end (right for LTR, left for RTL), used to
    // put different singers on opposite sides.
    property bool alignEnd: false

    // Width available for the line, in pixels. A positive value wraps the
    // words onto as many rows as needed; 0 sizes the line to its content on a
    // single row.
    property real wrapWidth: 0

    // ---- appearance (all adjustable from QML) ----------------------------
    property color baseColor: "#9b9ba4"      // not sung yet
    property color sungColor: "#ffffff"      // the part that has been sung
    property color glowColor: "#ffd9ec"      // bright band at the playhead
    property real edge: 0.05                 // soft boundary, in line widths
    property real glow: 0.55                 // strength of the bright band
    property real sizeReduce: 0.94           // how much smaller the unsung line gets
    property int transitionDuration: 1000    // ms for the activate/deactivate tween
    property int transitionTiming: Easing.InOutQuart
    property font textFont: Qt.font({
        pixelSize: 42,
        weight: Font.DemiBold
    })
    readonly property real pad: 0           // breathing room around the glyphs

    readonly property int wordCount: wordItems.count
    readonly property real wordSpacing: textFont.pixelSize * 0.3
    readonly property real progress: computeProgress(position)
    // Total reading-order length of the line, used to normalise positions.
    readonly property real readingWidth: computeReadingWidth()
    // Space left over when a single row does not fill the wrap width; used to
    // push an end-aligned line to the opposite side.
    readonly property real freeSpace: root.wrapWidth > 0 ? Math.max(0, root.wrapWidth - 2 * root.pad - readingWidth) : 0
    readonly property bool singleRow: {
        var count = wordItems.count;
        if (count === 0)
            return true;
        var first = wordItems.itemAt(0);
        return first !== null && flow.height <= first.height + 0.5;
    }
    readonly property real alignOffset: root.alignEnd && singleRow ? (root.rtl ? -freeSpace : freeSpace) : 0

    // ---- animated state ---------------------------------------------------
    // These flip the instant `active` changes; the Behaviors below tween them,
    // so a line fades up and grows when it starts being sung, then fades back
    // down and shrinks once the next line takes over - no colour or size snap.
    property real inactiveDim: 0.6
    property real dim: active ? 1.0 : inactiveDim

    scale: active ? 1.0 : sizeReduce
    transformOrigin: root.alignEnd !== root.rtl ? Item.Right : Item.Left

    Behavior on dim {
        NumberAnimation {
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

    width: textItem.width
    height: textItem.height

    // ---- the text itself -------------------------------------------------
    Item {
        id: textItem

        height: flow.height + 2 * root.pad
        width: root.wrapWidth > 0 ? root.wrapWidth : flow.implicitWidth + 2 * root.pad

        WordFlow {
            id: flow

            width: root.wrapWidth > 0 ? Math.max(1, root.wrapWidth - 2 * root.pad) : undefined
            wordSpacing: root.wordSpacing
            x: root.pad + root.alignOffset
            y: root.pad

            rtl: root.rtl
            horizontalAlignment: root.rtl !== root.alignEnd ? Text.AlignRight : Text.AlignLeft

            Repeater {
                id: wordItems

                model: root.words

                Text {
                    id: wordText

                    required property var modelData

                    // A word wider than the line is broken so it cannot
                    // overflow; every other word keeps its natural width. The
                    // comparison uses the Text's own implicit width, so a word
                    // is never sized a fraction short (which would wrap its last
                    // glyph onto a line of its own).
                    readonly property bool tooWide: root.wrapWidth > 0 && flow.width > 0 && implicitWidth > flow.width + 0.5

                    color: root.baseColor
                    // Rebuilt from the animated pixelSize so the glyphs are
                    // re-rasterised crisp at every size instead of being scaled.
                    font: Qt.font({
                        family: root.textFont.family ? root.textFont.family : "",
                        weight: root.textFont.weight,
                        pixelSize: root.textFont.pixelSize
                    })
                    lineHeight: 1.0
                    text: modelData ? modelData.text : ""
                    width: tooWide ? flow.width : implicitWidth
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    // The reading-order coordinate, encoded one gradient rectangle per word.
    // It lives in its own item (never in the glyph texture) and only its red
    // channel is read: an opaque gradient from the word's start to its end
    // along the reading order, normalised by the line's total length. Glyph
    // pixels are always inside a word box, so every visible fragment gets the
    // right reading position even when words wrap onto a new row.
    Item {
        id: coordItem

        anchors.fill: textItem

        Repeater {
            model: root.words

            Rectangle {
                id: coord

                required property int index
                required property var modelData

                readonly property var wordItem: wordItems.count > index ? wordItems.itemAt(index) : null
                readonly property var span: root.wordSpan(index)

                height: wordItem ? wordItem.height : 0
                width: wordItem ? wordItem.width : 0
                x: flow.x + (wordItem ? wordItem.x : 0)
                y: flow.y + (wordItem ? wordItem.y : 0)

                gradient: Gradient {
                    orientation: Gradient.Horizontal

                    GradientStop {
                        color: Qt.rgba(coord.span[root.rtl ? 1 : 0], 0, 0, 1)
                        position: 0.0
                    }

                    GradientStop {
                        color: Qt.rgba(coord.span[root.rtl ? 0 : 1], 0, 0, 1)
                        position: 1.0
                    }
                }
            }
        }
    }

    // The line rendered once into a texture. It is kept transparent because
    // ShaderEffect below draws the visible result; this item only feeds it.
    ShaderEffectSource {
        id: textTexture

        hideSource: true
        opacity: 0
        sourceItem: textItem
    }

    // Same idea for the reading-order coordinate texture.
    ShaderEffectSource {
        id: coordTexture

        hideSource: true
        opacity: 0
        sourceItem: coordItem
    }

    ShaderEffect {
        anchors.fill: textItem

        property real progress: root.progress
        property real edge: root.edge
        property real dim: root.dim
        property real inactiveDim: root.inactiveDim
        property real glow: root.glow
        property color baseColor: root.baseColor
        property color sungColor: root.sungColor
        property color glowColor: root.glowColor
        property var source: textTexture
        property var coords: coordTexture

        fragmentShader: "qrc:/shaders/karaoke.frag.qsb"
        vertexShader: "qrc:/shaders/karaoke.vert.qsb"
    }

    function lerp(t0, x0, t1, x1, t) {
        if (t1 <= t0)
            return x1;
        var f = (t - t0) / (t1 - t0);
        if (f < 0)
            f = 0;
        else if (f > 1)
            f = 1;
        return x0 + (x1 - x0) * f;
    }

    // Normalised [start, end] reading position of word `index`.
    function wordSpan(index) {
        var count = root.wordCount;
        if (count === 0)
            return [0, 0];
        var total = root.readingWidth;
        var spacing = root.wordSpacing;
        var acc = 0;
        for (var i = 0; i < count; ++i) {
            var item = wordItems.itemAt(i);
            var w = item ? item.width : 0;
            if (i === index)
                return [acc / total, (acc + w) / total];
            acc += w + spacing;
        }
        return [0, 0];
    }

    // Sum of every word's width plus the gaps between them. Read from the laid
    // out items, so it is correct once the Flow has wrapped.
    function computeReadingWidth() {
        var count = root.wordCount;
        if (count === 0)
            return 1;
        var spacing = root.wordSpacing;
        var sum = 0;
        for (var i = 0; i < count; ++i) {
            var item = wordItems.itemAt(i);
            sum += item ? item.width : 0;
        }
        var total = sum + spacing * (count - 1);
        return total > 0 ? total : 1;
    }

    // Maps playback time to a position along the reading order, interpolating
    // between the anchors of each word and the gaps between them. Same maths as
    // a single-row line, but on the reading-order axis instead of x.
    function computeProgress(t) {
        var count = root.wordCount;
        if (count === 0)
            return 0;
        if (t <= lineStart)
            return 0;
        if (t >= lineEnd)
            return 1;

        var total = root.readingWidth;
        if (total <= 0)
            return 0;
        var spacing = root.wordSpacing;
        var prevT = lineStart;
        var prevR = 0;
        var acc = 0;

        for (var i = 0; i < count; ++i) {
            var item = wordItems.itemAt(i);
            if (!item)
                break;
            var word = item.modelData;
            if (!word)
                break;
            var w = item.width;
            var r0 = acc;
            var r1 = acc + w;

            if (t <= word.start)          // in the gap before this word
                return lerp(prevT, prevR / total, word.start, r0 / total, t);
            if (t <= word.end)            // inside this word
                return lerp(word.start, r0 / total, word.end, r1 / total, t);

            prevT = word.end;
            prevR = r1;
            acc += w + spacing;
        }

        // Past the last word: sweep the remainder of the line.
        return lerp(prevT, prevR / total, lineEnd, 1, t);
    }
}
