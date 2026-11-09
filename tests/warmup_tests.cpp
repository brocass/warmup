#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "metadata.h"
#include "settings.h"
#include "songstore.h"
#include "theme.h"
#include "translator.h"

class WarmupTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        qunsetenv("WARMUP_HELPER_URL");
        qunsetenv("WARMUP_ACCESS_KEY");
        qunsetenv("WARMUP_MUSIC_ROOT");
        qunsetenv("WARMUP_QUALITY");
        qunsetenv("WARMUP_PLAYLIST_ENABLED");
        qunsetenv("WARMUP_PLAYLIST_PATH");
        qunsetenv("WARMUP_KARAOKE_ENABLED");
        qunsetenv("WARMUP_KARAOKE_PATH");
        qunsetenv("WARMUP_KARAOKE_DIRECTORY");
        qunsetenv("WARMUP_STORE_PATH");
        qunsetenv("WARMUP_KARAOKE_STORE_PATH");
    }

    void backendBuildsSongJson()
    {
        SongInfo song;
        song.filePath = "/music/Band/Album/track.mp3";
        song.matchPath = "Band/Album/track.mp3";
        song.title = "Neon Skyline";
        song.albumArtist = "The Test Band";
        song.album = "Midnight Parade";
        song.disc = 1;
        song.track = 3;
        song.durationSeconds = 213;

        const QJsonObject json = Backend::songToJson(song);
        QCOMPARE(json.value("clientSongId").toString(), song.filePath);
        QCOMPARE(json.value("title").toString(), QString("Neon Skyline"));
        QCOMPARE(json.value("durationSeconds").toInt(), 213);
        QCOMPARE(json.value("path").toString(), QString("Band/Album/track.mp3"));
        QCOMPARE(json.value("albumArtist").toString(), QString("The Test Band"));
        QCOMPARE(json.value("track").toInt(), 3);

        // Optional fields are omitted when empty, not sent as blanks.
        SongInfo bare;
        bare.filePath = "/music/x.mp3";
        bare.matchPath = "x.mp3";
        bare.title = "x";
        bare.durationSeconds = 10;
        const QJsonObject bareJson = Backend::songToJson(bare);
        QVERIFY(!bareJson.contains("albumArtist"));
        QVERIFY(!bareJson.contains("disc"));
    }

    void backendTalliesBatchResponses()
    {
        const QJsonArray results = QJsonDocument::fromJson(R"([
            {"state":"queued"}, {"state":"running"}, {"state":"done"},
            {"state":"notFound"}, {"state":"failed"}, {"state":"cancelled"}
        ])").array();
        const Backend::Tally tally = Backend::tallyBatchResponse(results);
        QCOMPARE(tally.added, 2);
        QCOMPARE(tally.ready, 1);
        QCOMPARE(tally.missing, 1);
        QCOMPARE(tally.failed, 1);
        QCOMPARE(tally.other, 1);
    }

    void metadataParsesM3u()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("sub");
        touch(dir.filePath("a.mp3"));
        touch(dir.filePath("sub/c.flac"));
        write(dir.filePath("list.m3u"),
              "#EXTM3U\n#EXTINF:213, Band - Song\na.mp3\nsub/c.flac\nhttp://stream.example/x.mp3\nmissing.mp3\n");

        const QStringList entries = Metadata::parsePlaylist(dir.filePath("list.m3u"));
        QCOMPARE(entries, QStringList({dir.filePath("a.mp3"), dir.filePath("sub/c.flac")}));
    }

    void metadataParsesPls()
    {
        QTemporaryDir dir;
        touch(dir.filePath("a.mp3"));
        touch(dir.filePath("b.mp3"));
        write(dir.filePath("list.pls"), QString("[playlist]\nFile1=%1\nFile2=b.mp3\nNumberOfEntries=2\n")
                                                .arg(dir.filePath("a.mp3"))
                                                .toUtf8());

        const QStringList entries = Metadata::parsePlaylist(dir.filePath("list.pls"));
        QCOMPARE(entries, QStringList({dir.filePath("a.mp3"), dir.filePath("b.mp3")}));
    }

    void metadataCollectsAudioFiles()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("sub");
        touch(dir.filePath("a.mp3"));
        touch(dir.filePath("notes.txt"));
        touch(dir.filePath("sub/c.FLAC"));
        write(dir.filePath("list.m3u"), "a.mp3\nsub/c.flac\n");

        const QStringList files = Metadata::collectAudioFiles({QUrl::fromLocalFile(dir.path())});
        // Sorted, de-duplicated (the playlist repeats both songs), no .txt.
        QCOMPARE(files, QStringList({dir.filePath("a.mp3"), dir.filePath("sub/c.FLAC")}));
        QVERIFY(Metadata::isAudioFile("x.Opus"));
        QVERIFY(!Metadata::isAudioFile("x.txt"));
        QVERIFY(Metadata::isPlaylist("x.m3u8"));
    }

    void metadataParsesFfprobeJson()
    {
        const QByteArray json = R"({"format":{"duration":"213.4","tags":{
            "TITLE":"Neon Skyline","ARTIST":"The Test Band","ALBUM":"Midnight Parade",
            "TRACK":"3/12","DISC":"1"}}})";

        const SongInfo song = Metadata::parseProbe("/music/Band/Album/song.mp3", json, "/music");
        QVERIFY(song.ok);
        QCOMPARE(song.title, QString("Neon Skyline"));
        QCOMPARE(song.albumArtist, QString("The Test Band"));
        QCOMPARE(song.album, QString("Midnight Parade"));
        QCOMPARE(song.track, 3);
        QCOMPARE(song.disc, 1);
        QCOMPARE(song.durationSeconds, 213);
        QCOMPARE(song.matchPath, QString("Band/Album/song.mp3"));

        // Outside the music root the file name alone is the match path.
        const SongInfo outside = Metadata::parseProbe("/elsewhere/song.mp3", json, "/music");
        QCOMPARE(outside.matchPath, QString("song.mp3"));

        // Without tags the file name stands in for the title.
        const SongInfo untagged = Metadata::parseProbe(
            "/music/song.mp3", R"({"format":{"duration":"60.2"}})", "/music");
        QVERIFY(untagged.ok);
        QCOMPARE(untagged.title, QString("song"));

        // Unreadable files (no format, no duration) are rejected.
        QVERIFY(!Metadata::parseProbe("/music/bad.mp3", "not json", "/music").ok);
        QVERIFY(!Metadata::parseProbe("/music/bad.mp3", R"({"format":{}})", "/music").ok);
    }

    void metadataCountsMp3FramesForDuration()
    {
        // An mp3 whose container estimate (423 s) is wrong: the audio frame
        // count gives the true, decodable length (257 s) that the helper
        // indexes. The cover-art stream must not be mistaken for audio.
        const QByteArray json = R"({"streams":[
            {"codec_type":"video","nb_read_frames":"1"},
            {"codec_type":"audio","nb_read_frames":"9855","sample_rate":"44100"}],
            "format":{"duration":"423.252433","tags":{"TITLE":"Skin & Bones"}}})";
        const SongInfo song = Metadata::parseProbe("/music/song.mp3", json, "/music");
        QVERIFY(song.ok);
        QCOMPARE(song.durationSeconds, 257);
        QCOMPARE(song.probeVersion, Metadata::kProbeVersion);
        QVERIFY(song.hasEmbeddedArt);

        QCOMPARE(Metadata::mp3SamplesPerFrame(44100), 1152);
        QCOMPARE(Metadata::mp3SamplesPerFrame(48000), 1152);
        QCOMPARE(Metadata::mp3SamplesPerFrame(22050), 576);
        QCOMPARE(Metadata::mp3SamplesPerFrame(12000), 576);

        // Without a streams array (other formats) the container duration stands.
        const SongInfo flac = Metadata::parseProbe("/music/song.flac", R"({"format":{"duration":"60.2"}})", "/music");
        QCOMPARE(flac.durationSeconds, 60);
        QCOMPARE(flac.probeVersion, Metadata::kProbeVersion);
        QVERIFY(!flac.hasEmbeddedArt);
    }

    void metadataReadsTrackArtist()
    {
        const QByteArray json = R"({"format":{"duration":"200.1","tags":{
            "TITLE":"Pick A Part","ARTIST":"Stereophonics","ALBUM_ARTIST":"Various Artists"}}})";
        const SongInfo song = Metadata::parseProbe("/music/comp/song.mp3", json, "/music");
        QCOMPARE(song.artist, QString("Stereophonics"));
        QCOMPARE(song.albumArtist, QString("Various Artists")); // the helper matches on this one

        // Single-artist albums usually have no album_artist tag; it falls back.
        const SongInfo solo = Metadata::parseProbe(
            "/music/solo/song.mp3", R"({"format":{"duration":"200.1","tags":{"ARTIST":"The Sundays"}}})", "/music");
        QCOMPARE(solo.albumArtist, QString("The Sundays"));
    }

    void karaokePrefersTrackArtist()
    {
        SongInfo song = probeSong("/music/comp/song.mp3", "Pick A Part");
        song.artist = "Stereophonics";
        song.albumArtist = "Various Artists";
        QCOMPARE(Backend::karaokeArtist(song), QString("Stereophonics"));
        QCOMPARE(Backend::karaokePathFor(song, "/karaoke", "Sing karaoke"),
                 QString("/karaoke/Sing karaoke/Stereophonics - Pick A Part.m4a"));
        song.artist.clear();
        QCOMPARE(Backend::karaokeArtist(song), QString("Various Artists"));
    }

    void folderCoverIsFoundNextToTheSong()
    {
        QTemporaryDir dir;
        touch(dir.filePath("song.mp3"));
        QCOMPARE(Backend::folderCoverFor(dir.filePath("song.mp3")), QString());
        touch(dir.filePath("notes.jpg"));
        touch(dir.filePath("Folder.JPG"));
        QCOMPARE(Backend::folderCoverFor(dir.filePath("song.mp3")), dir.filePath("Folder.JPG"));
    }

    void metadataJsonKeepsProbeVersion()
    {
        SongInfo song;
        song.filePath = "/music/x.mp3";
        song.title = "x";
        song.artist = "a";
        song.durationSeconds = 100;
        song.hasEmbeddedArt = true;
        song.probeVersion = Metadata::kProbeVersion;
        const SongInfo loaded = Metadata::songInfoFromJson(Metadata::songInfoToJson(song));
        QCOMPARE(loaded.probeVersion, Metadata::kProbeVersion);
        QCOMPARE(loaded.artist, QString("a"));
        QVERIFY(loaded.hasEmbeddedArt);
        // Entries written before the fields existed load with defaults.
        const SongInfo legacy = Metadata::songInfoFromJson(QJsonObject{{"filePath", "/music/x.mp3"}});
        QCOMPARE(legacy.probeVersion, 0);
        QVERIFY(!legacy.hasEmbeddedArt);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings first(path);
        QVERIFY(!first.isConfigured());
        QCOMPARE(first.quality(), QString("best"));

        first.apply("192.168.1.20:8765/", " key ", "/music/root", "both");
        QCOMPARE(first.helperUrl(), QString("http://192.168.1.20:8765"));
        QCOMPARE(first.accessKey(), QString("key"));
        QVERIFY(first.isConfigured());

        Settings second(path);
        QCOMPARE(second.helperUrl(), QString("http://192.168.1.20:8765"));
        QCOMPARE(second.accessKey(), QString("key"));
        QCOMPARE(second.musicRoot(), QString("/music/root"));
        QCOMPARE(second.quality(), QString("both"));

        // Unknown quality words fall back to best.
        second.apply("http://h:1", "k", "/m", "everything");
        QCOMPARE(second.quality(), QString("best"));
    }

    void settingsEnvWins()
    {
        qputenv("WARMUP_HELPER_URL", "10.0.0.1:8765");
        qputenv("WARMUP_QUALITY", "fast");
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        QCOMPARE(settings.helperUrl(), QString("http://10.0.0.1:8765"));
        QCOMPARE(settings.quality(), QString("fast"));
        qunsetenv("WARMUP_HELPER_URL");
        qunsetenv("WARMUP_QUALITY");
    }

    void settingsPlaylistRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings first(path);
        QCOMPARE(first.playlistEnabled(), false);
        QCOMPARE(first.playlistPath(), first.musicRoot() + "/Sing ready.m3u"); // default

        first.applyPlaylist(true, "/music/Playlists/Karaoke Night");
        QCOMPARE(first.playlistPath(), QString("/music/Playlists/Karaoke Night.m3u"));

        Settings second(path);
        QCOMPARE(second.playlistEnabled(), true);
        QCOMPARE(second.playlistPath(), QString("/music/Playlists/Karaoke Night.m3u"));

        // An empty path falls back to the default.
        second.applyPlaylist(true, "");
        QCOMPARE(second.playlistPath(), second.musicRoot() + "/Sing ready.m3u");
    }

    void settingsMigratesFolderAndNameToPath()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        write(path, R"({"musicRoot": "/music", "playlistEnabled": true,
                        "playlistName": "Narjo Karaoke", "playlistDirectory": "/music/Playlists",
                        "karaokeEnabled": true, "karaokeName": "Sing karaoke"})");

        Settings settings(path);
        QCOMPARE(settings.playlistPath(), QString("/music/Playlists/Narjo Karaoke.m3u"));
        QCOMPARE(settings.karaokePath(), QString("/music/Playlists/Sing karaoke.m3u"));
    }

    void storeKeepsSubmittedSongs()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("songs.json");
        SongStore store(path);
        store.add(probeSong("/music/Band/Album/one.mp3", "One"));
        store.add(probeSong("/music/Band/Album/two.mp3", "Two"));
        QCOMPARE(store.songs().size(), 2);

        SongStore reloaded(path);
        QCOMPARE(reloaded.songs().size(), 2);
        QVERIFY(reloaded.contains("/music/Band/Album/one.mp3"));

        // Re-adding the same file updates it in place and keeps better data.
        SongInfo updated = probeSong("/music/Band/Album/one.mp3", "");
        updated.durationSeconds = 0;
        store.add(updated);
        QCOMPARE(store.songs().size(), 2);
        QCOMPARE(store.songs().first().title, QString("One"));
        QCOMPARE(store.songs().first().durationSeconds, 213);
    }

    void lookupBodiesSplitIntoChunks()
    {
        QVector<SongInfo> songs;
        for (int i = 0; i < 450; ++i)
            songs.append(probeSong(QString("/music/Band/song%1.mp3").arg(i), QString("Song %1").arg(i)));
        const QVector<QJsonObject> bodies = Backend::buildLookupBodies(songs);
        QCOMPARE(bodies.size(), 3); // 200 + 200 + 50
        QCOMPARE(bodies.first().value("quality").toString(), QString("auto"));
        QCOMPARE(bodies.first().value("songs").toArray().size(), 200);
        QCOMPARE(bodies.last().value("songs").toArray().size(), 50);
        QCOMPARE(bodies.last().value("songs").toArray().first().toObject().value("title").toString(),
                 QString("Song 400"));
    }

    void storeGetsSingleSongs()
    {
        QTemporaryDir dir;
        SongStore store(dir.filePath("songs.json"));
        store.add(probeSong("/music/Band/one.mp3", "One"));
        QCOMPARE(store.get("/music/Band/one.mp3").title, QString("One"));
        QVERIFY(store.get("/music/Band/nope.mp3").filePath.isEmpty());
    }

    void playlistPathNormalized()
    {
        QCOMPARE(Settings::normalizePlaylistPath("Karaoke Night", "/music"), QString("/music/Karaoke Night.m3u"));
        QCOMPARE(Settings::normalizePlaylistPath("/data/lists/already.m3u", "/music"),
                 QString("/data/lists/already.m3u"));
        QCOMPARE(Settings::normalizePlaylistPath("sub/dir/list", "/music"), QString("/music/sub/dir/list.m3u"));
        QCOMPARE(Settings::normalizePlaylistPath("", "/music"), QString());
        QCOMPARE(Settings::normalizePlaylistPath("  /data/x.m3u  ", "/music"), QString("/data/x.m3u"));
    }

    void playlistPathsStayRelativeInsideTheMusicFolder()
    {
        QCOMPARE(Backend::playlistPathFor("/music/Band/song.mp3", "/music", "/music"),
                 QString("Band/song.mp3"));
        QCOMPARE(Backend::playlistPathFor("/music/Band/song.mp3", "/music/Playlists", "/music"),
                 QString("../Band/song.mp3"));
        QCOMPARE(Backend::playlistPathFor("/music/Band/song.mp3", "/home/x/Playlists", "/music"),
                 QString("/music/Band/song.mp3"));
    }

    void playlistM3uContent()
    {
        QVector<SongInfo> songs{probeSong("/music/Band/Album/two.mp3", "Two"),
                                probeSong("/music/Band/Album/one.mp3", "One")};
        songs[0].albumArtist.clear(); // "Two" has no artist tag
        const QString m3u = Backend::buildM3u(songs, [](const SongInfo &song) {
            return Backend::playlistPathFor(song.filePath, "/music", "/music");
        });
        const QStringList lines = m3u.split('\n');
        QCOMPARE(lines.first(), QString("#EXTM3U"));
        // Sorted by match path, one EXTINF (duration, artist - title) per song.
        QCOMPARE(lines.value(1), QString("#EXTINF:213,The Test Band - One"));
        QCOMPARE(lines.value(2), QString("Band/Album/one.mp3"));
        QCOMPARE(lines.value(3), QString("#EXTINF:213,Two"));
        QCOMPARE(lines.value(4), QString("Band/Album/two.mp3"));
        QVERIFY(m3u.endsWith('\n'));
    }

    void karaokePathsFlattenIntoTheCollection()
    {
        // One folder per playlist, files named by artist and title.
        QCOMPARE(Backend::karaokePathFor(probeSong("/music/Band/Album/03 Song.mp3", "Song"), "/karaoke",
                                         "Sing karaoke"),
                 QString("/karaoke/Sing karaoke/The Test Band - Song.m4a"));
        // Without an artist tag the title stands alone.
        SongInfo untagged = probeSong("/music/Band/Album/04 Solo.mp3", "Solo");
        untagged.albumArtist.clear();
        QCOMPARE(Backend::karaokePathFor(untagged, "/karaoke", "Sing karaoke"),
                 QString("/karaoke/Sing karaoke/Solo.m4a"));
        // Characters that break other operating systems' shares are replaced.
        QCOMPARE(Backend::karaokePathFor(probeSong("/music/A:B?/Al\"bum/03 S*ong.mp3", "S*ong"), "/karaoke",
                                         "My/List"),
                 QString("/karaoke/My-List/The Test Band - S-ong.m4a"));
    }

    void karaokePlaylistPathsAreRelativeOnlyInsideTheFolder()
    {
        QCOMPARE(Backend::relativeIfInside("/music/Instrumentals/x.m4a", "/music"),
                 QString("Instrumentals/x.m4a"));
        QCOMPARE(Backend::relativeIfInside("/home/x/Karaoke/x.m4a", "/music"),
                 QString("/home/x/Karaoke/x.m4a"));
    }

    void settingsKaraokeRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings first(path);
        QCOMPARE(first.karaokeEnabled(), false);
        QCOMPARE(first.karaokePath(), first.musicRoot() + "/Sing karaoke.m3u");
        QCOMPARE(first.karaokeDirectory(), QDir::homePath() + "/Karaoke");

        first.applyKaraoke(true, "lists/Karaoke night", "/data/karaoke");
        Settings second(path);
        QCOMPARE(second.karaokeEnabled(), true);
        QCOMPARE(second.karaokePath(), second.musicRoot() + "/lists/Karaoke night.m3u");
        QCOMPARE(second.karaokeDirectory(), QString("/data/karaoke"));

        second.applyKaraoke(true, "", "");
        QCOMPARE(second.karaokePath(), second.musicRoot() + "/Sing karaoke.m3u");
        QCOMPARE(second.karaokeDirectory(), QDir::homePath() + "/Karaoke");
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "xx-YY\n");
        write(dir.path() + "/xx.json", R"({"Save": "[Save]", "Open": "[Open]"})");
        write(dir.path() + "/xx-YY.json", R"({"Save": "[Save YY]"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("xx-YY"));
        QCOMPARE(translator.translate("Main", "Save"), QString("[Save YY]"));
        QCOMPARE(translator.translate("Main", "Open"), QString("[Open]"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "xx\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/xx.json", R"({"%n clips": {"one": "[one clip]", "other": "[other clips]"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 1), QString("[one clip]"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 5), QString("[other clips]"));
        QCOMPARE(Translator::pluralCategory("ru", 21), QString("one"));
        QCOMPARE(Translator::pluralCategory("ru", 25), QString("many"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static bool write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    static void touch(const QString &path) { write(path, "x"); }

    static SongInfo probeSong(const QString &filePath, const QString &title)
    {
        SongInfo song;
        song.filePath = filePath;
        song.matchPath = Metadata::matchPathFor(filePath, "/music");
        song.title = title;
        song.albumArtist = "The Test Band";
        song.album = "Midnight Parade";
        song.track = 3;
        song.disc = 1;
        song.durationSeconds = 213;
        song.ok = true;
        return song;
    }
};

QTEST_MAIN(WarmupTests)
#include "warmup_tests.moc"
