#include "backend.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>

namespace {
// The helper accepts this many songs per lookup, so batches stay under it
// too rather than testing an undocumented limit.
constexpr int kBatchSize = 200;
constexpr int kMaxProbes = 4;

QString baseName(const QString &relPath) {
    return QFileInfo(relPath).fileName();
}

QString dirName(const QString &relPath) {
    const QString dir = QFileInfo(relPath).path();
    return dir == "." ? QString() : dir;
}
} // namespace

Backend::Backend(QObject *parent) : QObject(parent) {
    m_healthTimer.setInterval(10000);
    connect(&m_healthTimer, &QTimer::timeout, this, &Backend::fetchHealth);
    m_queueTimer.setInterval(2000);
    connect(&m_queueTimer, &QTimer::timeout, this, &Backend::fetchQueue);
    connect(&m_settings, &Settings::changed, this, [this] {
        m_blocked = false;
        reconnect();
        emit stateChanged();
    });
    reconnect();
}

Backend::~Backend() {
    for (QProcess *probe : m_probes) {
        probe->kill();
        probe->waitForFinished(500);
    }
}

void Backend::reconnect() {
    m_connected = false;
    if (m_settings.isConfigured() && !m_blocked) {
        fetchHealth();
        fetchQueue();
        m_healthTimer.start();
        m_queueTimer.start();
    } else {
        m_healthTimer.stop();
        m_queueTimer.stop();
    }
    emit stateChanged();
}

QNetworkRequest Backend::request(const QString &path) const {
    QNetworkRequest req(QUrl(m_settings.helperUrl() + path));
    req.setRawHeader("X-Narjo-Sing-Key", m_settings.accessKey().toUtf8());
    req.setTransferTimeout(15000);
    return req;
}

// Parses the reply body and turns failures into status text. Returns false
// when the reply wasn't usable; *parsed is the reply's JSON otherwise.
bool Backend::handleReply(QNetworkReply *reply, QJsonValue *parsed) {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        m_blocked = true;
        m_healthTimer.stop();
        m_queueTimer.stop();
        m_connected = false;
        setStatus(tr("Access key rejected — check it in Settings (,)"));
        emit stateChanged();
        return false;
    }
    if (reply->error() != QNetworkReply::NoError) {
        m_connected = false;
        setStatus(tr("Can't reach the helper at %1").arg(m_settings.helperUrl()));
        emit stateChanged();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isObject())
        *parsed = doc.object();
    else if (doc.isArray())
        *parsed = doc.array();
    else
        *parsed = QJsonValue();
    return true;
}

void Backend::fetchHealth() {
    if (m_blocked || !m_settings.isConfigured())
        return;
    QNetworkReply *reply = m_nam.get(request("/v1/health"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        QJsonValue parsed;
        if (!handleReply(reply, &parsed))
            return;
        const QJsonObject health = parsed.toObject();
        m_connected = true;
        m_blocked = false;
        m_state = health.value("state").toString();
        m_mode = health.value("mode").toString();
        m_paused = health.value("paused").toBool();
        m_scanning = health.value("libraryScanning").toBool();
        m_version = health.value("version").toString();
        m_device = health.value("device").toString();
        m_fastModel = health.value("fastModel").toString();
        m_bestModel = health.value("configuredBestModel").toString();
        m_bestStatus = health.value("bestStatus").toString();
        m_libraryFiles = qlonglong(health.value("libraryFiles").toDouble());
        // A music-folder problem on the helper is the one error the user can
        // actually fix, so say it plainly instead of just "error".
        const QString problem = health.value("musicFolderProblem").toString();
        const QString error = health.value("error").toString();
        m_healthProblem = !problem.isEmpty() ? problem : error;
        if (m_state == "error")
            setStatus(tr("Helper error: %1").arg(m_healthProblem));
        else if (m_statusText.startsWith(tr("Helper error:")) || m_statusText.startsWith(tr("Can't reach")))
            setStatus({});
        emit stateChanged();
    });
}

void Backend::fetchQueue() {
    if (m_blocked || !m_settings.isConfigured())
        return;
    QNetworkReply *reply = m_nam.get(request("/v1/queue"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        QJsonValue parsed;
        if (!handleReply(reply, &parsed))
            return;
        const QJsonObject queue = parsed.toObject();
        const bool paused = queue.value("paused").toBool();
        if (paused != m_paused) {
            m_paused = paused;
            emit stateChanged();
        }

        QVariantList items;
        for (const auto &value : queue.value("jobs").toArray()) {
            const QJsonObject job = value.toObject();
            const QString relPath = job.value("relPath").toString();
            const QString quality = job.value("quality").toString().isEmpty()
                                        ? job.value("requestedQuality").toString()
                                        : job.value("quality").toString();
            QString subtitle = dirName(relPath);
            if (!quality.isEmpty())
                subtitle += (subtitle.isEmpty() ? "" : " · ") + quality;
            const QString model = job.value("model").toString();
            if (!model.isEmpty())
                subtitle += " · " + model;
            items.append(QVariantMap{{"jobId", job.value("id").toString()},
                                     {"name", baseName(relPath)},
                                     {"subtitle", subtitle},
                                     {"state", job.value("state").toString()},
                                     {"progress", job.value("progress").toDouble()},
                                     {"eta", job.value("etaSeconds").toDouble(-1)},
                                     {"cancellable", true},
                                     {"section", tr("In queue")}});
        }
        for (const auto &value : queue.value("recent").toArray()) {
            const QJsonObject recent = value.toObject();
            const QString relPath = recent.value("relPath").toString();
            QString subtitle = dirName(relPath);
            const QString quality = recent.value("quality").toString();
            if (!quality.isEmpty())
                subtitle += (subtitle.isEmpty() ? "" : " · ") + quality;
            items.append(QVariantMap{{"jobId", QString()},
                                     {"name", baseName(relPath)},
                                     {"subtitle", subtitle},
                                     {"state", "done"},
                                     {"progress", 1.0},
                                     {"eta", -1},
                                     {"cancellable", false},
                                     {"section", tr("Recently ready")}});
        }
        m_items = items;
        emit itemsChanged();
    });
}

void Backend::post(const QString &path, const QJsonDocument &body,
                   const std::function<void(const QJsonValue &)> &done) {
    if (m_blocked || !m_settings.isConfigured())
        return;
    QNetworkRequest req = request(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_nam.post(req, body.toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        QJsonValue parsed;
        if (handleReply(reply, &parsed))
            done(parsed);
    });
}

void Backend::togglePause() {
    post(m_paused ? "/v1/queue/resume" : "/v1/queue/pause", QJsonDocument(), [this](const QJsonValue &) {
        fetchQueue();
    });
}

void Backend::cancelJob(const QString &jobId) {
    if (jobId.isEmpty())
        return;
    post("/v1/jobs/" + jobId + "/cancel", QJsonDocument(), [this](const QJsonValue &) { fetchQueue(); });
}

void Backend::applySettings(const QString &helperUrl, const QString &accessKey, const QString &musicRoot,
                            const QString &quality) {
    m_settings.apply(helperUrl, accessKey, musicRoot, quality);
}

void Backend::enqueueUrls(const QList<QUrl> &urls) {
    if (!m_settings.isConfigured()) {
        setStatus(tr("Set up the helper connection first — press ,"));
        return;
    }
    const QStringList files = Metadata::collectAudioFiles(urls);
    if (files.isEmpty()) {
        setStatus(tr("No audio files in that drop"));
        return;
    }
    for (const QString &file : files)
        if (!m_probeQueue.contains(file))
            m_probeQueue.append(file);
    emit probingChanged();
    pumpProbes();
}

void Backend::pumpProbes() {
    while (m_probes.size() < kMaxProbes && !m_probeQueue.isEmpty()) {
        const QString path = m_probeQueue.takeFirst();
        auto *probe = new QProcess(this);
        m_probes.append(probe);
        connect(probe, &QProcess::finished, this, [this, probe, path] {
            m_probes.removeAll(probe);
            const SongInfo song = Metadata::parseProbe(path, probe->readAllStandardOutput(),
                                                       m_settings.musicRoot());
            const bool readable = probe->exitCode() == 0 && song.ok;
            if (readable)
                m_pending.append(song);
            else
                ++m_probeSkipped;
            probe->deleteLater();
            if (m_pending.size() >= kBatchSize)
                flushBatch();
            pumpProbes();
            if (m_probeQueue.isEmpty() && m_probes.isEmpty()) {
                flushBatch();
                if (m_probeSkipped > 0) {
                    setStatus(m_statusText.isEmpty()
                                  ? tr("%n couldn't be read", nullptr, m_probeSkipped)
                                  : m_statusText + " · " + tr("%n couldn't be read", nullptr, m_probeSkipped));
                    m_probeSkipped = 0;
                }
            }
            emit probingChanged();
        });
        // ffprobe prints one JSON object with the duration and every tag.
        probe->start("ffprobe", {"-v", "error", "-print_format", "json", "-show_entries",
                                 "format=duration:format_tags", "--", path});
    }
}

QJsonObject Backend::songToJson(const SongInfo &song) {
    QJsonObject object{{"clientSongId", song.filePath},
                       {"title", song.title},
                       {"durationSeconds", song.durationSeconds},
                       {"path", song.matchPath}};
    if (!song.albumArtist.isEmpty())
        object.insert("albumArtist", song.albumArtist);
    if (!song.album.isEmpty())
        object.insert("album", song.album);
    if (song.disc > 0)
        object.insert("disc", song.disc);
    if (song.track > 0)
        object.insert("track", song.track);
    return object;
}

void Backend::flushBatch() {
    if (m_pending.isEmpty())
        return;
    QJsonArray songs;
    while (!m_pending.isEmpty() && songs.size() < kBatchSize)
        songs.append(songToJson(m_pending.takeFirst()));
    const QJsonObject body{{"songs", songs}, {"priority", "batch"}, {"quality", m_settings.quality()}};
    post("/v1/jobs/batch", QJsonDocument(body), [this](const QJsonValue &response) {
        const Tally tally = tallyBatchResponse(response.toArray());
        QStringList parts;
        if (tally.added > 0)
            parts.append(tr("%n songs sent to the helper", nullptr, tally.added));
        if (tally.ready > 0)
            parts.append(tr("%n already ready", nullptr, tally.ready));
        if (tally.missing > 0)
            parts.append(tr("%n not found in the helper's library", nullptr, tally.missing));
        if (tally.failed > 0)
            parts.append(tr("%n failed", nullptr, tally.failed));
        if (!parts.isEmpty())
            setStatus(parts.join(" · "));
        fetchQueue();
    });
}

Backend::Tally Backend::tallyBatchResponse(const QJsonArray &results) {
    Tally tally;
    for (const auto &value : results) {
        const QString state = value.toObject().value("state").toString();
        if (state == "queued" || state == "running")
            ++tally.added;
        else if (state == "done")
            ++tally.ready;
        else if (state == "notFound")
            ++tally.missing;
        else if (state == "failed" || state == "expired")
            ++tally.failed;
        else
            ++tally.other;
    }
    return tally;
}

QString Backend::helperLine() const {
    if (!m_settings.isConfigured())
        return tr("Not set up");
    if (!m_connected)
        return tr("Offline");
    QStringList parts;
    if (m_state == "ready")
        parts.append(tr("Ready"));
    else if (m_state == "benchmarking")
        parts.append(tr("Benchmarking…"));
    else if (m_state == "error")
        parts.append(tr("Error"));
    else if (!m_state.isEmpty())
        parts.append(m_state);
    if (!m_mode.isEmpty())
        parts.append(m_mode);
    if (m_paused)
        parts.append(tr("Paused"));
    if (m_scanning)
        parts.append(tr("Checking music…"));
    return parts.join(" · ");
}

QString Backend::healthSummary() const {
    if (!m_connected)
        return {};
    QStringList lines;
    if (!m_version.isEmpty())
        lines.append(tr("Version %1 · %2").arg(m_version, m_device));
    if (m_libraryFiles > 0)
        lines.append(tr("%n songs in the library", nullptr, int(m_libraryFiles)));
    QString models = m_fastModel;
    if (!m_bestModel.isEmpty())
        models += " · " + m_bestModel;
    if (!m_bestStatus.isEmpty() && m_bestStatus != "measured")
        models += " (" + m_bestStatus + ")";
    if (!models.trimmed().isEmpty())
        lines.append(models);
    return lines.join("\n");
}

void Backend::setStatus(const QString &text) {
    if (text == m_statusText)
        return;
    m_statusText = text;
    emit statusTextChanged();
}
