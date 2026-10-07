#include "lyricsdocument.h"

#include <QHash>
#include <QStringList>
#include <QVector>

#include <utility>

namespace {

qint64 toMs(const lyrics::Timestamp &timestamp)
{
    return static_cast<qint64>(timestamp.count());
}

qint64 timingStart(const lyrics::OptionalTiming &timing)
{
    return timing ? toMs(timing->start) : -1;
}

qint64 timingEnd(const lyrics::OptionalTiming &timing)
{
    return (timing && timing->end) ? toMs(*timing->end) : -1;
}

// Best-effort word timing for one vocal. Word timing wins; a word without its
// own timing falls back to the span of its syllables. This never invents a
// precision the source did not provide - if nothing is timed, the result is
// "not karaoke".
LyricRow::Timed buildTimed(const lyrics::Vocal &vocal, qint64 lineStartMs, qint64 lineEndMs)
{
    const qsizetype count = static_cast<qsizetype>(vocal.words.size());
    if (count == 0)
        return {};

    QVector<qint64> start(count, -1);
    QVector<qint64> end(count, -1);
    bool anyTimed = false;
    for (qsizetype i = 0; i < count; ++i) {
        const lyrics::Word &word = vocal.words[static_cast<std::size_t>(i)];
        if (word.timing) {
            start[i] = toMs(word.timing->start);
            if (word.timing->end)
                end[i] = toMs(*word.timing->end);
            anyTimed = true;
        } else if (!word.syllables.empty()) {
            for (const lyrics::Syllable &syllable : word.syllables) {
                if (!syllable.timing)
                    continue;
                const qint64 s = toMs(syllable.timing->start);
                const qint64 e = syllable.timing->end ? toMs(*syllable.timing->end) : s;
                if (start[i] < 0 || s < start[i])
                    start[i] = s;
                if (end[i] < 0 || e > end[i])
                    end[i] = e;
                anyTimed = true;
            }
        }
    }
    if (!anyTimed)
        return {};

    qint64 lineStart = lineStartMs;
    qint64 lineEnd = lineEndMs;
    for (qsizetype i = 0; i < count && lineStart < 0; ++i)
        lineStart = start[i];
    for (qsizetype i = count - 1; i >= 0 && lineEnd < 0; --i)
        lineEnd = end[i];
    if (lineStart < 0)
        lineStart = 0;
    if (lineEnd < 0)
        lineEnd = lineStart;

    // Fill the gaps so the renderer always sees a monotonic, fully timed list.
    qint64 cursor = lineStart;
    for (qsizetype i = 0; i < count; ++i) {
        if (start[i] < 0)
            start[i] = cursor;
        if (start[i] < cursor)
            start[i] = cursor;
        cursor = start[i];
    }
    for (qsizetype i = 0; i < count; ++i) {
        if (end[i] < 0)
            end[i] = (i + 1 < count) ? start[i + 1] : lineEnd;
        if (end[i] < start[i])
            end[i] = start[i];
    }

    LyricRow::Timed timed;
    timed.karaoke = true;
    timed.lineStart = lineStart / 1000.0;
    timed.lineEnd = lineEnd / 1000.0;
    timed.words.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        timed.words.append(LyricRow::WordSpec{vocal.words[static_cast<std::size_t>(i)].text,
                                              start[i] / 1000.0, end[i] / 1000.0});
    }
    return timed;
}

QString formatTimestamp(const lyrics::Timestamp &timestamp)
{
    const qint64 total = toMs(timestamp);
    const qint64 minutes = total / 60000;
    const qint64 seconds = (total % 60000) / 1000;
    const qint64 millis = total % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString formatTiming(const lyrics::OptionalTiming &timing)
{
    if (!timing)
        return QStringLiteral("(untimed)");
    QString result = formatTimestamp(timing->start);
    if (timing->end)
        result += QStringLiteral("..") + formatTimestamp(*timing->end);
    return result;
}

QString agentTypeName(lyrics::AgentType type)
{
    switch (type) {
    case lyrics::AgentType::Person:
        return QStringLiteral("person");
    case lyrics::AgentType::Group:
        return QStringLiteral("group");
    case lyrics::AgentType::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

/// One element's active window, in ms. `end` of -1 means "until something else
/// takes over". `start` of -1 means untimed.
struct Window {
    qint64 start = -1;
    qint64 end = -1;
};

} // namespace

// ---------------------------------------------------------------------------
// TimedWord
// ---------------------------------------------------------------------------

TimedWord::TimedWord(QString text, qreal start, qreal end, QObject *parent)
    : QObject(parent), m_text(std::move(text)), m_start(start), m_end(end)
{}

// ---------------------------------------------------------------------------
// LyricRow
// ---------------------------------------------------------------------------

LyricRow::LyricRow(Kind kind, QString text, QString agentName, bool agentIsGroup, bool alignEnd,
                   qint64 startMs, qint64 activeStartMs, qint64 activeEndMs, bool groupStart,
                   Timed timed, QObject *parent)
    : QObject(parent), m_kind(kind), m_text(std::move(text)), m_agentName(std::move(agentName)),
      m_agentIsGroup(agentIsGroup), m_alignEnd(alignEnd), m_startMs(startMs),
      m_activeStartMs(activeStartMs), m_activeEndMs(activeEndMs), m_groupStart(groupStart),
      m_karaoke(timed.karaoke), m_lineStart(timed.lineStart), m_lineEnd(timed.lineEnd)
{
    m_isRtl = lyrics::textDirection(m_text) == lyrics::TextDirection::Rtl;
    m_words.reserve(timed.words.size());
    for (const WordSpec &spec : timed.words)
        m_words.append(new TimedWord(spec.text, spec.start, spec.end, this));
}

// ---------------------------------------------------------------------------
// LyricsDocument
// ---------------------------------------------------------------------------

LyricsDocument::LyricsDocument(QObject *parent) : QObject(parent) {}

LyricsDocument::~LyricsDocument() = default;

void LyricsDocument::setLyrics(lyrics::Lyrics &&document)
{
    m_lyrics = std::move(document);
    m_hasLyrics = true;
    rebuild();
}

LyricRow *LyricsDocument::addRow(LyricRow::Kind kind, const QString &text, const QString &agentName,
                                 bool agentIsGroup, bool alignEnd, qint64 startMs,
                                 qint64 activeStartMs, qint64 activeEndMs, bool groupStart,
                                 LyricRow::Timed timed)
{
    auto *row = new LyricRow(kind, text, agentName, agentIsGroup, alignEnd, startMs, activeStartMs,
                             activeEndMs, groupStart, std::move(timed), this);
    if (activeStartMs >= 0)
        m_timed = true;
    m_rows.append(row);
    return row;
}

void LyricsDocument::resolveAgent(lyrics::Id id, QString *name, bool *isGroup) const
{
    if (const lyrics::Agent *agent = m_lyrics.agent(id)) {
        *name = agent->name;
        *isGroup = agent->type == lyrics::AgentType::Group;
    }
}

void LyricsDocument::rebuild()
{
    qDeleteAll(m_rows);
    m_rows.clear();
    m_timed = false;

    // First pass: one active window per element, so an untimed end can borrow
    // the next element's start.
    QVector<Window> windows;
    windows.reserve(m_lyrics.elements().size());
    for (const std::unique_ptr<lyrics::Element> &element : m_lyrics.elements()) {
        Window window;
        switch (element->kind()) {
        case lyrics::ElementKind::Section: {
            const auto *section = static_cast<const lyrics::Section *>(element.get());
            window.start = timingStart(section->timing);
            window.end = timingEnd(section->timing);
            break;
        }
        case lyrics::ElementKind::Instrumental: {
            const auto *instrumental = static_cast<const lyrics::Instrumental *>(element.get());
            window.start = timingStart(instrumental->timing);
            window.end = timingEnd(instrumental->timing);
            break;
        }
        case lyrics::ElementKind::Line: {
            const auto *line = static_cast<const lyrics::Line *>(element.get());
            window.start = timingStart(line->mainVocal.timing);
            window.end = timingEnd(line->mainVocal.timing);
            break;
        }
        }
        windows.append(window);
    }
    for (qsizetype i = 0; i < windows.size(); ++i) {
        if (windows[i].start < 0 || windows[i].end >= 0)
            continue;
        for (qsizetype j = i + 1; j < windows.size(); ++j) {
            if (windows[j].start >= 0) {
                windows[i].end = windows[j].start;
                break;
            }
        }
        if (windows[i].end >= 0 && windows[i].end < windows[i].start)
            windows[i].end = windows[i].start;
    }

    // Different singers are placed on opposite sides, alternating in the order
    // they first appear (the classic duet layout).
    QHash<quint64, bool> agentSides;
    bool nextEnd = false;
    // Side of the last attributed line, inherited by background-only lines.
    bool lastAlignEnd = false;
    const auto alignEndFor = [&](lyrics::Id id) -> bool {
        if (id == lyrics::InvalidId)
            return false;
        const quint64 key = static_cast<quint64>(id);
        const auto it = agentSides.constFind(key);
        if (it != agentSides.constEnd())
            return it.value();
        const bool end = nextEnd;
        agentSides.insert(key, end);
        nextEnd = !nextEnd;
        return end;
    };

    // Second pass: emit the rows.
    for (qsizetype i = 0; i < windows.size(); ++i) {
        const Window &window = windows[i];
        const std::unique_ptr<lyrics::Element> &element =
            m_lyrics.elements()[static_cast<std::size_t>(i)];
        switch (element->kind()) {
        case lyrics::ElementKind::Section: {
            const auto *section = static_cast<const lyrics::Section *>(element.get());
            addRow(LyricRow::Section, section->name, {}, false, false, window.start, window.start,
                   window.end, true);
            break;
        }
        case lyrics::ElementKind::Instrumental: {
            const auto *instrumental = static_cast<const lyrics::Instrumental *>(element.get());
            const QString text = instrumental->description.isEmpty()
                                     ? QStringLiteral("\u266A instrumental")
                                     : instrumental->description;
            addRow(LyricRow::Instrumental, text, {}, false, false, window.start, window.start,
                   window.end, true);
            break;
        }
        case lyrics::ElementKind::Line: {
            const auto *line = static_cast<const lyrics::Line *>(element.get());

            // A line that is nothing but backgrounds has no singer of its own;
            // it backs the line before it, so it takes that line's side.
            const bool bgOnly = line->mainVocal.text.trimmed().isEmpty() &&
                                line->mainVocal.agentId == lyrics::InvalidId;

            QString who;
            bool isGroup = false;
            resolveAgent(line->mainVocal.agentId, &who, &isGroup);
            const bool alignEnd = alignEndFor(line->mainVocal.agentId);
            const bool rowAlignEnd = bgOnly ? lastAlignEnd : alignEnd;
            if (!bgOnly)
                lastAlignEnd = alignEnd;

            bool firstRow = true;
            if (!line->mainVocal.text.trimmed().isEmpty()) {
                addRow(LyricRow::Main, line->mainVocal.text, who, isGroup, rowAlignEnd,
                       window.start, window.start, window.end, firstRow,
                       buildTimed(line->mainVocal, window.start, window.end));
                firstRow = false;
            }

            // Translations inherit the line's timing (and side), so they
            // highlight with it and a click seeks to the line's start.
            for (const lyrics::Translation &translation : line->translations) {
                addRow(LyricRow::Translation, translation.text, {}, false, rowAlignEnd,
                       window.start, window.start, window.end, firstRow);
                firstRow = false;
            }

            // Background vocals carry their own timing and singer when the
            // source gives them.
            for (const lyrics::Vocal &background : line->backgrounds) {
                // A background inherits the line's window only as a fallback;
                // word timing narrows it to the span the words are actually
                // sung in, so several backgrounds under one line light up one
                // after another instead of all at once.
                qint64 backgroundStart =
                    background.timing ? toMs(background.timing->start) : window.start;
                qint64 backgroundEnd = (background.timing && background.timing->end)
                                           ? toMs(*background.timing->end)
                                           : window.end;
                LyricRow::Timed timed =
                    buildTimed(background, background.timing ? backgroundStart : -1,
                               background.timing && background.timing->end ? backgroundEnd : -1);
                if (timed.karaoke && !background.timing) {
                    backgroundStart = qRound64(timed.lineStart * 1000.0);
                    backgroundEnd = qRound64(timed.lineEnd * 1000.0);
                }

                QString backgroundWho;
                bool backgroundIsGroup = false;
                resolveAgent(background.agentId, &backgroundWho, &backgroundIsGroup);
                // A background sits on the same side as the line it backs, so
                // it stays under its own singer instead of swapping sides.
                addRow(LyricRow::Background, background.text, backgroundWho, backgroundIsGroup,
                       rowAlignEnd, backgroundStart, backgroundStart, backgroundEnd, firstRow,
                       std::move(timed));
                firstRow = false;
            }
            break;
        }
        }
    }

    emit rowsChanged();
}

int LyricsDocument::firstActiveRow(qint64 positionMs) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const LyricRow *row = m_rows.at(i);
        const qint64 start = row->activeStartMs();
        if (start < 0 || positionMs < start)
            continue;
        const qint64 end = row->activeEndMs();
        if (end >= 0 && positionMs >= end)
            continue;
        return i;
    }
    return -1;
}

QString LyricsDocument::debugText() const
{
    if (!m_hasLyrics)
        return QStringLiteral("(no lyrics document)");

    QStringList out;
    const lyrics::Metadata &meta = m_lyrics.metadata();
    out << QStringLiteral("metadata:") << QStringLiteral("  title: ") + meta.title
        << QStringLiteral("  artist: ") + meta.artist << QStringLiteral("  album: ") + meta.album
        << QStringLiteral("  language: ") + meta.language;
    out << QStringLiteral("agents: ") + QString::number(m_lyrics.agents().size());
    for (const lyrics::Agent &agent : m_lyrics.agents()) {
        out << QStringLiteral("  #%1 %2 \"%3\"")
                   .arg(agent.id)
                   .arg(agentTypeName(agent.type))
                   .arg(agent.name);
    }
    out << QStringLiteral("elements: ") + QString::number(m_lyrics.elementCount());

    for (const std::unique_ptr<lyrics::Element> &element : m_lyrics.elements()) {
        switch (element->kind()) {
        case lyrics::ElementKind::Section: {
            const auto *section = static_cast<const lyrics::Section *>(element.get());
            out << QStringLiteral("  [section] \"%1\" %2")
                       .arg(section->name, formatTiming(section->timing));
            break;
        }
        case lyrics::ElementKind::Instrumental: {
            const auto *instrumental = static_cast<const lyrics::Instrumental *>(element.get());
            out << QStringLiteral("  [instrumental] \"%1\" %2")
                       .arg(instrumental->description, formatTiming(instrumental->timing));
            break;
        }
        case lyrics::ElementKind::Line: {
            const auto *line = static_cast<const lyrics::Line *>(element.get());
            const lyrics::Agent *who = m_lyrics.agent(line->mainVocal.agentId);
            QString header = QStringLiteral("  [line #%1] \"%2\" %3")
                                 .arg(line->id())
                                 .arg(line->mainVocal.text, formatTiming(line->mainVocal.timing));
            if (who)
                header += QStringLiteral(" agent=") + who->name;
            out << header;
            for (const lyrics::Word &word : line->mainVocal.words) {
                out << QStringLiteral("    word \"%1\" %2")
                           .arg(word.text, formatTiming(word.timing));
                for (const lyrics::Syllable &syllable : word.syllables)
                    out << QStringLiteral("      syllable \"%1\" %2")
                               .arg(syllable.text, formatTiming(syllable.timing));
            }
            for (const lyrics::Translation &translation : line->translations)
                out << QStringLiteral("    translation [%1] \"%2\"")
                           .arg(translation.language, translation.text);
            for (const lyrics::Vocal &background : line->backgrounds) {
                const lyrics::Agent *backgroundAgent = m_lyrics.agent(background.agentId);
                QString row = QStringLiteral("    background \"%1\" %2")
                                  .arg(background.text, formatTiming(background.timing));
                if (backgroundAgent)
                    row += QStringLiteral(" agent=") + backgroundAgent->name;
                out << row;
            }
            break;
        }
        }
    }
    return out.join(QLatin1Char('\n'));
}
