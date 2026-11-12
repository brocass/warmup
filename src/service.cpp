#include "service.h"

#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

QString Service::unitName() {
    return QStringLiteral("warmup.service");
}

Service::Service(QObject *parent) : QObject(parent) {}

void Service::setState(State state, bool enabled, bool installed, const QString &detail) {
    if (state == m_state && enabled == m_enabled && installed == m_installed && detail == m_detail)
        return;
    m_state = state;
    m_enabled = enabled;
    m_installed = installed;
    m_detail = detail;
    emit changed();
}

bool Service::run(const QStringList &args, QString *output) {
    QProcess process;
    process.start("systemctl", args);
    if (!process.waitForStarted(5000)) {
        m_detail = tr("systemctl couldn't be started");
        return false;
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        m_detail = tr("systemctl didn't answer in time");
        return false;
    }
    const QString out = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (output)
        *output = out;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        m_detail = err.isEmpty() ? out : err;
        return false;
    }
    m_detail.clear();
    return true;
}

Service::State Service::classify(const QString &activeState, const QString &unitFileState, bool queryOk) {
    if (!queryOk)
        return State::Unknown;
    if (unitFileState.isEmpty())
        return State::NotInstalled;
    if (activeState == "failed")
        return State::Failed;
    // "activating" covers the brief window while systemd auto-restarts a
    // crashed service; treating it as running avoids the window fighting it.
    if (activeState == "active" || activeState == "activating" || activeState == "deactivating" ||
        activeState == "reloading")
        return State::Active;
    return State::Inactive;
}

bool Service::unitFileIsEnabled(const QString &unitFileState) {
    return unitFileState.startsWith("enabled") || unitFileState == "static";
}

void Service::refresh() {
    m_available = !QStandardPaths::findExecutable("systemctl").isEmpty();
    if (!m_available) {
        setState(State::Unknown, false, false, tr("systemctl isn't available on this system"));
        return;
    }

    QString output;
    if (!run({"--user", "show", "-p", "ActiveState", "-p", "UnitFileState", unitName()}, &output)) {
        setState(State::Unknown, false, false, m_detail);
        return;
    }

    QString activeState;
    QString unitFileState;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int equals = line.indexOf('=');
        if (equals <= 0)
            continue;
        const QString key = line.left(equals);
        const QString value = line.mid(equals + 1).trimmed();
        if (key == "ActiveState")
            activeState = value;
        else if (key == "UnitFileState")
            unitFileState = value;
    }

    const bool installed = !unitFileState.isEmpty();
    setState(classify(activeState, unitFileState, true), unitFileIsEnabled(unitFileState), installed, {});
}

bool Service::enable() {
    // After a package upgrade systemd's user manager may still have the unit
    // cached as missing; a reload makes the freshly installed file visible.
    run({"--user", "daemon-reload"});
    if (!run({"--user", "enable", unitName()})) {
        refresh();
        return false;
    }
    refresh();
    return m_enabled;
}

bool Service::disable() {
    const bool ok = run({"--user", "disable", "--now", unitName()});
    refresh();
    return ok;
}

bool Service::start() {
    const bool ok = run({"--user", "start", unitName()});
    refresh();
    return ok && m_state == State::Active;
}

bool Service::stop() {
    const bool ok = run({"--user", "stop", unitName()});
    refresh();
    return ok;
}

bool Service::restart() {
    const bool ok = run({"--user", "restart", unitName()});
    refresh();
    return ok;
}
