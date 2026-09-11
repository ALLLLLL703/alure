#include "TaskbarService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <tuple>

namespace Alure {
namespace {
// IDs stay decimal strings in QML, avoiding JavaScript's 53-bit number limit.
quint64 idOf(const QJsonValue &value) { return value.toInteger(-1); }
QVariantMap windowRow(const QJsonObject &window) {
    auto row = window.toVariantMap();
    row["id"] = QString::number(idOf(window.value("id")));
    row["workspace_id"] = window.value("workspace_id").isNull() ? QString() : QString::number(idOf(window.value("workspace_id")));
    return row;
}
}
TaskbarService::TaskbarService(QObject *parent) : Service(parent) {
    m_streamDeadline.setSingleShot(true); m_actionDeadline.setSingleShot(true);
    m_stream.setReadBufferSize(1024 * 1024 + 1);
    m_actionSocket.setReadBufferSize(1024 * 1024 + 1);
    connect(&m_stream, &QLocalSocket::connected, this, [this] { m_stream.write("\"EventStream\"\n"); });
    connect(&m_streamDeadline, &QTimer::timeout, this, [this] { streamFailed("Niri taskbar initial snapshot timed out"); });
    connect(&m_stream, &QLocalSocket::errorOccurred, this, [this] { streamFailed("Niri taskbar: " + m_stream.errorString()); });
    connect(&m_stream, &QLocalSocket::disconnected, this, [this] {
        if (m_haveWindows || m_haveWorkspaces) streamFailed("Niri taskbar disconnected");
    });
    connect(&m_stream, &QLocalSocket::readyRead, this, [this] {
        m_streamBuffer += m_stream.readAll();
        while (true) {
            const auto end = m_streamBuffer.indexOf('\n');
            if (end < 0) break;
            if (end > 1024 * 1024) { streamFailed("Niri taskbar event exceeds 1 MiB"); return; }
            const auto line = m_streamBuffer.left(end); m_streamBuffer.remove(0, end + 1);
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) { streamFailed("Invalid Niri taskbar event"); return; }
            const auto event = document.object();
            if (event.contains("Err")) { streamFailed("Niri taskbar: " + event.value("Err").toString()); return; }
            consume(event);
        }
        if (m_streamBuffer.size() > 1024 * 1024) streamFailed("Niri taskbar event exceeds 1 MiB");
    });
    connect(&m_actionSocket, &QLocalSocket::connected, this, [this] { m_actionSocket.write(m_actionPayload + '\n'); });
    const auto actionFailed = [this](const QString &error) {
        m_actionDeadline.stop(); m_actionSocket.abort(); setBusy(false);
        auto snapshot = state(); snapshot["actionError"] = error; publish(snapshot, items());
    };
    connect(&m_actionDeadline, &QTimer::timeout, this, [actionFailed] { actionFailed("Niri focus request timed out"); });
    connect(&m_actionSocket, &QLocalSocket::errorOccurred, this, [this, actionFailed] {
        if (busy()) actionFailed("Niri focus: " + m_actionSocket.errorString());
    });
    connect(&m_actionSocket, &QLocalSocket::readyRead, this, [this, actionFailed] {
        m_actionBuffer += m_actionSocket.readAll();
        if (m_actionBuffer.size() > 1024 * 1024) { actionFailed("Niri focus reply exceeds 1 MiB"); return; }
        const auto end = m_actionBuffer.indexOf('\n'); if (end < 0) return;
        const auto reply = QJsonDocument::fromJson(m_actionBuffer.left(end)).object();
        if (!reply.contains("Ok")) { actionFailed("Niri focus: " + reply.value("Err").toString("Invalid reply")); return; }
        m_actionDeadline.stop(); m_actionSocket.abort(); setBusy(false);
        // Active indication is changed only by compositor events, not acknowledgements.
        publishWindows();
    });
}
TaskbarService::~TaskbarService() { stop(); }
QString TaskbarService::socketPath() const {
    const auto configured = m_options.value("socket_path").toString();
    return configured.isEmpty() ? qEnvironmentVariable("NIRI_SOCKET") : configured;
}
void TaskbarService::stop() {
    m_haveWindows = m_haveWorkspaces = false;
    m_streamDeadline.stop(); m_actionDeadline.stop(); m_stream.abort(); m_actionSocket.abort();
    m_streamBuffer.clear(); m_actionBuffer.clear(); m_windows.clear(); m_workspaces.clear();
    m_panelWindows.clear();
    emit panelWindowsChanged({}, false);
}
void TaskbarService::streamFailed(const QString &message) { stop(); setBusy(false); fail(message); }
void TaskbarService::poll() {
    if (m_stream.state() != QLocalSocket::UnconnectedState) return;
    const auto path = socketPath();
    if (path.isEmpty()) { fail("NIRI_SOCKET is not set for taskbar"); return; }
    m_streamDeadline.start(timeout()); m_stream.connectToServer(path);
}
void TaskbarService::consume(const QJsonObject &event) {
    if (event.contains("WindowsChanged")) {
        const auto windows = event.value("WindowsChanged").toObject().value("windows");
        if (!windows.isArray()) { streamFailed("Invalid Niri windows snapshot"); return; }
        m_windows.clear();
        for (const auto &entry : windows.toArray()) {
            const auto window = entry.toObject();
            if (!window.value("id").isDouble()) { streamFailed("Invalid Niri window ID"); return; }
            m_windows[idOf(window.value("id"))] = windowRow(window);
        }
        m_haveWindows = true;
    } else if (event.contains("WorkspacesChanged")) {
        const auto workspaces = event.value("WorkspacesChanged").toObject().value("workspaces");
        if (!workspaces.isArray()) { streamFailed("Invalid Niri workspace snapshot"); return; }
        m_workspaces.clear();
        for (const auto &entry : workspaces.toArray()) {
            const auto workspace = entry.toObject();
            m_workspaces[idOf(workspace.value("id"))] = workspace.toVariantMap();
        }
        m_haveWorkspaces = true;
    } else if (event.contains("WindowOpenedOrChanged")) {
        const auto window = event.value("WindowOpenedOrChanged").toObject().value("window").toObject();
        if (!window.value("id").isDouble()) { streamFailed("Invalid Niri window event"); return; }
        if (window.value("is_focused").toBool()) for (auto &row : m_windows) row["is_focused"] = false;
        m_windows[idOf(window.value("id"))] = windowRow(window);
    } else if (event.contains("WindowLayoutsChanged")) {
        const auto changes = event.value("WindowLayoutsChanged").toObject().value("changes");
        if (!changes.isArray()) { streamFailed("Invalid Niri window layouts event"); return; }
        for (const auto &entry : changes.toArray()) {
            const auto pair = entry.toArray();
            if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isObject()) {
                streamFailed("Invalid Niri window layout pair"); return;
            }
            const auto id = idOf(pair[0]);
            if (m_windows.contains(id)) m_windows[id]["layout"] = pair[1].toObject().toVariantMap();
        }
    } else if (event.contains("WindowClosed")) {
        m_windows.remove(idOf(event.value("WindowClosed").toObject().value("id")));
    } else if (event.contains("WindowFocusChanged")) {
        const auto id = event.value("WindowFocusChanged").toObject().value("id");
        for (auto it = m_windows.begin(); it != m_windows.end(); ++it) it.value()["is_focused"] = !id.isNull() && it.key() == idOf(id);
    } else if (event.contains("WorkspaceActivated")) {
        const auto activation = event.value("WorkspaceActivated").toObject();
        const auto id = idOf(activation.value("id"));
        if (!m_workspaces.contains(id)) return; // Niri may temporarily refer to removed workspaces.
        const auto output = m_workspaces.value(id).value("output");
        for (auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it) {
            if (it.value().value("output") == output) it.value()["is_active"] = it.key() == id;
            if (activation.value("focused").toBool()) it.value()["is_focused"] = it.key() == id;
        }
    } else return; // Other compositor events do not affect task presentation.
    if (m_haveWindows && m_haveWorkspaces) { m_streamDeadline.stop(); publishWindows(event.contains("WindowLayoutsChanged")); }
}
void TaskbarService::publishWindows(bool layoutOnly) {
    if (!m_haveWindows || !m_haveWorkspaces) return;
    QString focusedOutput;
    for (const auto &workspace : m_workspaces) if (workspace.value("is_focused").toBool()) focusedOutput = workspace.value("output").toString();
    QVariantList rows;
    for (const auto &window : m_windows) {
        auto row = window;
        const auto workspaceId = row.value("workspace_id").toString();
        const auto workspace = workspaceId.isEmpty() ? QVariantMap{} : m_workspaces.value(workspaceId.toULongLong());
        row["output"] = workspace.value("output").toString();
        row["workspace_active"] = workspace.value("is_active", false);
        row["workspace_focused"] = workspace.value("is_focused", false);
        row["output_focused"] = !focusedOutput.isEmpty() && row.value("output").toString() == focusedOutput;
        rows.append(row);
    }
    const auto ordering = m_options.value("ordering").toString();
    std::ranges::stable_sort(rows, {}, [ordering](const QVariant &entry) {
        const auto row = entry.toMap();
        return std::tuple{ordering == "app-id" ? row.value("app_id").toString().toCaseFolded() : ordering == "title" ? row.value("title").toString().toCaseFolded() : QString(), row.value("id").toString().toULongLong()};
    });
    const bool firstSnapshot = !available();
    if (firstSnapshot || m_panelWindows != rows) {
        m_panelWindows = rows;
        emit panelWindowsChanged(m_panelWindows, true);
    }
    if (layoutOnly && !firstSnapshot) return;
    // Layout is consumed only by panel visibility, not task presentation. Keep
    // Service::changed quiet for geometry-only events so QML retains delegates.
    for (auto &entry : rows) {
        auto row = entry.toMap();
        row.remove("layout");
        entry = row;
    }
    const QVariantMap snapshot{{"count", rows.size()}};
    if (firstSnapshot || state() != snapshot || items() != rows) publish(snapshot, rows);
}
bool TaskbarService::act(const QString &name, const QVariantMap &args) {
    if (name != "activate" || !available() || !m_options.value("focus_on_click").toBool()) return false;
    bool valid = false; const auto id = args.value("id").toString().toULongLong(&valid);
    if (!valid || !m_windows.contains(id)) return false;
    const QJsonObject request{{"Action", QJsonObject{{"FocusWindow", QJsonObject{{"id", QJsonValue(static_cast<qint64>(id))}}}}}};
    m_actionPayload = QJsonDocument(request).toJson(QJsonDocument::Compact); m_actionBuffer.clear();
    setBusy(true); m_actionDeadline.start(timeout()); m_actionSocket.connectToServer(socketPath()); return true;
}
}
