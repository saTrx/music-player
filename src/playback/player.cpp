#include "player.h"

#include "library/library.h"
#include "library/song.h"

#include <QRandomGenerator>
#include <QUrl>

#include <gst/gst.h>

Player::Player(QObject *parent) : QObject(parent)
{
    gst_init(nullptr, nullptr);

    m_playbin = gst_element_factory_make("playbin", "karaoke-player");
    if (!m_playbin) {
        m_error = QStringLiteral("GStreamer playbin is not available");
        emit errorStringChanged();
        return;
    }

    // Escape hatch for headless testing: KARAOKE_AUDIO_SINK=fakesink.
    const QByteArray sinkName = qgetenv("KARAOKE_AUDIO_SINK");
    if (!sinkName.isEmpty()) {
        if (GstElement *sink = gst_element_factory_make(sinkName.constData(), nullptr))
            g_object_set(m_playbin, "audio-sink", sink, nullptr);
    }

    m_bus = gst_element_get_bus(m_playbin);

    m_pollTimer.setInterval(10);
    connect(&m_pollTimer, &QTimer::timeout, this, &Player::poll);
    m_pollTimer.start();
}

Player::~Player()
{
    m_pollTimer.stop();
    if (m_playbin)
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    if (m_bus)
        gst_object_unref(m_bus);
    if (m_playbin)
        gst_object_unref(m_playbin);
}

Song *Player::currentSong() const
{
    if (m_index < 0 || m_index >= m_queue.size())
        return nullptr;
    return m_queue.at(m_index);
}

void Player::setVolume(qreal volume)
{
    volume = qBound<qreal>(0.0, volume, 1.0);
    if (qFuzzyCompare(m_volume, volume))
        return;
    m_volume = volume;
    if (m_playbin)
        g_object_set(m_playbin, "volume", double(m_volume), nullptr);
    emit volumeChanged();
}

void Player::setShuffle(bool shuffle)
{
    if (m_shuffle == shuffle)
        return;
    m_shuffle = shuffle;

    // Shuffle rearranges the queue itself, keeping the current track playing;
    // turning it off restores the original order.
    if (!m_originalQueue.isEmpty()) {
        Song *current = currentSong();
        m_queue = m_originalQueue;
        if (m_shuffle)
            shuffleQueue();
        const int index = current ? m_queue.indexOf(current) : -1;
        m_index = index;
        emit queueChanged();
        emit currentIndexChanged();
    }

    emit shuffleChanged();
}

void Player::shuffleQueue()
{
    for (int i = m_queue.size() - 1; i > 0; --i)
        m_queue.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
}

void Player::playFromLibrary(int libraryIndex)
{
    MusicLibrary *library = MusicLibrary::instance();
    if (!library)
        return;

    const QList<Song *> &songs = library->songList();
    Song *chosen =
        (libraryIndex >= 0 && libraryIndex < songs.size()) ? songs.at(libraryIndex) : nullptr;

    m_originalQueue = songs;
    m_queue = m_originalQueue;
    if (m_shuffle)
        shuffleQueue();
    emit queueChanged();

    if (m_queue.isEmpty())
        return;

    const int index = chosen ? m_queue.indexOf(chosen) : 0;
    playAt(index >= 0 ? index : 0);
}

void Player::playQueueIndex(int queueIndex)
{
    playAt(queueIndex);
}

void Player::playAt(int queueIndex)
{
    if (queueIndex < 0 || queueIndex >= m_queue.size())
        return;
    m_index = queueIndex;
    emit currentIndexChanged();
    emit currentSongChanged();
    loadCurrent();
}

void Player::loadCurrent()
{
    Song *song = currentSong();
    if (!song || !m_playbin) {
        stopInternal();
        return;
    }

    m_error.clear();
    emit errorStringChanged();

    m_duration = song->duration();
    emit durationChanged();
    m_position = 0.0;
    emit positionChanged();

    // A null -> playing cycle makes playbin pick up the new URI cleanly.
    gst_element_set_state(m_playbin, GST_STATE_NULL);
    const QByteArray uri = song->source().toString(QUrl::FullyEncoded).toUtf8();
    g_object_set(m_playbin, "uri", uri.constData(), nullptr);
    gst_element_set_state(m_playbin, GST_STATE_PLAYING);

    m_playing = true;
    emit playingChanged();
}

void Player::stopInternal()
{
    if (m_playbin)
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    if (m_playing) {
        m_playing = false;
        emit playingChanged();
    }
    if (m_position != 0.0) {
        m_position = 0.0;
        emit positionChanged();
    }
}

void Player::setPlaying(bool playing)
{
    if (m_queue.isEmpty() || m_index < 0)
        return;
    m_playing = playing;
    if (m_playbin)
        gst_element_set_state(m_playbin, playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    emit playingChanged();
}

void Player::toggle()
{
    setPlaying(!m_playing);
}

void Player::advance(bool automatic)
{
    if (m_queue.isEmpty())
        return;

    // Repeat-one only applies to songs ending on their own; tapping next still
    // moves on.
    if (automatic && m_repeat == RepeatOne) {
        seek(0.0);
        setPlaying(true);
        return;
    }

    // The queue is already in play order (shuffled when shuffle is on), so next
    // is simply the following entry.
    const int count = m_queue.size();
    int next = m_index + 1;
    if (next >= count) {
        if (m_repeat == RepeatAll) {
            next = 0;
        } else {
            if (automatic)
                setPlaying(false);
            return;
        }
    }
    playAt(next);
}

void Player::next()
{
    advance(false);
}

void Player::previous()
{
    if (m_queue.isEmpty())
        return;
    // Restart the song first, like most players.
    if (m_position > 3.0) {
        seek(0.0);
        return;
    }
    const int count = m_queue.size();
    int previousIndex = m_index - 1;
    if (previousIndex < 0) {
        if (m_repeat == RepeatAll) {
            previousIndex = count - 1;
        } else {
            seek(0.0);
            return;
        }
    }
    playAt(previousIndex);
}

void Player::seek(qreal seconds)
{
    if (!m_playbin)
        return;
    const gint64 target = gint64(qMax<qreal>(0.0, seconds) * GST_SECOND);
    gst_element_seek_simple(m_playbin, GST_FORMAT_TIME,
                            GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), target);
}

void Player::cycleRepeat()
{
    m_repeat = RepeatMode((m_repeat + 1) % 3);
    emit repeatModeChanged();
}

void Player::toggleShuffle()
{
    setShuffle(!m_shuffle);
}

void Player::poll()
{
    if (!m_bus)
        return;

    const GstMessageType wanted =
        GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_DURATION_CHANGED);
    while (GstMessage *message = gst_bus_pop_filtered(m_bus, wanted)) {
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            advance(true);
            break;
        case GST_MESSAGE_DURATION_CHANGED:
            updateDuration();
            break;
        case GST_MESSAGE_ERROR: {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            m_error = error ? QString::fromUtf8(error->message) : QStringLiteral("Playback error");
            if (error)
                g_error_free(error);
            g_free(debug);
            m_playing = false;
            emit playingChanged();
            emit errorStringChanged();
            gst_element_set_state(m_playbin, GST_STATE_NULL);
            break;
        }
        default:
            break;
        }
        gst_message_unref(message);
    }

    updatePosition();
}

void Player::updatePosition()
{
    if (!m_playbin)
        return;
    gint64 position = 0;
    if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &position) && position >= 0) {
        const qreal seconds = qreal(position) / GST_SECOND;
        if (!qFuzzyCompare(1.0 + seconds, 1.0 + m_position)) {
            m_position = seconds;
            emit positionChanged();
        }
    }
}

void Player::updateDuration()
{
    if (!m_playbin)
        return;
    gint64 duration = 0;
    if (gst_element_query_duration(m_playbin, GST_FORMAT_TIME, &duration) && duration > 0) {
        const qreal seconds = qreal(duration) / GST_SECOND;
        if (!qFuzzyCompare(1.0 + seconds, 1.0 + m_duration)) {
            m_duration = seconds;
            emit durationChanged();
        }
    }
}
