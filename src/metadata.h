#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

// Everything about local audio: which files count as songs, what playlists
// point at, and what ffprobe says about a file's tags.
struct SongInfo {
    QString filePath;   // absolute local path
    QString matchPath;  // path relative to the music root, or just the file name
    QString title;
    QString artist;     // the track's own artist (e.g. the group on compilations)
    QString albumArtist;
    QString album;
    int disc = 0;
    int track = 0;
    int durationSeconds = 0;
    bool ok = false;    // false when ffprobe couldn't read the file as audio
    bool hasEmbeddedArt = false; // a video stream (cover) rides inside the file
    int probeVersion = 0; // 0 = pre-frame-counted mp3 durations, 1 = pre artist/art
};

namespace Metadata {
    bool isAudioFile(const QString &path);
    bool isPlaylist(const QString &path);
    QStringList parsePlaylist(const QString &path);
    // Files, folders, and playlists become a sorted, de-duplicated list of
    // audio files. Remote URLs are skipped: the helper needs local files.
    QStringList collectAudioFiles(const QList<QUrl> &urls);
    SongInfo parseProbe(const QString &filePath, const QByteArray &ffprobeJson, const QString &musicRoot);
    QString matchPathFor(const QString &filePath, const QString &musicRoot);
    // Samples in one mp3 frame: 576 for MPEG-2/2.5 (≤ 24 kHz), 1152 for MPEG-1.
    int mp3SamplesPerFrame(int sampleRate);
    constexpr int kProbeVersion = 2;
    // Storage shape for the song store; the helper API shape lives in Backend.
    QJsonObject songInfoToJson(const SongInfo &song);
    SongInfo songInfoFromJson(const QJsonObject &json);
}
