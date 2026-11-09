#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include "metadata.h"
#include "settings.h"
#include "songstore.h"

// One downloaded instrumental: where it lives and how fresh it is, so an
// unchanged stem (same ETag) isn't downloaded again. The collection is the
// playlist name written into its album tag and folder.
struct KaraokeEntry {
    QString instrumentalPath;
    QString etag;
    QString collection;
    QString lrcPath;
    qlonglong lrcSourceMtime = 0;
};

// Talks to the narjo-sing-helper REST API: turns dropped files into batch
// jobs, mirrors the helper's queue so the window can show progress, and keeps
// an M3U playlist of the processed (Sing-ready) tracks.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(QString helperLine READ helperLine NOTIFY stateChanged)
    Q_PROPERTY(QString healthSummary READ healthSummary NOTIFY stateChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int probingLeft READ probingLeft NOTIFY probingChanged)
    Q_PROPERTY(QString helperUrl READ helperUrl NOTIFY stateChanged)
    Q_PROPERTY(QString accessKey READ accessKey NOTIFY stateChanged)
    Q_PROPERTY(QString musicRoot READ musicRoot NOTIFY stateChanged)
    Q_PROPERTY(QString quality READ quality NOTIFY stateChanged)
    Q_PROPERTY(bool playlistEnabled READ playlistEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString playlistPath READ playlistPath NOTIFY stateChanged)
    Q_PROPERTY(bool karaokeEnabled READ karaokeEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString karaokePath READ karaokePath NOTIFY stateChanged)
    Q_PROPERTY(QString karaokeDirectory READ karaokeDirectory NOTIFY stateChanged)
    Q_PROPERTY(int downloadsLeft READ downloadsLeft NOTIFY downloadsChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    bool configured() const { return m_settings.isConfigured(); }
    bool connected() const { return m_connected; }
    bool paused() const { return m_paused; }
    QString helperLine() const;
    QString healthSummary() const;
    QVariantList items() const { return m_items; }
    QString statusText() const { return m_statusText; }
    int probingLeft() const { return m_probeQueue.size() + m_playlistProbeQueue.size() + m_probes.size(); }

    QString helperUrl() const { return m_settings.helperUrl(); }
    QString accessKey() const { return m_settings.accessKey(); }
    QString musicRoot() const { return m_settings.musicRoot(); }
    QString quality() const { return m_settings.quality(); }
    bool playlistEnabled() const { return m_settings.playlistEnabled(); }
    QString playlistPath() const { return m_settings.playlistPath(); }
    bool karaokeEnabled() const { return m_settings.karaokeEnabled(); }
    QString karaokePath() const { return m_settings.karaokePath(); }
    QString karaokeDirectory() const { return m_settings.karaokeDirectory(); }
    int downloadsLeft() const {
        return m_karaokeChunksLeft + m_downloadQueue.size() + m_activeReplies.size() + m_remuxes.size();
    }

    Q_INVOKABLE void enqueueUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void cancelJob(const QString &jobId);
    Q_INVOKABLE void applySettings(const QString &helperUrl, const QString &accessKey, const QString &musicRoot,
                                   const QString &quality);
    Q_INVOKABLE void applyPlaylistSettings(bool enabled, const QString &path);
    Q_INVOKABLE void applyKaraokeSettings(bool enabled, const QString &path, const QString &directory);
    Q_INVOKABLE void updatePlaylist();

    // Static, so tests can drive them without a server.
    static QJsonObject songToJson(const SongInfo &song);
    struct Tally {
        int added = 0, ready = 0, missing = 0, failed = 0, other = 0;
    };
    static Tally tallyBatchResponse(const QJsonArray &results);
    static QString playlistPathFor(const QString &filePath, const QString &playlistDir, const QString &musicRoot);
    static QString relativeIfInside(const QString &path, const QString &directory);
    static QString karaokePathFor(const SongInfo &song, const QString &karaokeDir, const QString &collection);
    static QString buildM3u(const QVector<SongInfo> &songs,
                            const std::function<QString(const SongInfo &)> &pathFor);

signals:
    void stateChanged();
    void itemsChanged();
    void statusTextChanged();
    void probingChanged();
    void downloadsChanged();

private:
    QNetworkRequest request(const QString &path) const;
    void reconnect();
    void fetchHealth();
    void fetchQueue();
    void post(const QString &path, const QJsonDocument &body, const std::function<void(const QJsonValue &)> &done,
              const std::function<void()> &failed = {});
    bool handleReply(QNetworkReply *reply, QJsonValue *parsed);
    bool replyFailedFatally(QNetworkReply *reply);
    void setStatus(const QString &text);
    void pumpProbes();
    void probeFinished(QProcess *probe, const QString &path, bool forPlaylist);
    void flushBatch();
    void startLookups();
    QVector<SongInfo> writePlaylist(const QVector<SongInfo> &ready);

    // The karaoke library: instrumentals downloaded from the helper, remuxed
    // with the original tags, with the original .lrc copied beside them.
    void startKaraoke(const QVector<SongInfo> &readySongs);
    void pumpDownloads();
    void downloadFinished(QNetworkReply *reply);
    void startRemux(const SongInfo &song, const QByteArray &bytes, const QString &etag);
    void startRetag(const SongInfo &song, KaraokeEntry &entry);
    QStringList remuxArgs(const SongInfo &song, const QString &input, const QString &output) const;
    void remuxFinished(QProcess *process);
    void copyLrc(const SongInfo &song, KaraokeEntry &entry);
    QString karaokeCollection() const;
    QString claimKaraokePath(const SongInfo &song);
    void relocateKaraokeFile(const SongInfo &song, KaraokeEntry &entry);
    void pruneKaraokeDirs(QString startDirectory);
    void checkKaraokeDone();
    void writeKaraokePlaylist();
    void loadKaraoke();
    void saveKaraoke() const;

    Settings m_settings;
    QNetworkAccessManager m_nam;
    QTimer m_healthTimer, m_queueTimer;
    bool m_blocked = false; // the key was rejected; wait for new settings

    bool m_connected = false;
    bool m_paused = false;
    bool m_scanning = false;
    QString m_state;  // ready | benchmarking | error
    QString m_mode;   // single | two-tier | fast-only
    QString m_healthProblem;
    QString m_version, m_device;
    QString m_fastModel, m_bestModel, m_bestStatus;
    qlonglong m_libraryFiles = 0;

    QVariantList m_items;
    QString m_statusText;

    QHash<QProcess *, bool> m_probes; // process → true when probing for the playlist
    QStringList m_probeQueue;         // files to probe and submit as jobs
    QStringList m_playlistProbeQueue; // files to probe for the playlist only
    QVector<SongInfo> m_pending;
    int m_probeSkipped = 0;

    SongStore m_store;
    QStringList m_recentPaths;   // from the latest queue snapshot
    QSet<QString> m_seenRecent;  // every relPath ever seen in "recent"
    bool m_seenQueueSnapshot = false;
    QTimer m_playlistDebounce;
    QElapsedTimer m_libraryScanTimer; // throttles full music-folder walks
    bool m_playlistUpdatePending = false;
    bool m_lookupRunning = false;
    bool m_lookupFailed = false;
    int m_lookupChunksLeft = 0;
    QSet<QString> m_readyPaths;

    struct DownloadJob {
        SongInfo song;
        QString jobId;
    };
    struct RemuxJob {
        SongInfo song;
        QString etag;
        QString tmpIn;
        QString tmpOut;
        QString final;
        bool inputIsOldFile = false; // relocation retag: keep the input on failure
    };
    bool m_karaokeRunning = false;
    int m_karaokeChunksLeft = 0;
    int m_karaokeFailures = 0;
    QVector<DownloadJob> m_downloadQueue;
    QHash<QNetworkReply *, SongInfo> m_activeReplies;
    QHash<QProcess *, RemuxJob> m_remuxes;
    QHash<QString, KaraokeEntry> m_karaoke; // source filePath → downloaded instrumental
    QString m_karaokePath;
};
