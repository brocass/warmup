#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "metadata.h"
#include "settings.h"
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
};

QTEST_MAIN(WarmupTests)
#include "warmup_tests.moc"
