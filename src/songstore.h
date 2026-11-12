#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "metadata.h"

// Remembers every song Warmup has submitted, plus ready songs seen in the
// helper's queue, so the playlist of processed tracks can be rebuilt after
// those jobs have left the helper's queue history. Stored in
// ~/.local/share/warmup/songs.json, written atomically.
class SongStore : public QObject {
    Q_OBJECT

public:
    explicit SongStore(const QString &path = {}, QObject *parent = nullptr);

    QVector<SongInfo> songs() const { return m_songs; }
    bool contains(const QString &filePath) const { return m_index.contains(filePath); }
    SongInfo get(const QString &filePath) const;
    void add(const SongInfo &song);
    void addAll(const QVector<SongInfo> &songs);
    // Picks up entries another process (the background service) wrote, after
    // that process handed the work back to this one.
    void reload();

    static QString defaultPath();
    // The store mirrors the whole library after a full scan, so it needs
    // headroom past big collections.
    static constexpr int kMaxSongs = 50000;

signals:
    void changed();

private:
    void load();
    void save();

    QString m_path;
    QVector<SongInfo> m_songs;
    QHash<QString, int> m_index; // filePath → row in m_songs
};
