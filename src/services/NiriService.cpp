#include "NiriService.h"
#include "CommandServices.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <tuple>
namespace Alure {
NiriService::NiriService(QObject *parent) : Service(parent) {
    m_deadline.setSingleShot(true);
    m_socket.setReadBufferSize(1024 * 1024 + 1);
    connect(&m_deadline, &QTimer::timeout, this, [this] { m_socket.abort(); setBusy(false); fail("Niri request timed out"); });
    connect(&m_socket, &QLocalSocket::connected, this, [this] { m_socket.write(m_payload + '\n'); });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (!busy()) return;
        m_deadline.stop(); setBusy(false); fail("Niri: " + m_socket.errorString());
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
        m_response += m_socket.readAll();
        if (m_response.size() > 1024 * 1024) { m_deadline.stop(); m_socket.abort(); setBusy(false); fail("Niri reply exceeds 1 MiB"); return; }
        const auto end = m_response.indexOf('\n');
        if (end < 0) return;
        m_deadline.stop(); m_socket.abort(); setBusy(false);
        QJsonParseError error;
        const auto reply = QJsonDocument::fromJson(m_response.left(end), &error).object();
        if (error.error != QJsonParseError::NoError || !reply.contains("Ok")) { fail("Niri: " + reply.value("Err").toString("Invalid reply")); return; }
        if (m_action) { refresh(); return; }
        const auto workspaces = reply.value("Ok").toObject().value("Workspaces");
        QVariantMap state; QVariantList rows; QString diagnostic;
        if (!workspaces.isArray() || !CommandService::parse(CommandService::Workspaces, QJsonDocument(workspaces.toArray()).toJson(QJsonDocument::Compact), 0, state, rows, diagnostic)) {
            fail(diagnostic.isEmpty() ? "Invalid Niri workspaces reply" : diagnostic); return;
        }
        if (m_options.value("ordering").toString() == "output-index") {
            const auto key = [](const QVariant &entry) {
                const auto row = entry.toMap();
                return std::tuple{row.value("output").toString(), row.value("idx").toInt(), row.value("id").toLongLong()};
            };
            std::ranges::stable_sort(rows, {}, key);
        }
        publish(state, rows);
    });
}
void NiriService::stop() { m_deadline.stop(); m_socket.abort(); m_response.clear(); }
void NiriService::request(const QByteArray &payload, bool action) {
    m_socket.abort(); m_response.clear(); m_payload = payload; m_action = action;
    const auto configured = m_options.value("socket_path").toString();
    const auto path = configured.isEmpty() ? qEnvironmentVariable("NIRI_SOCKET") : configured;
    if (path.isEmpty()) { fail("NIRI_SOCKET is not set"); return; }
    setBusy(true); m_deadline.start(timeout()); m_socket.connectToServer(path);
}
void NiriService::poll() { request("\"Workspaces\"", false); }
bool NiriService::act(const QString &name, const QVariantMap &args) {
    if (name != "activate") return false;
    for (const auto &entry : items()) {
        const auto row = entry.toMap();
        if (row.value("id").toString() != args.value("id").toString()) continue;
        const QJsonObject payload{{"Action", QJsonObject{{"FocusWorkspace", QJsonObject{{"reference", QJsonObject{{"Id", QJsonValue(row.value("id").toLongLong())}}}}}}}};
        request(QJsonDocument(payload).toJson(QJsonDocument::Compact), true); return true;
    }
    return false;
}
}
