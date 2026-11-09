#include "songstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

SongStore::SongStore(const QString &path, QObject *parent)
    : QObject(parent), m_path(path.isEmpty() ? defaultPath() : path) {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const auto &value : array) {
        const SongInfo song = Metadata::songInfoFromJson(value.toObject());
        if (song.filePath.isEmpty() || m_index.contains(song.filePath))
            continue;
        m_index.insert(song.filePath, m_songs.size());
        m_songs.append(song);
    }
}

QString SongStore::defaultPath() {
    return QDir::homePath() + "/.local/share/warmup/songs.json";
}

SongInfo SongStore::get(const QString &filePath) const {
    const auto it = m_index.constFind(filePath);
    return it == m_index.constEnd() ? SongInfo() : m_songs[it.value()];
}

void SongStore::add(const SongInfo &song) {
    if (song.filePath.isEmpty())
        return;
    const auto existing = m_index.constFind(song.filePath);
    if (existing != m_index.constEnd()) {
        // Later information is richer (a probe after a queue-only sighting),
        // but never let a lesser entry erase a good title or duration.
        SongInfo merged = song;
        if (merged.title.isEmpty())
            merged.title = m_songs[existing.value()].title;
        if (merged.durationSeconds <= 0)
            merged.durationSeconds = m_songs[existing.value()].durationSeconds;
        if (merged.matchPath.isEmpty())
            merged.matchPath = m_songs[existing.value()].matchPath;
        if (merged.artist.isEmpty())
            merged.artist = m_songs[existing.value()].artist;
        if (merged.probeVersion < m_songs[existing.value()].probeVersion)
            merged.probeVersion = m_songs[existing.value()].probeVersion;
        m_songs[existing.value()] = merged;
    } else {
        if (m_songs.size() >= kMaxSongs) {
            m_songs.removeFirst();
            m_index.clear();
            for (int i = 0; i < m_songs.size(); ++i)
                m_index.insert(m_songs[i].filePath, i);
        }
        m_index.insert(song.filePath, m_songs.size());
        m_songs.append(song);
    }
    save();
    emit changed();
}

void SongStore::addAll(const QVector<SongInfo> &songs) {
    for (const auto &song : songs)
        add(song);
}

void SongStore::save() {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QJsonArray array;
    for (const auto &song : m_songs)
        array.append(Metadata::songInfoToJson(song));
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
