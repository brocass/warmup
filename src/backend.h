#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

#include "metadata.h"
#include "settings.h"

// Talks to the narjo-sing-helper REST API: turns dropped files into batch
// jobs, and mirrors the helper's queue so the window can show progress.
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
    int probingLeft() const { return m_probeQueue.size() + m_probes.size(); }

    QString helperUrl() const { return m_settings.helperUrl(); }
    QString accessKey() const { return m_settings.accessKey(); }
    QString musicRoot() const { return m_settings.musicRoot(); }
    QString quality() const { return m_settings.quality(); }

    Q_INVOKABLE void enqueueUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void cancelJob(const QString &jobId);
    Q_INVOKABLE void applySettings(const QString &helperUrl, const QString &accessKey, const QString &musicRoot,
                                   const QString &quality);

    // Static, so tests can drive them without a server.
    static QJsonObject songToJson(const SongInfo &song);
    struct Tally {
        int added = 0, ready = 0, missing = 0, failed = 0, other = 0;
    };
    static Tally tallyBatchResponse(const QJsonArray &results);

signals:
    void stateChanged();
    void itemsChanged();
    void statusTextChanged();
    void probingChanged();

private:
    QNetworkRequest request(const QString &path) const;
    void reconnect();
    void fetchHealth();
    void fetchQueue();
    void post(const QString &path, const QJsonDocument &body, const std::function<void(const QJsonValue &)> &done);
    bool handleReply(QNetworkReply *reply, QJsonValue *parsed);
    void setStatus(const QString &text);
    void pumpProbes();
    void flushBatch();

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

    QStringList m_probeQueue;
    QList<QProcess *> m_probes;
    QVector<SongInfo> m_pending;
    int m_probeSkipped = 0;
};
