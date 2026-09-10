#include "Service.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QProcessEnvironment>

namespace Alure {
QVariant unbox(const QVariant &v) {
    return v.metaType() == QMetaType::fromType<QDBusVariant>() ? qvariant_cast<QDBusVariant>(v).variant() : v;
}
QVariantMap dbusMap(const QVariant &v) {
    if (v.metaType() == QMetaType::fromType<QDBusArgument>()) return qdbus_cast<QVariantMap>(v);
    return v.toMap();
}
Service::Service(QObject *parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &Service::refresh);
}
void Service::configure(const QVariantMap &module) {
    auto behavior = module.value("behavior").toMap();
    // Presentation-only behavior must not reset notification history or in-flight reads.
    for (const auto &key : {"format", "popup_enabled", "toast_enabled", "menu_width", "menu_height", "show_artwork", "artwork_remote", "artwork_height", "show_artist", "show_album", "show_progress", "show_shuffle", "show_repeat"}) behavior.remove(key);
    const QVariantMap executionConfig{{"enabled", module.value("enabled")}, {"behavior", behavior}};
    if (executionConfig == m_config) return;
    m_config = executionConfig; ++m_generation; m_timer.stop();
    for (auto *watcher : findChildren<QDBusPendingCallWatcher *>(QString(), Qt::FindDirectChildrenOnly)) delete watcher;
    stop();
    m_busy = false; m_enabled = module.value("enabled").toBool();
    m_options = module.value("behavior").toMap();
    m_available = false; m_state.clear(); m_items.clear(); m_diagnostic = m_enabled ? "Connecting" : "Disabled";
    emit changed();
    if (m_enabled) {
        m_timer.start(m_options.value("interval_ms").toInt());
        refresh();
    }
}
void Service::refresh() { if (m_enabled && !m_busy) poll(); }
bool Service::action(const QString &name, const QVariantMap &args) {
    if (!m_enabled || m_busy || !m_options.value("allow_actions", true).toBool()) return false;
    return act(name, args);
}
void Service::publish(QVariantMap state, QVariantList items) {
    m_available = true; m_diagnostic.clear(); m_state = std::move(state); m_items = std::move(items); emit changed();
}
void Service::fail(const QString &message) {
    m_available = false; m_diagnostic = message; m_state.clear(); m_items.clear(); emit changed();
}
void Service::setBusy(bool value) { m_busy = value; emit changed(); }
void Service::call(const QDBusConnection &bus, const QString &destination, const QString &path,
                   const QString &interface, const QString &method, const QVariantList &args,
                   std::function<void(const QVariantList &)> success, std::function<void(const QString &)> failure) {
    if (findChildren<QDBusPendingCallWatcher *>(QString(), Qt::FindDirectChildrenOnly).size() >= 128) {
        setBusy(false); fail("Too many pending DBus requests"); return;
    }
    auto message = QDBusMessage::createMethodCall(destination, path, interface, method);
    message.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, timeout()), this);
    const auto generation = m_generation;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, success = std::move(success), failure = std::move(failure)] {
        const QDBusMessage reply = watcher->reply(); watcher->deleteLater();
        if (generation != m_generation || !enabled()) return;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            const auto error = reply.errorName() + ": " + reply.errorMessage();
            if (failure) failure(error); else { setBusy(false); fail(error); }
            return;
        }
        success(reply.arguments());
    });
}
void Service::properties(const QDBusConnection &bus, const QString &destination, const QString &path,
                         const QString &interface, std::function<void(QVariantMap)> success, std::function<void(const QString &)> failure) {
    call(bus, destination, path, "org.freedesktop.DBus.Properties", "GetAll", {interface},
         [success = std::move(success)](const QVariantList &args) { success(args.isEmpty() ? QVariantMap{} : dbusMap(args.first())); }, std::move(failure));
}
bool Service::dbusAction(const QDBusConnection &bus, const QString &destination, const QString &path,
                         const QString &interface, const QString &method, const QVariantList &args) {
    setBusy(true);
    call(bus, destination, path, interface, method, args, [this](const QVariantList &) { setBusy(false); refresh(); });
    return true;
}
CommandRunner::CommandRunner(QObject *parent) : QObject(parent) {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    auto env = QProcessEnvironment::systemEnvironment(); env.insert("LC_ALL", "C"); m_process.setProcessEnvironment(env);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] { m_error = "Command timed out"; m_process.kill(); });
    connect(&m_process, &QProcess::readyRead, this, &CommandRunner::drain);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_active) { m_active = false; m_deadline.stop(); emit completed(-1, {}, m_process.errorString()); }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        drain(); m_deadline.stop();
        if (!m_active) return;
        m_active = false;
        if (status == QProcess::CrashExit && m_error.isEmpty()) m_error = "Command crashed";
        emit completed(code, m_output, m_error);
    });
}
CommandRunner::~CommandRunner() { cancel(); }
void CommandRunner::cancel() { m_active = false; m_deadline.stop(); if (running()) m_process.kill(); }
void CommandRunner::drain() {
    const auto bytes = m_process.readAll();
    if (m_output.size() + bytes.size() > 1024 * 1024) { m_error = "Command output exceeds 1 MiB"; m_process.kill(); }
    else m_output += bytes;
}
bool CommandRunner::run(QStringList argv, int timeoutMs) {
    if (running() || argv.isEmpty() || argv.first().isEmpty()) return false;
    m_output.clear(); m_error.clear(); m_active = true;
    const auto program = argv.takeFirst(); m_process.start(program, argv); m_deadline.start(timeoutMs); return true;
}
}
