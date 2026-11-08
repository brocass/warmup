#pragma once

#include <QObject>

// The connection to the user's narjo-sing-helper, kept in
// ~/.config/warmup/config.json and written atomically. Environment variables
// (WARMUP_HELPER_URL, WARMUP_ACCESS_KEY, WARMUP_MUSIC_ROOT, WARMUP_QUALITY)
// win over the file, so tests and scripts can drive the app without touching
// real configuration.
class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(const QString &configPath = {}, QObject *parent = nullptr);

    QString helperUrl() const { return m_helperUrl; }
    QString accessKey() const { return m_accessKey; }
    QString musicRoot() const { return m_musicRoot; }
    QString quality() const { return m_quality; }
    bool isConfigured() const { return !m_helperUrl.isEmpty() && !m_accessKey.isEmpty(); }

    void apply(const QString &helperUrl, const QString &accessKey, const QString &musicRoot, const QString &quality);

    // "192.168.1.20:8765/" becomes "http://192.168.1.20:8765".
    static QString normalizeUrl(QString url);
    static QString normalizeQuality(const QString &quality);
    static QString defaultPath();

signals:
    void changed();

private:
    QString m_path;
    QString m_helperUrl;
    QString m_accessKey;
    QString m_musicRoot;
    QString m_quality = "best";
};
