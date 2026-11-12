// warmup — prepares Narjo Sing stems ahead of time via the helper's API.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "theme.h"
#include "translator.h"

namespace {
bool wantsBackgroundMode(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--service" || arg == "--background")
            return true;
    }
    return false;
}
} // namespace

int main(int argc, char *argv[]) {
    if (wantsBackgroundMode(argc, argv)) {
        // systemd runs this: the whole update pipeline without QML or a
        // display, so it works from a TTY and at login.
        QCoreApplication app(argc, argv);
        app.setApplicationName("warmup");
        app.setApplicationVersion("0.6.0");
        Backend backend(Backend::Mode::Service);
        return app.exec();
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("warmup");
    app.setApplicationVersion("0.6.0");

    // Matches the window to warmup.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("warmup");
    app.setWindowIcon(QIcon::fromTheme("warmup"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // When the window goes away the service picks up the background work, so
    // closing Warmup doesn't stop the playlist and instrumental updates.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::prepareForQuit);

    return app.exec();
}
