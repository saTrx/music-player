// lyricsdocument.h - the Qt/QML view of a `lyrics::Lyrics`.
//
// The model in lyrics.h is deliberately plain C++: no QObject, no QML. This
// adapter flattens the ordered element list into one row per thing the view
// draws (a main line, its backgrounds, its translations, a section, an
// instrumental) and exposes the handful of facts a renderer needs: the text,
// what kind of row it is, the agent, and the timing used both for "is this row
// being sung?" and for click-to-seek.
//
// Each row carries its own [activeStartMs, activeEndMs) window, so overlapping
// lines can be active together and a line stops being active once its own end
// has passed (rather than lingering until the next line starts).
//
// Rows are immutable and rebuilt whenever a parser hands over a new document, so
// QML only ever reads. A `LyricsDocument` owns the `lyrics::Lyrics` it was
// given, so `Song` can keep one per track and replace its contents when the
// parser runs.

#pragma once

#include "lyrics.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtQml/qqmllist.h> // QQmlListProperty
#include <QtQml/qqmlregistration.h>

/// One timed word, in seconds, for the synced karaoke renderer.
class TimedWord : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LyricRow; not created from QML.")

    Q_PROPERTY(QString text READ text CONSTANT)
    Q_PROPERTY(qreal start READ start CONSTANT)
    Q_PROPERTY(qreal end READ end CONSTANT)

  public:
    TimedWord(QString text, qreal start, qreal end, QObject *parent = nullptr);

    QString text() const { return m_text; }
    qreal start() const { return m_start; }
    qreal end() const { return m_end; }

  private:
    QString m_text;
    qreal m_start;
    qreal m_end;
};

/// One drawn row of a lyrics document.
class LyricRow : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LyricsDocument; not created from QML.")

    Q_PROPERTY(QString text READ text CONSTANT)
    Q_PROPERTY(QString agentName READ agentName CONSTANT)
    Q_PROPERTY(bool agentIsGroup READ agentIsGroup CONSTANT)
    /// True when this row belongs to the second (alternating) singer, so the
    /// view aligns it to the opposite side of the previous singer.
    Q_PROPERTY(bool alignEnd READ alignEnd CONSTANT)
    Q_PROPERTY(bool isMain READ isMain CONSTANT)
    Q_PROPERTY(bool isBackground READ isBackground CONSTANT)
    Q_PROPERTY(bool isTranslation READ isTranslation CONSTANT)
    Q_PROPERTY(bool isSection READ isSection CONSTANT)
    Q_PROPERTY(bool isInstrumental READ isInstrumental CONSTANT)
    Q_PROPERTY(bool isRtl READ isRtl CONSTANT)
    /// Where a click should seek to, in ms, or -1 when the row is untimed.
    Q_PROPERTY(qint64 startMs READ startMs CONSTANT)
    /// True for the first row of a line group, so the view can space groups.
    Q_PROPERTY(bool groupStart READ groupStart CONSTANT)
    /// The row is being sung during [activeStartMs, activeEndMs). `activeEndMs`
    /// of -1 means "until something else takes over". Both -1 when untimed.
    Q_PROPERTY(qint64 activeStartMs READ activeStartMs CONSTANT)
    Q_PROPERTY(qint64 activeEndMs READ activeEndMs CONSTANT)
    /// True when the line has word- or syllable-level timing, so the renderer
    /// should use the synced karaoke view instead of a plain line.
    Q_PROPERTY(bool karaoke READ karaoke CONSTANT)
    /// Word timing for the karaoke view; empty unless `karaoke` is true.
    Q_PROPERTY(QQmlListProperty<TimedWord> words READ words CONSTANT)
    Q_PROPERTY(qreal lineStart READ lineStart CONSTANT)
    Q_PROPERTY(qreal lineEnd READ lineEnd CONSTANT)

  public:
    enum Kind {
        Main,
        Background,
        Translation,
        Section,
        Instrumental,
    };
    Q_ENUM(Kind)

    struct WordSpec {
        QString text;
        qreal start = 0.0;
        qreal end = 0.0;
    };

    /// Everything the karaoke renderer needs for one line, in seconds. Empty
    /// unless the line actually carries word- or syllable-level timing.
    struct Timed {
        bool karaoke = false;
        QVector<WordSpec> words;
        qreal lineStart = 0.0;
        qreal lineEnd = 0.0;
    };

    LyricRow(Kind kind, QString text, QString agentName, bool agentIsGroup, bool alignEnd,
             qint64 startMs, qint64 activeStartMs, qint64 activeEndMs, bool groupStart, Timed timed,
             QObject *parent = nullptr);

    Kind kind() const { return m_kind; }
    QString text() const { return m_text; }
    QString agentName() const { return m_agentName; }
    bool agentIsGroup() const { return m_agentIsGroup; }
    bool alignEnd() const { return m_alignEnd; }

    bool isMain() const { return m_kind == Main; }
    bool isBackground() const { return m_kind == Background; }
    bool isTranslation() const { return m_kind == Translation; }
    bool isSection() const { return m_kind == Section; }
    bool isInstrumental() const { return m_kind == Instrumental; }
    bool isRtl() const { return m_isRtl; }

    qint64 startMs() const { return m_startMs; }
    bool groupStart() const { return m_groupStart; }
    qint64 activeStartMs() const { return m_activeStartMs; }
    qint64 activeEndMs() const { return m_activeEndMs; }

    bool karaoke() const { return m_karaoke; }
    QQmlListProperty<TimedWord> words() { return QQmlListProperty<TimedWord>(this, &m_words); }
    qreal lineStart() const { return m_lineStart; }
    qreal lineEnd() const { return m_lineEnd; }

  private:
    Kind m_kind;
    QString m_text;
    QString m_agentName;
    bool m_agentIsGroup = false;
    bool m_alignEnd = false;
    qint64 m_startMs;
    qint64 m_activeStartMs;
    qint64 m_activeEndMs;
    bool m_groupStart;
    bool m_isRtl = false;
    bool m_karaoke = false;
    qreal m_lineStart = 0.0;
    qreal m_lineEnd = 0.0;
    QList<TimedWord *> m_words;
};

/// The flattened, UI-ready form of a `lyrics::Lyrics`.
class LyricsDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQmlListProperty<LyricRow> rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY rowsChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY rowsChanged)
    /// True when at least one row carries timing (so the view may highlight).
    Q_PROPERTY(bool timed READ timed NOTIFY rowsChanged)
    /// Human-readable dump of the structured model, for debugging a parser.
    Q_PROPERTY(QString debugText READ debugText NOTIFY rowsChanged)

  public:
    explicit LyricsDocument(QObject *parent = nullptr);
    ~LyricsDocument() override;

    /// Takes ownership of `document` and rebuilds the rows.
    void setLyrics(lyrics::Lyrics &&document);

    QQmlListProperty<LyricRow> rows() { return QQmlListProperty<LyricRow>(this, &m_rows); }
    int rowCount() const { return m_rows.size(); }
    bool empty() const { return m_rows.isEmpty(); }
    bool timed() const { return m_timed; }
    QString debugText() const;

    /// Index of the first row being sung at `positionMs`, or -1. Rows are in
    /// document order, so this is the topmost active line.
    Q_INVOKABLE int firstActiveRow(qint64 positionMs) const;

  signals:
    void rowsChanged();

  private:
    void rebuild();
    LyricRow *addRow(LyricRow::Kind kind, const QString &text, const QString &agentName,
                     bool agentIsGroup, bool alignEnd, qint64 startMs, qint64 activeStartMs,
                     qint64 activeEndMs, bool groupStart, LyricRow::Timed timed = {});
    void resolveAgent(lyrics::Id id, QString *name, bool *isGroup) const;

    lyrics::Lyrics m_lyrics;
    bool m_hasLyrics = false;
    bool m_timed = false;
    QList<LyricRow *> m_rows;
};
