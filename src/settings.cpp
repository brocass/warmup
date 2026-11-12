#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

Settings::Settings(const QString &configPath, QObject *parent)
    : QObject(parent), m_path(configPath.isEmpty() ? defaultPath() : configPath) {
    QJsonObject object;
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly))
        object = QJsonDocument::fromJson(file.readAll()).object();

    m_helperUrl = normalizeUrl(object.value("helperUrl").toString());
    m_accessKey = object.value("accessKey").toString().trimmed();
    m_musicRoot = object.value("musicRoot").toString();
    m_quality = normalizeQuality(object.value("quality").toString());

    if (m_musicRoot.isEmpty()) {
        const QString music = QDir::homePath() + "/Music";
        m_musicRoot = QFileInfo::exists(music) ? music : QDir::homePath();
    }

    const QString envUrl = qEnvironmentVariable("WARMUP_HELPER_URL").trimmed();
    if (!envUrl.isEmpty())
        m_helperUrl = normalizeUrl(envUrl);
    const QString envKey = qEnvironmentVariable("WARMUP_ACCESS_KEY").trimmed();
    if (!envKey.isEmpty())
        m_accessKey = envKey;
    const QString envRoot = qEnvironmentVariable("WARMUP_MUSIC_ROOT").trimmed();
    if (!envRoot.isEmpty())
        m_musicRoot = envRoot;
    const QString envQuality = qEnvironmentVariable("WARMUP_QUALITY").trimmed();
    if (!envQuality.isEmpty())
        m_quality = normalizeQuality(envQuality);

    m_playlistEnabled = object.value("playlistEnabled").toBool();
    m_playlistPath = normalizePlaylistPath(object.value("playlistPath").toString(), m_musicRoot);
    if (m_playlistPath.isEmpty()) {
        // Pre-0.4 configs had a folder plus a name; fold them into one path.
        const QString legacyDir = object.value("playlistDirectory").toString().trimmed();
        const QString legacyName = object.value("playlistName").toString().trimmed();
        if (!legacyDir.isEmpty() || !legacyName.isEmpty())
            m_playlistPath = normalizePlaylistPath(
                (legacyDir.isEmpty() ? m_musicRoot : legacyDir) + "/" +
                    (legacyName.isEmpty() ? QString("Sing ready") : legacyName),
                m_musicRoot);
        else
            m_playlistPath = m_musicRoot + "/Sing ready.m3u";
    }

    const QString envEnabled = qEnvironmentVariable("WARMUP_PLAYLIST_ENABLED").trimmed();
    if (!envEnabled.isEmpty())
        m_playlistEnabled = envEnabled == "1" || envEnabled == "true";
    const QString envPath = qEnvironmentVariable("WARMUP_PLAYLIST_PATH").trimmed();
    if (!envPath.isEmpty())
        m_playlistPath = normalizePlaylistPath(envPath, m_musicRoot);

    m_karaokeEnabled = object.value("karaokeEnabled").toBool();
    m_karaokePath = normalizePlaylistPath(object.value("karaokePath").toString(), m_musicRoot);
    if (m_karaokePath.isEmpty()) {
        const QString legacyDir = object.value("playlistDirectory").toString().trimmed();
        const QString legacyName = object.value("karaokeName").toString().trimmed();
        if (!legacyDir.isEmpty() || !legacyName.isEmpty())
            m_karaokePath = normalizePlaylistPath(
                (legacyDir.isEmpty() ? m_musicRoot : legacyDir) + "/" +
                    (legacyName.isEmpty() ? QString("Sing karaoke") : legacyName),
                m_musicRoot);
        else
            m_karaokePath = m_musicRoot + "/Sing karaoke.m3u";
    }
    // The instrumentals deliberately live outside the music folder by
    // default, so the helper and music servers don't index them as songs.
    m_karaokeDirectory = object.value("karaokeDirectory").toString().trimmed();
    if (m_karaokeDirectory.isEmpty())
        m_karaokeDirectory = QDir::homePath() + "/Karaoke";

    const QString envKaraokeEnabled = qEnvironmentVariable("WARMUP_KARAOKE_ENABLED").trimmed();
    if (!envKaraokeEnabled.isEmpty())
        m_karaokeEnabled = envKaraokeEnabled == "1" || envKaraokeEnabled == "true";
    const QString envKaraokePath = qEnvironmentVariable("WARMUP_KARAOKE_PATH").trimmed();
    if (!envKaraokePath.isEmpty())
        m_karaokePath = normalizePlaylistPath(envKaraokePath, m_musicRoot);
    const QString envKaraokeDirectory = qEnvironmentVariable("WARMUP_KARAOKE_DIRECTORY").trimmed();
    if (!envKaraokeDirectory.isEmpty())
        m_karaokeDirectory = envKaraokeDirectory;

    m_serviceEnabled = object.value("serviceEnabled").toBool();
    const QString envService = qEnvironmentVariable("WARMUP_SERVICE_ENABLED").trimmed();
    if (!envService.isEmpty())
        m_serviceEnabled = envService == "1" || envService == "true";
}

QString Settings::defaultPath() {
    // Tests and the offscreen harness keep their configuration out of the
    // real user profile.
    const QString override = qEnvironmentVariable("WARMUP_CONFIG_PATH").trimmed();
    if (!override.isEmpty())
        return override;
    return QDir::homePath() + "/.config/warmup/config.json";
}

QString Settings::normalizeUrl(QString url) {
    url = url.trimmed();
    while (url.endsWith('/'))
        url.chop(1);
    if (!url.isEmpty() && !url.contains("://"))
        url.prepend("http://");
    return url;
}

QString Settings::normalizeQuality(const QString &quality) {
    static const QStringList choices{"auto", "fast", "best", "both"};
    return choices.contains(quality) ? quality : QString("best");
}

void Settings::apply(const QString &helperUrl, const QString &accessKey, const QString &musicRoot,
                     const QString &quality) {
    const QString url = normalizeUrl(helperUrl);
    const QString key = accessKey.trimmed();
    const QString root = musicRoot.trimmed().isEmpty() ? m_musicRoot : QDir::cleanPath(musicRoot.trimmed());
    const QString choice = normalizeQuality(quality);
    if (url == m_helperUrl && key == m_accessKey && root == m_musicRoot && choice == m_quality)
        return;

    m_helperUrl = url;
    m_accessKey = key;
    m_musicRoot = root;
    m_quality = choice;
    save();
    emit changed();
}

QString Settings::normalizePlaylistPath(QString path, const QString &musicRoot) {
    path = path.trimmed();
    if (path.isEmpty())
        return {};
    if (QDir::isRelativePath(path))
        path = QDir::cleanPath(musicRoot) + "/" + path;
    if (!path.endsWith(".m3u", Qt::CaseInsensitive))
        path += ".m3u";
    return QDir::cleanPath(path);
}

void Settings::applyPlaylist(bool enabled, const QString &path) {
    const QString normalized = normalizePlaylistPath(path, m_musicRoot);
    const QString final = normalized.isEmpty() ? m_musicRoot + "/Sing ready.m3u" : normalized;
    if (enabled == m_playlistEnabled && final == m_playlistPath)
        return;

    m_playlistEnabled = enabled;
    m_playlistPath = final;
    save();
    emit changed();
}

void Settings::applyKaraoke(bool enabled, const QString &path, const QString &directory) {
    const QString normalized = normalizePlaylistPath(path, m_musicRoot);
    const QString final = normalized.isEmpty() ? m_musicRoot + "/Sing karaoke.m3u" : normalized;
    const QString trimmedDirectory =
        QDir::cleanPath(directory.trimmed().isEmpty() ? QDir::homePath() + "/Karaoke" : directory.trimmed());
    if (enabled == m_karaokeEnabled && final == m_karaokePath && trimmedDirectory == m_karaokeDirectory)
        return;

    m_karaokeEnabled = enabled;
    m_karaokePath = final;
    m_karaokeDirectory = trimmedDirectory;
    save();
    emit changed();
}

void Settings::applyService(bool enabled) {
    if (enabled == m_serviceEnabled)
        return;
    m_serviceEnabled = enabled;
    save();
    emit changed();
}

void Settings::save() {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // QSaveFile writes beside the target and renames on commit, so a crash
    // mid-save can't leave a half-written config.
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        const QJsonObject object{{"helperUrl", m_helperUrl},
                                 {"accessKey", m_accessKey},
                                 {"musicRoot", m_musicRoot},
                                 {"quality", m_quality},
                                 {"playlistEnabled", m_playlistEnabled},
                                 {"playlistPath", m_playlistPath},
                                 {"karaokeEnabled", m_karaokeEnabled},
                                 {"karaokePath", m_karaokePath},
                                 {"karaokeDirectory", m_karaokeDirectory},
                                 {"serviceEnabled", m_serviceEnabled}};
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
