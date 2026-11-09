#include "metadata.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {

const QSet<QString> &audioExtensions() {
    static const QSet<QString> extensions{"mp3",  "flac", "m4a", "mp4", "aac", "ogg",  "oga", "opus",
                                          "wav",  "wave", "aif",  "aiff", "wma", "wv",   "mka", "ape"};
    return extensions;
}

// Playlist entries are often relative to the playlist's own folder.
QString resolveEntry(const QString &entry, const QString &playlistDirectory) {
    QString path = entry.trimmed();
    if (path.isEmpty() || path.contains("://"))
        return {};
    if (QDir::isRelativePath(path))
        path = playlistDirectory + "/" + path;
    return QDir::cleanPath(path);
}

QStringList keepAudioFiles(QStringList paths) {
    QStringList kept;
    for (const auto &path : paths)
        if (Metadata::isAudioFile(path) && QFileInfo::exists(path) && !kept.contains(path))
            kept.append(path);
    return kept;
}

} // namespace

bool Metadata::isAudioFile(const QString &path) {
    return audioExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool Metadata::isPlaylist(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "m3u" || suffix == "m3u8" || suffix == "pls";
}

QStringList Metadata::parsePlaylist(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QString directory = QFileInfo(path).absolutePath();
    const bool pls = path.endsWith(".pls", Qt::CaseInsensitive);
    QStringList entries;
    const auto lines = QString::fromUtf8(file.readAll()).split('\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (pls) {
            // PLS entries look like "File12=/music/song.mp3".
            const int equals = line.indexOf('=');
            if (!line.startsWith("File", Qt::CaseInsensitive) || equals < 0)
                continue;
            line = line.mid(equals + 1);
        } else if (line.startsWith('#')) {
            continue;
        }
        const QString resolved = resolveEntry(line, directory);
        if (!resolved.isEmpty())
            entries.append(resolved);
    }
    return keepAudioFiles(entries);
}

QStringList Metadata::collectAudioFiles(const QList<QUrl> &urls) {
    QStringList files;
    for (const auto &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString path = QDir::cleanPath(url.toLocalFile());
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString found = it.next();
                if (isAudioFile(found))
                    files.append(found);
            }
        } else if (info.isFile() && isPlaylist(path)) {
            files.append(parsePlaylist(path));
        } else if (info.isFile() && isAudioFile(path)) {
            files.append(path);
        }
    }
    files.removeDuplicates();
    files.sort(Qt::CaseInsensitive);
    return files;
}

QString Metadata::matchPathFor(const QString &filePath, const QString &musicRoot) {
    const QString root = QDir::cleanPath(musicRoot);
    const QString path = QDir::cleanPath(filePath);
    // The helper matches songs by the end of their path in its own library,
    // so a path relative to the music root matches however the helper mounts
    // it. A file outside the root falls back to its name, which still matches
    // when the name is unique in the library.
    if (path.startsWith(root + "/"))
        return path.mid(root.size() + 1);
    return QFileInfo(path).fileName();
}

SongInfo Metadata::parseProbe(const QString &filePath, const QByteArray &ffprobeJson, const QString &musicRoot) {
    SongInfo song;
    song.filePath = filePath;
    song.matchPath = matchPathFor(filePath, musicRoot);
    song.title = QFileInfo(filePath).completeBaseName();

    const QJsonDocument doc = QJsonDocument::fromJson(ffprobeJson);
    const QJsonObject format = doc.object().value("format").toObject();
    if (format.isEmpty())
        return song;

    double duration = format.value("duration").toString().toDouble();
    // ffprobe's container estimate can be badly wrong for mp3s with broken
    // VBR headers (the helper indexes the true length), so an mp3 probe also
    // asks ffprobe to count frames, which matches the decodable length.
    const QJsonArray streams = doc.object().value("streams").toArray();
    if (!streams.isEmpty()) {
        const QJsonObject stream = streams.first().toObject();
        const qlonglong frames = stream.value("nb_read_frames").toVariant().toLongLong();
        const int rate = stream.value("sample_rate").toVariant().toInt();
        if (frames > 0 && rate > 0)
            duration = double(frames) * mp3SamplesPerFrame(rate) / rate;
    }
    song.durationSeconds = int(duration + 0.5);
    if (song.durationSeconds <= 0)
        return song;
    song.probeVersion = kProbeVersion;

    // Tag keys differ by container (FLAC often uppercases them), so look them
    // up case-insensitively.
    const QJsonObject tags = format.value("tags").toObject();
    QHash<QString, QString> folded;
    for (auto it = tags.begin(); it != tags.end(); ++it)
        folded.insert(it.key().toLower(), it.value().toString());

    auto number = [](const QString &value) { return value.section('/', 0, 0).trimmed().toInt(); };
    if (!folded.value("title").isEmpty())
        song.title = folded.value("title");
    song.albumArtist = folded.value("album_artist");
    if (song.albumArtist.isEmpty())
        song.albumArtist = folded.value("albumartist");
    if (song.albumArtist.isEmpty())
        song.albumArtist = folded.value("artist");
    song.album = folded.value("album");
    song.disc = number(folded.value("disc"));
    song.track = number(folded.value("track"));
    song.ok = true;
    return song;
}

int Metadata::mp3SamplesPerFrame(int sampleRate) {
    return sampleRate > 0 && sampleRate <= 24000 ? 576 : 1152;
}

QJsonObject Metadata::songInfoToJson(const SongInfo &song) {
    return QJsonObject{{"filePath", song.filePath},       {"matchPath", song.matchPath},
                       {"title", song.title},             {"albumArtist", song.albumArtist},
                       {"album", song.album},             {"disc", song.disc},
                       {"track", song.track},             {"durationSeconds", song.durationSeconds},
                       {"probeVersion", song.probeVersion}};
}

SongInfo Metadata::songInfoFromJson(const QJsonObject &json) {
    SongInfo song;
    song.filePath = json.value("filePath").toString();
    song.matchPath = json.value("matchPath").toString();
    song.title = json.value("title").toString();
    song.albumArtist = json.value("albumArtist").toString();
    song.album = json.value("album").toString();
    song.disc = json.value("disc").toInt();
    song.track = json.value("track").toInt();
    song.durationSeconds = json.value("durationSeconds").toInt();
    song.ok = song.durationSeconds > 0;
    song.probeVersion = json.value("probeVersion").toInt();
    return song;
}
