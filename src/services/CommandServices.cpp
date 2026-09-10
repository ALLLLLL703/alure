#include "CommandServices.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
namespace Alure {
namespace {
QStringList argv(const QVariant &value) { QStringList result; for (const auto &v : value.toList()) result << v.toString(); return result; }
QStringList fields(const QString &line) {
    QStringList result; QString field; bool escape = false;
    for (QChar c : line) {
        if (escape) { field += c; escape = false; }
        else if (c == '\\') escape = true;
        else if (c == ':') { result << field; field.clear(); }
        else field += c;
    }
    result << field; return result;
}
}
CommandService::CommandService(Kind kind, QObject *parent) : Service(parent), m_kind(kind) {
    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (!enabled()) return;
        if (busy()) { m_debounce.start(m_options.value("debounce_ms").toInt()); return; }
        auto command = argv(m_options.value("set_volume_command")); command << QString::number(m_pendingVolume / 100.0, 'f', 3);
        execute(command, true);
    });
    connect(&m_runner, &CommandRunner::completed, this, [this](int code, const QByteArray &out, const QString &processError) {
        setBusy(false);
        if (!enabled()) return;
        if (!processError.isEmpty()) { fail(processError); return; }
        if (m_action) {
            if (code != 0) fail(QString("Action exited %1: %2").arg(code).arg(QString::fromUtf8(out).left(512)));
            else refresh();
            return;
        }
        if (m_kind == Wifi && m_wifiPhase > 0) {
            if (code != 0) { fail(QString("nmcli exited %1: %2").arg(code).arg(QString::fromUtf8(out).left(512))); return; }
            if (m_wifiPhase == 1) {
                const auto radio = out.trimmed();
                if (radio != "enabled" && radio != "disabled") { fail("Invalid nmcli radio response"); return; }
                m_wifiState["powered"] = radio == "enabled";
                m_wifiPhase = 2; execute(argv(m_options.value("saved_command")), false);
            } else {
                QVariantList saved;
                for (const auto &line : QString::fromUtf8(out).trimmed().split('\n', Qt::SkipEmptyParts)) {
                    const auto columns = fields(line);
                    if (columns.size() != 3) { fail("Invalid nmcli saved connections response"); return; }
                    if (columns[2] == "802-11-wireless" || columns[2] == "wifi") saved << QVariantMap{{"uuid", columns[0]}, {"name", columns[1]}};
                }
                m_wifiState["savedConnections"] = saved;
                publish(m_wifiState, m_wifiItems);
            }
            return;
        }
        QVariantMap state; QVariantList items; QString error;
        if (!parse(m_kind, out, code, state, items, error)) { fail(error); return; }
        if (m_kind == Wifi) {
            m_wifiState = state; m_wifiItems = items; m_wifiPhase = 1;
            execute(argv(m_options.value("radio_status_command")), false);
        } else publish(state, items);
    });
}
void CommandService::stop() { m_debounce.stop(); m_runner.cancel(); }
bool CommandService::execute(const QStringList &command, bool isAction) {
    if (!m_runner.run(command, timeout())) { fail("Command empty or previous process stopping"); return false; }
    m_action = isAction; setBusy(true); return true;
}
void CommandService::poll() { m_wifiPhase = 0; execute(argv(m_options.value("command")), false); }
bool CommandService::parse(Kind kind, const QByteArray &output, int code, QVariantMap &state, QVariantList &items, QString &error) {
    state.clear(); items.clear(); error.clear();
    if (kind == Updates && code == 2) { state = {{"count", 0}}; return true; }
    if (code != 0) { error = QString("Command exited %1: %2").arg(code).arg(QString::fromUtf8(output).left(512)); return false; }
    if (kind == Workspaces) {
        QJsonParseError jsonError;
        const auto document = QJsonDocument::fromJson(output, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !document.isArray()) { error = "Invalid Niri workspace JSON"; return false; }
        for (const auto &entry : document.array()) {
            const auto object = entry.toObject();
            const auto id = object.value("id");
            if (!id.isDouble() || id.toInteger(-1) < 0 || !object.value("idx").isDouble() || object.value("idx").toInt() < 1 || object.value("idx").toInt() > 255
                || !object.value("is_active").isBool() || !object.value("is_focused").isBool()) { error = "Invalid Niri workspace fields"; return false; }
            auto row = object.toVariantMap();
            row["id"] = QString::number(id.toInteger()); // Preserve IDs through QML's double-based number type.
            items << row;
        }
        state = {{"count", items.size()}}; return true;
    }
    const auto text = QString::fromUtf8(output).trimmed();
    if (kind == Volume) {
        static const QRegularExpression pattern("^Volume: ([0-9]+(?:\\.[0-9]+)?)(?: (\\[MUTED\\]))?$");
        const auto match = pattern.match(text); bool ok = false; const double volume = match.captured(1).toDouble(&ok);
        if (!match.hasMatch() || !ok || !std::isfinite(volume)) { error = "Invalid wpctl volume response"; return false; }
        state = {{"percent", volume * 100}, {"muted", !match.captured(2).isEmpty()}}; return true;
    }
    for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
        if (kind == Updates) {
            static const QRegularExpression pattern("^(\\S+) (\\S+) -> (\\S+)$");
            const auto match = pattern.match(line);
            if (!match.hasMatch()) { error = "Invalid checkupdates response"; return false; }
            items << QVariantMap{{"name", match.captured(1)}, {"current", match.captured(2)}, {"next", match.captured(3)}};
        } else {
            const auto columns = fields(line); bool ok = false;
            const int signal = columns.value(2).toInt(&ok);
            if (columns.size() != 3 || !ok || signal < 0 || signal > 100 || (columns[0] != "yes" && columns[0] != "no")) { error = "Invalid nmcli WiFi response"; return false; }
            const QVariantMap row{{"active", columns[0] == "yes"}, {"ssid", columns[1]}, {"signal", signal}};
            items << row;
            if (columns[0] == "yes") state = row;
        }
    }
    state["count"] = items.size();
    if (kind == Wifi) state["connected"] = state.value("active", false);
    return true;
}
bool CommandService::act(const QString &name, const QVariantMap &args) {
    QStringList command;
    if (m_kind == Volume && name == "setVolume") {
        if (argv(m_options.value("set_volume_command")).isEmpty()) return false;
        bool ok; const double percent = args.value("percent").toDouble(&ok);
        if (!ok || !std::isfinite(percent) || percent < 0 || percent > m_options.value("max_percent").toDouble()) return false;
        m_pendingVolume = percent; m_debounce.start(m_options.value("debounce_ms").toInt()); return true;
    } else if (m_kind == Volume && name == "toggleMute") command = argv(m_options.value("mute_command"));
    else if (m_kind == Updates && name == "update") command = argv(m_options.value("update_command"));
    else if (m_kind == Wifi && name == "setPowered" && args.value("powered").metaType().id() == QMetaType::Bool) {
        command = argv(m_options.value("radio_command"));
        if (command.isEmpty()) return false;
        command << (args.value("powered").toBool() ? "on" : "off");
    } else if (m_kind == Wifi && name == "connectSaved") {
        const auto uuid = args.value("uuid").toString();
        static const QRegularExpression valid("^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$");
        if (!valid.match(uuid).hasMatch()) return false;
        bool observed = false;
        for (const auto &entry : state().value("savedConnections").toList()) observed |= entry.toMap().value("uuid").toString() == uuid;
        if (!observed) return false;
        command = argv(m_options.value("connect_command"));
        if (command.isEmpty()) return false;
        command << uuid;
    }
    return !command.isEmpty() && execute(command, true);
}
void BatteryService::poll() {
    const QDir root(m_options.value("sysfs_path").toString());
    const auto read = [](const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(4096)).trimmed() : QString(); };
    QVariantList batteries;
    for (const auto &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = root.filePath(entry);
        if (read(path + "/type") != "Battery") continue;
        if (read(path + "/present") == "0") continue;
        bool ok; const int percent = read(path + "/capacity").toInt(&ok);
        if (!ok || percent < 0 || percent > 100) continue;
        batteries << QVariantMap{{"name", entry}, {"percent", percent}, {"status", read(path + "/status")}};
    }
    if (batteries.isEmpty()) fail("No readable battery in " + root.path());
    else publish(batteries.first().toMap(), batteries);
}
}
