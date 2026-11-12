#pragma once

#include <QObject>

// The user's systemd service for Warmup's headless mode. The unit is installed
// by the package to /usr/lib/systemd/user/warmup.service, so enabling it needs
// no root privileges. Every action runs through "systemctl --user".
class Service : public QObject {
    Q_OBJECT
public:
    enum class State {
        Unknown,      // systemctl failed or isn't available
        NotInstalled, // the unit file is missing (dev build / old package)
        Inactive,     // installed, but not running
        Active,       // running (or about to be, while systemd restarts it)
        Failed,       // systemd gave up starting it
    };
    Q_ENUM(State)

    explicit Service(QObject *parent = nullptr);

    State state() const { return m_state; }
    bool enabled() const { return m_enabled; }
    bool installed() const { return m_installed; }
    bool available() const { return m_available; }
    QString detail() const { return m_detail; }

    void refresh();
    bool enable();  // enable only: it starts on the next login / window close
    bool disable(); // disable and stop now
    bool start();
    bool stop();
    bool restart();

    static QString unitName();
    static State classify(const QString &activeState, const QString &unitFileState, bool queryOk);
    static bool unitFileIsEnabled(const QString &unitFileState);

signals:
    void changed();

private:
    bool run(const QStringList &args, QString *output = nullptr);
    void setState(State state, bool enabled, bool installed, const QString &detail);

    State m_state = State::Unknown;
    bool m_enabled = false;
    bool m_installed = false;
    bool m_available = true;
    QString m_detail;
};
