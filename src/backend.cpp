#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
// The helper accepts at most this many songs per lookup, so batches stay
// under it too rather than testing an undocumented limit.
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

Backend::Backend(QObject *parent) : QObject(parent), m_store(qEnvironmentVariable("WARMUP_STORE_PATH")) {
    m_karaokePath = qEnvironmentVariable("WARMUP_KARAOKE_STORE_PATH");
    if (m_karaokePath.isEmpty())
        m_karaokePath = QDir::homePath() + "/.local/share/warmup/karaoke.json";
    loadKaraoke();
    m_healthTimer.setInterval(10000);
    connect(&m_healthTimer, &QTimer::timeout, this, &Backend::fetchHealth);
    m_queueTimer.setInterval(2000);
    connect(&m_queueTimer, &QTimer::timeout, this, &Backend::fetchQueue);
    m_playlistDebounce.setSingleShot(true);
    m_playlistDebounce.setInterval(3000);
    connect(&m_playlistDebounce, &QTimer::timeout, this, &Backend::updatePlaylist);
    connect(&m_settings, &Settings::changed, this, [this] {
        m_blocked = false;
        reconnect();
        emit stateChanged();
    });
    reconnect();
}

Backend::~Backend() {
    for (QProcess *probe : m_probes.keys()) {
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

// Turns fatal reply failures (a rejected key, an unreachable helper) into
// status text. Does not consume the body. 3xx is not an error here.
bool Backend::replyFailedFatally(QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        m_blocked = true;
        m_healthTimer.stop();
        m_queueTimer.stop();
        m_connected = false;
        setStatus(tr("Access key rejected — check it in Settings (,)"));
        emit stateChanged();
        return true;
    }
    if (reply->error() != QNetworkReply::NoError) {
        m_connected = false;
        setStatus(tr("Can't reach the helper at %1").arg(m_settings.helperUrl()));
        emit stateChanged();
        return true;
    }
    return false;
}

// Parses the reply body and turns failures into status text. Returns false
// when the reply wasn't usable; *parsed is the reply's JSON otherwise.
bool Backend::handleReply(QNetworkReply *reply, QJsonValue *parsed) {
    reply->deleteLater();
    if (replyFailedFatally(reply))
        return false;
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

        QStringList recentPaths;
        for (const auto &value : queue.value("recent").toArray()) {
            const QJsonObject recent = value.toObject();
            const QString relPath = recent.value("relPath").toString();
            recentPaths.append(relPath);
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

        // A song appearing in "recent" means its stems just became ready (who
        //ever asked for them — Warmup or the phone), so the auto-updating
        // playlist rewrites itself on every fresh sighting. The first snapshot
        // after launch also triggers one, catching anything processed while
        // Warmup wasn't running.
        const bool firstSnapshot = !m_seenQueueSnapshot;
        m_seenQueueSnapshot = true;
        bool fresh = false;
        for (const QString &path : recentPaths)
            if (!m_seenRecent.contains(path)) {
                m_seenRecent.insert(path);
                fresh = true;
            }
        m_recentPaths = recentPaths;
        if (m_settings.playlistEnabled() && !recentPaths.isEmpty() && (fresh || firstSnapshot))
            m_playlistDebounce.start();
    });
}

void Backend::post(const QString &path, const QJsonDocument &body,
                   const std::function<void(const QJsonValue &)> &done, const std::function<void()> &failed) {
    if (m_blocked || !m_settings.isConfigured()) {
        if (failed)
            failed();
        return;
    }
    QNetworkRequest req = request(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_nam.post(req, body.toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, failed] {
        QJsonValue parsed;
        if (handleReply(reply, &parsed))
            done(parsed);
        else if (failed)
            failed();
    });
}

void Backend::togglePause() {
    post(m_paused ? "/v1/queue/resume" : "/v1/queue/pause", QJsonDocument(),
         [this](const QJsonValue &) { fetchQueue(); });
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

void Backend::applyPlaylistSettings(bool enabled, const QString &path) {
    m_settings.applyPlaylist(enabled, path);
    emit stateChanged();
    if (enabled)
        updatePlaylist();
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
    while (m_probes.size() < kMaxProbes && (!m_probeQueue.isEmpty() || !m_playlistProbeQueue.isEmpty())) {
        // Songs waiting to be submitted come first; playlist probes fill the gaps.
        const bool forPlaylist = m_probeQueue.isEmpty();
        const QString path = forPlaylist ? m_playlistProbeQueue.takeFirst() : m_probeQueue.takeFirst();
        auto *probe = new QProcess(this);
        m_probes.insert(probe, forPlaylist);
        connect(probe, &QProcess::finished, this,
                [this, probe, path, forPlaylist] { probeFinished(probe, path, forPlaylist); });
        // ffprobe prints one JSON object with the duration and every tag.
        probe->start("ffprobe", {"-v", "error", "-print_format", "json", "-show_entries",
                                 "format=duration:format_tags", "--", path});
    }
}

void Backend::probeFinished(QProcess *probe, const QString &path, bool forPlaylist) {
    m_probes.remove(probe);
    const SongInfo song = Metadata::parseProbe(path, probe->readAllStandardOutput(), m_settings.musicRoot());
    const bool readable = probe->exitCode() == 0 && song.ok;
    probe->deleteLater();

    if (forPlaylist) {
        if (readable)
            m_store.add(song);
    } else {
        if (readable)
            m_pending.append(song);
        else
            ++m_probeSkipped;
        if (m_pending.size() >= kBatchSize)
            flushBatch();
    }

    pumpProbes();
    if (m_probeQueue.isEmpty() && m_playlistProbeQueue.isEmpty() && m_probes.isEmpty()) {
        flushBatch();
        if (m_probeSkipped > 0) {
            setStatus(m_statusText.isEmpty()
                          ? tr("%n couldn't be read", nullptr, m_probeSkipped)
                          : m_statusText + " · " + tr("%n couldn't be read", nullptr, m_probeSkipped));
            m_probeSkipped = 0;
        }
        if (m_playlistUpdatePending) {
            m_playlistUpdatePending = false;
            startLookups();
        }
    }
    emit probingChanged();
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
    QVector<SongInfo> chunk;
    QJsonArray songs;
    while (!m_pending.isEmpty() && songs.size() < kBatchSize) {
        chunk.append(m_pending.takeFirst());
        songs.append(songToJson(chunk.last()));
    }
    const QJsonObject body{{"songs", songs}, {"priority", "batch"}, {"quality", m_settings.quality()}};
    post("/v1/jobs/batch", QJsonDocument(body), [this, chunk](const QJsonValue &response) {
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

        // Remember every song the helper accepted, so the playlist can
        // include it once its stems are ready.
        QHash<QString, SongInfo> byId;
        for (const auto &song : chunk)
            byId.insert(song.filePath, song);
        QVector<SongInfo> accepted;
        for (const auto &value : response.toArray()) {
            const QJsonObject result = value.toObject();
            const QString state = result.value("state").toString();
            if ((state == "queued" || state == "running" || state == "done") &&
                byId.contains(result.value("clientSongId").toString()))
                accepted.append(byId.value(result.value("clientSongId").toString()));
        }
        if (!accepted.isEmpty())
            m_store.addAll(accepted);
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

void Backend::applyKaraokeSettings(bool enabled, const QString &path, const QString &directory) {
    m_settings.applyKaraoke(enabled, path, directory);
    emit stateChanged();
    if (enabled)
        updatePlaylist();
}

void Backend::updatePlaylist() {
    if (!m_settings.isConfigured()) {
        setStatus(tr("Set up the helper connection first — press ,"));
        return;
    }
    if (m_lookupRunning || m_karaokeRunning) {
        m_playlistUpdatePending = true;
        return;
    }

    // Discover every song the helper might have processed — including ones
    // prepared long ago or at the phone's request that fell out of the
    // queue's recent list — by walking the whole music folder. New files are
    // probed so the readiness lookup has their title and duration. Walking is
    // cheap, but on a big or remote library it isn't free, so it's throttled.
    if (!m_libraryScanTimer.isValid() || m_libraryScanTimer.elapsed() > 10 * 60 * 1000) {
        m_libraryScanTimer.start();
        const QStringList library = Metadata::collectAudioFiles({QUrl::fromLocalFile(m_settings.musicRoot())});
        for (const QString &file : library)
            if (!m_store.contains(file) && !m_probeQueue.contains(file) &&
                !m_playlistProbeQueue.contains(file))
                m_playlistProbeQueue.append(file);
    }

    // Songs seen in the queue's "recent" list but never submitted from here
    // (usually prepared at the phone's request) join the store; local files
    // get probed first so the playlist has their real title and duration.
    QVector<SongInfo> known;
    for (const QString &relPath : m_recentPaths) {
        const QString filePath = m_settings.musicRoot() + "/" + relPath;
        if (m_store.contains(filePath) || m_playlistProbeQueue.contains(filePath))
            continue;
        if (QFileInfo::exists(filePath)) {
            m_playlistProbeQueue.append(filePath);
        } else {
            SongInfo song;
            song.filePath = filePath;
            song.matchPath = relPath;
            song.title = QFileInfo(relPath).completeBaseName();
            known.append(song);
        }
    }
    if (!known.isEmpty())
        m_store.addAll(known);
    if (!m_playlistProbeQueue.isEmpty()) {
        m_playlistUpdatePending = true;
        pumpProbes();
        emit probingChanged();
        return;
    }
    startLookups();
}

void Backend::startLookups() {
    // Entries without a duration (ready songs heard about through the queue,
    // whose files aren't local) would fail validation for the whole chunk.
    QVector<SongInfo> songs;
    for (const auto &song : m_store.songs())
        if (song.durationSeconds > 0)
            songs.append(song);
    if (songs.isEmpty()) {
        setStatus(tr("No ready songs yet"));
        return;
    }

    m_lookupRunning = true;
    m_lookupFailed = false;
    m_readyPaths.clear();
    // "recent" entries have stems by definition, whatever the lookup says.
    for (const QString &relPath : m_recentPaths)
        m_readyPaths.insert(m_settings.musicRoot() + "/" + relPath);

    m_lookupChunksLeft = (songs.size() + kBatchSize - 1) / kBatchSize;
    for (int offset = 0; offset < songs.size(); offset += kBatchSize) {
        QJsonArray chunk;
        for (int i = offset; i < qMin(offset + kBatchSize, songs.size()); ++i)
            chunk.append(songToJson(songs[i]));
        // "auto" counts a song as ready no matter which tier its stems are.
        const QJsonObject body{{"songs", chunk}, {"quality", "auto"}};
        post("/v1/stems/lookup", QJsonDocument(body),
             [this](const QJsonValue &response) {
                 for (const auto &value : response.toArray()) {
                     const QJsonObject result = value.toObject();
                     if (result.value("ready").toBool())
                         m_readyPaths.insert(result.value("clientSongId").toString());
                 }
                 if (--m_lookupChunksLeft == 0) {
                     m_lookupRunning = false;
                     if (!m_lookupFailed) {
                         QVector<SongInfo> ready;
                         for (const auto &song : m_store.songs())
                             if (m_readyPaths.contains(song.filePath))
                                 ready.append(song);
                         if (m_settings.playlistEnabled())
                             writePlaylist(ready);
                         if (m_settings.karaokeEnabled() && !ready.isEmpty())
                             startKaraoke(ready);
                         else
                             checkKaraokeDone();
                     } else {
                         checkKaraokeDone();
                     }
                 }
             },
             [this] {
                 // Without every answer the ready set would be incomplete and
                 // the rewritten playlist could lose songs, so skip this round.
                 m_lookupFailed = true;
                 if (--m_lookupChunksLeft == 0)
                     m_lookupRunning = false;
             });
    }
}

QString Backend::playlistPathFor(const QString &filePath, const QString &playlistDir, const QString &musicRoot) {
    const QString dir = QDir::cleanPath(playlistDir);
    const QString root = QDir::cleanPath(musicRoot);
    // Inside the music folder, paths stay relative so a music server reading
    // the same library resolves them against its own mount; anywhere else the
    // absolute local path is the only one a player can use.
    if (dir == root || dir.startsWith(root + "/"))
        return QDir(dir).relativeFilePath(QDir::cleanPath(filePath));
    return QDir::cleanPath(filePath);
}

QString Backend::relativeIfInside(const QString &path, const QString &directory) {
    const QString dir = QDir::cleanPath(directory);
    const QString clean = QDir::cleanPath(path);
    if (clean.startsWith(dir + "/"))
        return QDir(dir).relativeFilePath(clean);
    return clean;
}

// Instrumentals live flat in one folder per karaoke playlist
// (<karaokeDir>/<collection>/<Artist> - <Title>.m4a) instead of mirroring
// the library's artist/album tree: music players then see them as their own
// "album" rather than as duplicates of the originals.
QString Backend::karaokePathFor(const SongInfo &song, const QString &karaokeDir, const QString &collection) {
    static const QRegularExpression unsafe("[/\\\\:*?\"<>|\\x00-\\x1f]");
    auto clean = [&](QString part) {
        part.replace(unsafe, "-");
        return part.trimmed();
    };
    QString name = song.title;
    if (name.isEmpty())
        name = QFileInfo(song.filePath).completeBaseName();
    if (!song.albumArtist.isEmpty())
        name = song.albumArtist + " - " + name;
    return QDir::cleanPath(karaokeDir) + "/" + clean(collection) + "/" + clean(name) + ".m4a";
}

QString Backend::buildM3u(const QVector<SongInfo> &songs,
                          const std::function<QString(const SongInfo &)> &pathFor) {
    QVector<SongInfo> sorted = songs;
    std::sort(sorted.begin(), sorted.end(), [](const SongInfo &a, const SongInfo &b) {
        return a.matchPath.toLower() < b.matchPath.toLower();
    });
    QStringList lines{"#EXTM3U"};
    for (const auto &song : sorted) {
        const QString label = song.albumArtist.isEmpty() ? song.title
                                                         : song.albumArtist + " - " + song.title;
        lines.append(QString("#EXTINF:%1,%2").arg(song.durationSeconds).arg(label));
        lines.append(pathFor(song));
    }
    return lines.join('\n') + '\n';
}

void Backend::startKaraoke(const QVector<SongInfo> &readySongs) {
    m_karaokeRunning = true;
    m_karaokeFailures = 0;
    m_downloadQueue.clear();

    // A ready song re-submitted at "auto" quality answers done immediately
    // with a job id — the key to its stems — without queueing any new work.
    QVector<SongInfo> valid;
    for (const auto &song : readySongs)
        if (song.durationSeconds > 0)
            valid.append(song);

    m_karaokeChunksLeft = (valid.size() + kBatchSize - 1) / kBatchSize;
    if (valid.isEmpty()) {
        checkKaraokeDone();
        return;
    }
    for (int offset = 0; offset < valid.size(); offset += kBatchSize) {
        QJsonArray chunk;
        QHash<QString, SongInfo> byId;
        for (int i = offset; i < qMin(offset + kBatchSize, valid.size()); ++i) {
            chunk.append(songToJson(valid[i]));
            byId.insert(valid[i].filePath, valid[i]);
        }
        const QJsonObject body{{"songs", chunk}, {"priority", "batch"}, {"quality", "auto"}};
        post("/v1/jobs/batch", QJsonDocument(body),
             [this, byId](const QJsonValue &response) {
                 for (const auto &value : response.toArray()) {
                     const QJsonObject result = value.toObject();
                     const QString filePath = result.value("clientSongId").toString();
                     const QString jobId = result.value("jobId").toString();
                     if (result.value("state").toString() == "done" && !jobId.isEmpty() && byId.contains(filePath))
                         m_downloadQueue.append({byId.value(filePath), jobId});
                 }
                 if (--m_karaokeChunksLeft == 0)
                     pumpDownloads();
                 emit downloadsChanged();
             },
             [this] {
                 // Without the job ids no download can start; try again on the
                 // next trigger rather than writing a stale playlist.
                 m_karaokeChunksLeft = 0;
                 checkKaraokeDone();
             });
    }
    emit downloadsChanged();
}

void Backend::pumpDownloads() {
    while (m_activeReplies.size() < 2 && !m_downloadQueue.isEmpty()) {
        const DownloadJob job = m_downloadQueue.takeFirst();
        QNetworkRequest req = request("/v1/jobs/" + job.jobId + "/instrumental.m4a");
        req.setTransferTimeout(120000); // stems run to tens of MB
        const auto entry = m_karaoke.constFind(job.song.filePath);
        if (entry != m_karaoke.constEnd() && !entry->etag.isEmpty() &&
            QFileInfo::exists(entry->instrumentalPath))
            req.setRawHeader("If-None-Match", entry->etag.toUtf8());
        QNetworkReply *reply = m_nam.get(req);
        m_activeReplies.insert(reply, job.song);
        connect(reply, &QNetworkReply::finished, this, [this, reply] { downloadFinished(reply); });
    }
    checkKaraokeDone();
    emit downloadsChanged();
}

void Backend::downloadFinished(QNetworkReply *reply) {
    const SongInfo song = m_activeReplies.take(reply);
    reply->deleteLater();
    if (replyFailedFatally(reply)) {
        ++m_karaokeFailures;
    } else {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 304) {
            // Unchanged on the helper — but a renamed collection moves the
            // file, and a lyric edited since the copy gets refreshed.
            auto entry = m_karaoke.find(song.filePath);
            if (entry != m_karaoke.end()) {
                relocateKaraokeFile(song, entry.value());
                copyLrc(song, entry.value());
                saveKaraoke();
            }
        } else if (status == 200) {
            startRemux(song, reply->readAll(), QString::fromUtf8(reply->rawHeader("ETag")));
        } else {
            ++m_karaokeFailures;
        }
    }
    pumpDownloads();
}

QString Backend::karaokeCollection() const {
    return QFileInfo(m_settings.karaokePath()).completeBaseName();
}

// The flat artist-title name can collide between two sources (a single and
// the album version, say); a claimed path earns a numeric suffix.
QString Backend::claimKaraokePath(const SongInfo &song) {
    QString base = karaokePathFor(song, m_settings.karaokeDirectory(), karaokeCollection());
    if (!base.endsWith(".m4a"))
        return base;
    QString candidate = base;
    for (int n = 2;; ++n) {
        bool claimed = false;
        for (auto it = m_karaoke.constBegin(); it != m_karaoke.constEnd(); ++it)
            if (it->instrumentalPath == candidate && it.key() != song.filePath) {
                claimed = true;
                break;
            }
        if (!claimed)
            return candidate;
        candidate = base.left(base.size() - 4) + QString(" (%1).m4a").arg(n);
    }
}

void Backend::startRemux(const SongInfo &song, const QByteArray &bytes, const QString &etag) {
    RemuxJob job;
    job.song = song;
    job.etag = etag;
    job.final = claimKaraokePath(song);
    // ffmpeg picks the container format from the extension, so the temporary
    // names keep .m4a.
    job.tmpIn = job.final + ".dl.m4a";
    job.tmpOut = job.final + ".tmp.m4a";
    QDir().mkpath(QFileInfo(job.final).absolutePath());

    QFile in(job.tmpIn);
    if (!in.open(QIODevice::WriteOnly) || in.write(bytes) != bytes.size()) {
        QFile::remove(job.tmpIn);
        ++m_karaokeFailures;
        return;
    }
    in.close();

    auto *process = new QProcess(this);
    m_remuxes.insert(process, job);
    connect(process, &QProcess::finished, this, [this, process] { remuxFinished(process); });
    process->start("ffmpeg", remuxArgs(song, job.tmpIn, job.tmpOut));
}

// The helper's stems carry no tags, so the original's are written back —
// except the album, which becomes the karaoke playlist's name so players
// group these as their own album instead of duplicating the originals.
QStringList Backend::remuxArgs(const SongInfo &song, const QString &input, const QString &output) const {
    QStringList args{"-v", "error", "-y", "-i", input, "-c:a", "copy"};
    auto tag = [&args](const char *key, const QString &value) {
        if (!value.isEmpty())
            args << "-metadata" << QString("%1=%2").arg(key, value);
    };
    tag("title", song.title);
    tag("artist", song.albumArtist);
    tag("album_artist", song.albumArtist);
    tag("album", karaokeCollection());
    args << output;
    return args;
}

// A renamed collection (or the flat-layout change) retags and moves the
// existing file — no new download needed.
void Backend::startRetag(const SongInfo &song, KaraokeEntry &entry) {
    RemuxJob job;
    job.song = song;
    job.etag = entry.etag;
    job.inputIsOldFile = true;
    job.tmpIn = entry.instrumentalPath;
    job.final = claimKaraokePath(song);
    job.tmpOut = job.final + ".tmp.m4a";
    QDir().mkpath(QFileInfo(job.final).absolutePath());
    auto *process = new QProcess(this);
    m_remuxes.insert(process, job);
    connect(process, &QProcess::finished, this, [this, process] { remuxFinished(process); });
    process->start("ffmpeg", remuxArgs(song, job.tmpIn, job.tmpOut));
    emit downloadsChanged();
}

void Backend::remuxFinished(QProcess *process) {
    const RemuxJob job = m_remuxes.take(process);
    process->deleteLater();
    const bool ok = process->exitCode() == 0 && QFileInfo(job.tmpOut).size() > 0;
    bool installed = false;
    if (ok) {
        QFile::remove(job.final);
        installed = QFile::rename(job.tmpOut, job.final);
    }
    if (installed) {
        // The download temp (or the pre-retag file, which lives in the old
        // folder) must go before pruning, or the old folder isn't empty.
        QFile::remove(job.tmpIn);
        const auto previous = m_karaoke.constFind(job.song.filePath);
        KaraokeEntry entry;
        entry.instrumentalPath = job.final;
        entry.etag = job.etag;
        entry.collection = karaokeCollection();
        if (previous != m_karaoke.constEnd()) {
            entry.lrcSourceMtime = previous->lrcSourceMtime;
            if (previous->instrumentalPath != job.final) {
                // A layout or collection change leaves the old file behind;
                // remove what we created and carry the lyric over.
                const QString oldDirectory = QFileInfo(previous->instrumentalPath).absolutePath();
                if (!job.inputIsOldFile) // for a retag the old file was the input, removed above
                    QFile::remove(previous->instrumentalPath);
                if (!previous->lrcPath.isEmpty() && QFileInfo::exists(previous->lrcPath)) {
                    const QString newLrc = QFileInfo(job.final).dir().absoluteFilePath(
                        QFileInfo(job.final).completeBaseName() + ".lrc");
                    QFile::remove(newLrc);
                    if (QFile::rename(previous->lrcPath, newLrc))
                        entry.lrcPath = newLrc;
                }
                pruneKaraokeDirs(oldDirectory);
            } else {
                entry.lrcPath = previous->lrcPath;
            }
        }
        copyLrc(job.song, entry);
        m_karaoke.insert(job.song.filePath, entry);
        saveKaraoke();
    } else {
        ++m_karaokeFailures;
        QFile::remove(job.tmpOut);
        if (!job.inputIsOldFile)
            QFile::remove(job.tmpIn); // a failed retag keeps the old file
    }
    pumpDownloads();
}

// The stem didn't change (304), but its home or album tag may have (renamed
// collection, new layout): retag and move the existing file, or move it
// plainly when only the folder changed.
void Backend::relocateKaraokeFile(const SongInfo &song, KaraokeEntry &entry) {
    const QString want = karaokePathFor(song, m_settings.karaokeDirectory(), karaokeCollection());
    if (entry.instrumentalPath == want || !QFileInfo::exists(entry.instrumentalPath))
        return;
    if (entry.collection != karaokeCollection()) {
        startRetag(song, entry);
        return;
    }
    const QString oldDirectory = QFileInfo(entry.instrumentalPath).absolutePath();
    QDir().mkpath(QFileInfo(want).absolutePath());
    QFile::remove(want);
    if (!QFile::rename(entry.instrumentalPath, want))
        return;
    if (!entry.lrcPath.isEmpty() && QFileInfo::exists(entry.lrcPath)) {
        const QString newLrc = QFileInfo(want).dir().absoluteFilePath(QFileInfo(want).completeBaseName() + ".lrc");
        QFile::remove(newLrc);
        if (QFile::rename(entry.lrcPath, newLrc))
            entry.lrcPath = newLrc;
    }
    entry.instrumentalPath = want;
    entry.lrcSourceMtime = 0; // force a lyric re-check against the new file
    pruneKaraokeDirs(oldDirectory);
}

// Removes folders emptied by a relocation or replacement, up to (never
// including) the karaoke root, and only inside it.
void Backend::pruneKaraokeDirs(QString startDirectory) {
    const QString root = QDir::cleanPath(m_settings.karaokeDirectory());
    QDir dir(startDirectory);
    while (QDir::cleanPath(dir.absolutePath()).startsWith(root + "/") && QDir().rmdir(dir.absolutePath()))
        dir.cdUp();
}

void Backend::copyLrc(const SongInfo &song, KaraokeEntry &entry) {
    const QFileInfo source(song.filePath);
    const QString lrcSource = source.dir().absoluteFilePath(source.completeBaseName() + ".lrc");
    if (!QFileInfo::exists(lrcSource) || entry.instrumentalPath.isEmpty())
        return;
    const QFileInfo instrumental(entry.instrumentalPath);
    const QString lrcDest =
        instrumental.dir().absoluteFilePath(instrumental.completeBaseName() + ".lrc");
    const qlonglong sourceMtime = QFileInfo(lrcSource).lastModified().toMSecsSinceEpoch();
    if (sourceMtime == entry.lrcSourceMtime && QFileInfo::exists(lrcDest))
        return; // this exact lyric file is already beside the instrumental
    QFile::remove(lrcDest);
    if (QFile::copy(lrcSource, lrcDest)) {
        entry.lrcPath = lrcDest;
        entry.lrcSourceMtime = sourceMtime;
    }
}

void Backend::checkKaraokeDone() {
    if (m_karaokeRunning &&
        (m_karaokeChunksLeft > 0 || !m_downloadQueue.isEmpty() || !m_activeReplies.isEmpty() ||
         !m_remuxes.isEmpty())) {
        emit downloadsChanged();
        return;
    }
    const bool wasRunning = m_karaokeRunning;
    m_karaokeRunning = false;
    if (wasRunning)
        writeKaraokePlaylist();
    emit downloadsChanged();
    if (m_playlistUpdatePending) {
        m_playlistUpdatePending = false;
        updatePlaylist();
    }
}

void Backend::writeKaraokePlaylist() {
    QVector<SongInfo> entries;
    for (const auto &song : m_store.songs()) {
        if (!m_readyPaths.contains(song.filePath))
            continue;
        const auto entry = m_karaoke.constFind(song.filePath);
        if (entry != m_karaoke.constEnd() && QFileInfo::exists(entry->instrumentalPath))
            entries.append(song);
    }
    if (entries.isEmpty()) {
        setStatus(tr("No ready songs yet"));
        return;
    }

    const QString path = m_settings.karaokePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(tr("Can't write the playlist to %1").arg(path));
        return;
    }
    const QString playlistDir = QFileInfo(path).absolutePath();
    file.write(buildM3u(entries, [this, playlistDir](const SongInfo &song) {
                   return relativeIfInside(m_karaoke.value(song.filePath).instrumentalPath, playlistDir);
               }).toUtf8());
    if (!file.commit()) {
        setStatus(tr("Can't write the playlist to %1").arg(path));
        return;
    }
    QString message = tr("Karaoke playlist updated — %n songs", nullptr, entries.size());
    if (m_karaokeFailures > 0)
        message += " · " + tr("%n couldn't be downloaded", nullptr, m_karaokeFailures);
    setStatus(message);
}

void Backend::loadKaraoke() {
    QFile file(m_karaokePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QJsonObject value = it.value().toObject();
        KaraokeEntry entry;
        entry.instrumentalPath = value.value("instrumentalPath").toString();
        entry.etag = value.value("etag").toString();
        entry.collection = value.value("collection").toString();
        entry.lrcPath = value.value("lrcPath").toString();
        entry.lrcSourceMtime = qlonglong(value.value("lrcSourceMtime").toDouble());
        if (!entry.instrumentalPath.isEmpty())
            m_karaoke.insert(it.key(), entry);
    }
}

void Backend::saveKaraoke() const {
    QJsonObject object;
    for (auto it = m_karaoke.constBegin(); it != m_karaoke.constEnd(); ++it) {
        object.insert(it.key(), QJsonObject{{"instrumentalPath", it->instrumentalPath},
                                            {"etag", it->etag},
                                            {"collection", it->collection},
                                            {"lrcPath", it->lrcPath},
                                            {"lrcSourceMtime", double(it->lrcSourceMtime)}});
    }
    QDir().mkpath(QFileInfo(m_karaokePath).absolutePath());
    QSaveFile file(m_karaokePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

QVector<SongInfo> Backend::writePlaylist(const QVector<SongInfo> &ready) {
    if (ready.isEmpty()) {
        setStatus(tr("No ready songs yet"));
        return ready;
    }

    const QString path = m_settings.playlistPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(tr("Can't write the playlist to %1").arg(path));
        return ready;
    }
    const QString directory = QFileInfo(path).absolutePath();
    const QString root = m_settings.musicRoot();
    file.write(buildM3u(ready, [directory, root](const SongInfo &song) {
                   return playlistPathFor(song.filePath, directory, root);
               }).toUtf8());
    if (!file.commit()) {
        setStatus(tr("Can't write the playlist to %1").arg(path));
        return ready;
    }
    setStatus(tr("Playlist updated — %n songs", nullptr, ready.size()));
    return ready;
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
