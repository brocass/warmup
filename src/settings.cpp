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
}

QString Settings::defaultPath() {
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

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // QSaveFile writes beside the target and renames on commit, so a crash
    // mid-save can't leave a half-written config.
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        const QJsonObject object{{"helperUrl", m_helperUrl},
                                 {"accessKey", m_accessKey},
                                 {"musicRoot", m_musicRoot},
                                 {"quality", m_quality}};
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
    emit changed();
}
