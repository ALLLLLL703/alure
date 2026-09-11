#include "NiriService.h"
#include "CommandServices.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QSignalBlocker>
#include <algorithm>
#include <tuple>
namespace Alure {
namespace {
constexpr int maximumLine = 1024 * 1024;
bool validId(const QJsonValue &value) { return value.isDouble() && value.toInteger(-1) >= 0; }
}
NiriService::NiriService(QObject *parent) : Service(parent) {
    m_streamDeadline.setSingleShot(true); m_actionDeadline.setSingleShot(true);
    m_stream.setReadBufferSize(maximumLine + 1); m_actionSocket.setReadBufferSize(maximumLine + 1);
    connect(&m_stream, &QLocalSocket::connected, this, [this] { m_stream.write("\"EventStream\"\n"); });
    connect(&m_streamDeadline, &QTimer::timeout, this, [this] { streamFailed("Niri workspace initial snapshot timed out"); });
    connect(&m_stream, &QLocalSocket::errorOccurred, this, [this] { streamFailed("Niri workspaces: " + m_stream.errorString()); });
    connect(&m_stream, &QLocalSocket::disconnected, this, [this] { streamFailed("Niri workspace stream disconnected"); });
    connect(&m_stream, &QLocalSocket::readyRead, this, [this] {
        // Bound each fragmented line, not a batch of individually valid events.
        while (m_stream.bytesAvailable()) {
            const auto remaining = maximumLine + 1 - m_streamBuffer.size();
            m_streamBuffer += m_stream.read(remaining);
            while (true) {
                const auto end = m_streamBuffer.indexOf('\n');
                if (end < 0) break;
                const auto line = m_streamBuffer.left(end); m_streamBuffer.remove(0, end + 1);
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(line, &error);
                if (error.error != QJsonParseError::NoError || !document.isObject()) { streamFailed("Invalid Niri workspace event"); return; }
                if (!consume(document.object())) return;
            }
            if (m_streamBuffer.size() > maximumLine) { streamFailed("Niri workspace event exceeds 1 MiB"); return; }
        }
    });
    connect(&m_actionSocket, &QLocalSocket::connected, this, [this] { m_actionSocket.write(m_actionPayload + '\n'); });
    connect(&m_actionDeadline, &QTimer::timeout, this, [this] { actionFinished("Niri workspace action timed out"); });
    connect(&m_actionSocket, &QLocalSocket::errorOccurred, this, [this] {
        if (m_actionPending) actionFinished("Niri workspace action: " + m_actionSocket.errorString());
    });
    connect(&m_actionSocket, &QLocalSocket::disconnected, this, [this] {
        if (m_actionPending) actionFinished("Niri workspace action disconnected before acknowledgement");
    });
    connect(&m_actionSocket, &QLocalSocket::readyRead, this, [this] {
        m_actionBuffer += m_actionSocket.readAll();
        if (m_actionBuffer.size() > maximumLine) { actionFinished("Niri workspace action reply exceeds 1 MiB"); return; }
        const auto end = m_actionBuffer.indexOf('\n'); if (end < 0) return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_actionBuffer.left(end), &error);
        const auto reply = document.object();
        if (error.error != QJsonParseError::NoError || reply.size() != 1 || reply.value("Ok").toString() != "Handled") {
            actionFinished("Niri workspace action: " + reply.value("Err").toString("Invalid acknowledgement")); return;
        }
        // Only events change active/focused state, never successful action ACKs.
        actionFinished();
    });
}
NiriService::~NiriService() { stop(); }
QString NiriService::socketPath() const {
    const auto configured = m_options.value("socket_path").toString();
    return configured.isEmpty() ? qEnvironmentVariable("NIRI_SOCKET") : configured;
}
void NiriService::stop() {
    m_streamDeadline.stop(); m_actionDeadline.stop();
    const QSignalBlocker streamBlocker(&m_stream), actionBlocker(&m_actionSocket);
    m_stream.abort(); m_actionSocket.abort();
    m_streamBuffer.clear(); m_actionBuffer.clear(); m_actionPayload.clear(); m_workspaces.clear(); m_actionError.clear();
    m_subscribed = m_haveWorkspaces = m_actionPending = false;
}
void NiriService::streamFailed(const QString &message) { stop(); setBusy(false); fail(message); }
void NiriService::actionFinished(const QString &error) {
    m_actionPending = false; m_actionDeadline.stop();
    { const QSignalBlocker blocker(&m_actionSocket); m_actionSocket.abort(); }
    m_actionBuffer.clear(); m_actionPayload.clear(); m_actionError = error; setBusy(false);
    // A rejected action does not invalidate an otherwise healthy subscription.
    if (m_haveWorkspaces) publishWorkspaces();
}
void NiriService::poll() {
    if (m_stream.state() != QLocalSocket::UnconnectedState) return;
    const auto path = socketPath();
    if (path.isEmpty()) { fail("NIRI_SOCKET is not set"); return; }
    m_streamDeadline.start(timeout()); m_stream.connectToServer(path);
}
bool NiriService::consume(const QJsonObject &event) {
    if (event.contains("Err")) { streamFailed("Niri workspaces: " + event.value("Err").toString("Invalid reply")); return false; }
    if (!m_subscribed) {
        if (event.size() != 1 || event.value("Ok").toString() != "Handled") { streamFailed("Invalid Niri event stream acknowledgement"); return false; }
        m_subscribed = true; return true;
    }
    if (event.contains("WorkspacesChanged")) {
        const auto workspaces = event.value("WorkspacesChanged").toObject().value("workspaces");
        QVariantMap snapshot; QVariantList rows; QString diagnostic;
        if (!workspaces.isArray() || !CommandService::parse(CommandService::Workspaces,
                QJsonDocument(workspaces.toArray()).toJson(QJsonDocument::Compact), 0, snapshot, rows, diagnostic)) {
            streamFailed(diagnostic.isEmpty() ? "Invalid Niri workspaces snapshot" : diagnostic); return false;
        }
        QSet<QString> ids;
        for (const auto &entry : rows) {
            const auto id = entry.toMap().value("id").toString();
            if (ids.contains(id)) { streamFailed("Duplicate Niri workspace ID"); return false; }
            ids.insert(id);
        }
        m_workspaces = rows; m_haveWorkspaces = true; m_streamDeadline.stop();
    } else if (event.contains("WorkspaceActivated")) {
        const auto activation = event.value("WorkspaceActivated").toObject();
        if (!validId(activation.value("id")) || !activation.value("focused").isBool()) { streamFailed("Invalid Niri workspace activation"); return false; }
        const auto id = QString::number(activation.value("id").toInteger());
        const auto target = std::ranges::find_if(m_workspaces, [&id](const QVariant &entry) { return entry.toMap().value("id").toString() == id; });
        if (target == m_workspaces.end()) return true; // May refer to a just-removed workspace.
        const auto output = target->toMap().value("output");
        for (auto &entry : m_workspaces) {
            auto row = entry.toMap(); const bool selected = row.value("id").toString() == id;
            if (row.value("output") == output) row["is_active"] = selected;
            if (activation.value("focused").toBool()) row["is_focused"] = selected;
            entry = row;
        }
    } else if (event.contains("WorkspaceUrgencyChanged") || event.contains("WorkspaceActiveWindowChanged")) {
        const bool urgency = event.contains("WorkspaceUrgencyChanged");
        const auto update = event.value(urgency ? "WorkspaceUrgencyChanged" : "WorkspaceActiveWindowChanged").toObject();
        const auto id = update.value(urgency ? "id" : "workspace_id");
        const auto value = update.value(urgency ? "urgent" : "active_window_id");
        if (!validId(id) || (urgency ? !value.isBool() : !(value.isNull() || validId(value)))) { streamFailed("Invalid Niri workspace update"); return false; }
        for (auto &entry : m_workspaces) {
            auto row = entry.toMap();
            if (row.value("id").toString() != QString::number(id.toInteger())) continue;
            row[urgency ? "is_urgent" : "active_window_id"] = value.toVariant(); entry = row; break;
        }
    } else return true; // Windows, geometry and other consumers remain independent.
    if (m_haveWorkspaces) publishWorkspaces();
    return true;
}
void NiriService::publishWorkspaces() {
    auto rows = m_workspaces;
    if (m_options.value("ordering").toString() == "output-index") {
        std::ranges::stable_sort(rows, {}, [](const QVariant &entry) {
            const auto row = entry.toMap();
            return std::tuple{row.value("output").toString(), row.value("idx").toInt(), row.value("id").toLongLong()};
        });
    }
    QVariantMap snapshot{{"count", rows.size()}};
    if (!m_actionError.isEmpty()) snapshot["actionError"] = m_actionError;
    if (!available() || state() != snapshot || items() != rows) publish(snapshot, rows);
}
bool NiriService::act(const QString &name, const QVariantMap &args) {
    if (name != "activate" || !available() || !m_haveWorkspaces) return false;
    for (const auto &entry : m_workspaces) {
        const auto row = entry.toMap();
        if (row.value("id").toString() != args.value("id").toString()) continue;
        const QJsonObject payload{{"Action", QJsonObject{{"FocusWorkspace", QJsonObject{{"reference", QJsonObject{{"Id", QJsonValue(row.value("id").toLongLong())}}}}}}}};
        m_actionPayload = QJsonDocument(payload).toJson(QJsonDocument::Compact); m_actionBuffer.clear();
        m_actionPending = true; setBusy(true); m_actionDeadline.start(timeout()); m_actionSocket.connectToServer(socketPath()); return true;
    }
    return false;
}
}
