#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>
#include <QDBusConnection>
#include <functional>

namespace Alure {
// Snapshot properties contain only observed data. Unavailable/disabled clears it.
class Service : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
public:
    explicit Service(QObject *parent = nullptr);
    bool enabled() const { return m_enabled; }
    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QString diagnostic() const { return m_diagnostic; }
    QVariantMap state() const { return m_state; }
    QVariantList items() const { return m_items; }
    void configure(const QVariantMap &module);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool action(const QString &name, const QVariantMap &arguments = {});
signals:
    void changed();
protected:
    virtual void poll() = 0;
    virtual bool act(const QString &, const QVariantMap &) { return false; }
    virtual void stop() {}
    void publish(QVariantMap state = {}, QVariantList items = {});
    void fail(const QString &message);
    void setBusy(bool value);
    int timeout() const { return m_options.value("timeout_ms").toInt(); }
    QVariantMap m_options;
    quint64 m_generation = 0;
    // Async callbacks are tied to QObject lifetime and config generation.
    void call(const QDBusConnection &bus, const QString &destination, const QString &path,
              const QString &interface, const QString &method, const QVariantList &arguments,
              std::function<void(const QVariantList &)> success);
    void properties(const QDBusConnection &bus, const QString &destination, const QString &path,
                    const QString &interface, std::function<void(QVariantMap)> success);
    bool dbusAction(const QDBusConnection &bus, const QString &destination, const QString &path,
                    const QString &interface, const QString &method, const QVariantList &arguments = {});
private:
    QTimer m_timer;
    bool m_enabled = false, m_available = false, m_busy = false;
    QString m_diagnostic = "Disabled";
    QVariantMap m_state, m_config;
    QVariantList m_items;
};

// One direct child process at a time, C locale, output cap, deadline, no shell.
class CommandRunner : public QObject {
    Q_OBJECT
public:
    explicit CommandRunner(QObject *parent = nullptr);
    ~CommandRunner() override;
    bool run(QStringList argv, int timeoutMs);
    void cancel();
    bool running() const { return m_process.state() != QProcess::NotRunning; }
signals:
    void completed(int exitCode, QByteArray output, QString error);
private:
    void drain();
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    QString m_error;
    bool m_active = false;
};
QVariant unbox(const QVariant &value);
QVariantMap dbusMap(const QVariant &value);
}
