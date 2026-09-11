#include "VolumeService.h"
#include "CommandServices.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <utility>

namespace Alure {
VolumeService::VolumeService(QObject *parent) : Service(parent) {
    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (!m_pending || !enabled()) return;
        if (busy()) { m_debounce.start(m_options.value("debounce_ms").toInt()); return; }
        const auto pending = std::exchange(m_pending, std::nullopt).value();
        if (!available() || !m_options.value("allow_actions").toBool()) return;
        if (pending.backend != m_active || pending.sink != m_sink) {
            fail("Audio output changed before the volume adjustment; retry the control."); return;
        }
        auto argv = command(volumeKey());
        if (argv.isEmpty()) return;
        if (m_active == Backend::PulseAudio) argv << m_sink << QString::number(pending.percent, 'f', 3) + "%";
        else argv << QString::number(pending.percent / 100., 'f', 3);
        executeAction(argv);
    });
    connect(&m_runner, &CommandRunner::completed, this, [this](int code, const QByteArray &output, const QString &processError) {
        const auto generation = m_generation;
        // A FailedToStart callback is still inside QProcess cleanup. Defer a
        // fallback launch until it returns, and discard obsolete config results.
        QTimer::singleShot(0, this, [this, generation, code, output, processError] {
        if (generation != m_generation) return;
        setBusy(false);
        if (!enabled() || m_phase == Phase::Idle) return;
        if (code != 0 || !processError.isEmpty()) {
            const auto error = processError.isEmpty() ? QString("Command exited %1: %2").arg(code).arg(QString::fromUtf8(output).left(512)) : processError;
            if (m_phase == Phase::Action) { m_phase = Phase::Idle; m_pending.reset(); fail("Audio action failed: " + error); }
            else readFailed(error);
            return;
        }
        if (m_phase == Phase::Action) { m_phase = Phase::Idle; refresh(); return; }
        QVariantMap snapshot; QString error;
        if (m_phase == Phase::PipeWire) {
            QVariantList rows;
            if (!CommandService::parse(CommandService::Volume, output, 0, snapshot, rows, error)) { readFailed(error); return; }
        } else if (m_phase == Phase::PulseInfo) {
            const auto info = QJsonDocument::fromJson(output).object();
            if (info.value("default_sink_name").toString().isEmpty()) { readFailed("PulseAudio has no default sink, or pactl info returned invalid JSON."); return; }
            m_pulseInfo = output; read("pulse_sinks_command", Phase::PulseSinks); return;
        } else if (!parsePulse(m_pulseInfo, output, snapshot, error)) { readFailed(error); return; }
        publishVolume(snapshot);
        });
    });
}
VolumeService::~VolumeService() { stop(); }
QStringList VolumeService::command(const char *key) const {
    QStringList argv; for (const auto &value : m_options.value(key).toList()) argv << value.toString(); return argv;
}
const char *VolumeService::volumeKey() const { return m_active == Backend::PulseAudio ? "pulse_set_volume_command" : "set_volume_command"; }
const char *VolumeService::muteKey() const { return m_active == Backend::PulseAudio ? "pulse_mute_command" : "mute_command"; }
void VolumeService::stop() {
    m_phase = Phase::Idle; m_debounce.stop(); m_pending.reset(); m_runner.cancel();
    m_pulseInfo.clear(); m_sink.clear(); m_attempted.clear(); m_errors.clear(); m_active = Backend::PipeWire;
}
void VolumeService::poll() {
    m_attempted.clear(); m_errors.clear();
    const auto backend = m_options.value("backend").toString();
    probe(backend == "pulseaudio" ? Backend::PulseAudio : backend == "pipewire" ? Backend::PipeWire : m_active);
}
void VolumeService::probe(Backend backend) {
    m_probe = backend; m_attempted.insert(backend);
    read(backend == Backend::PulseAudio ? "pulse_info_command" : "command", backend == Backend::PulseAudio ? Phase::PulseInfo : Phase::PipeWire);
}
void VolumeService::read(const char *key, Phase phase) {
    m_phase = phase; setBusy(true);
    if (!m_runner.run(command(key), timeout())) { setBusy(false); readFailed("Read command is empty or the previous process is stopping."); }
}
void VolumeService::readFailed(const QString &error) {
    m_errors << (m_probe == Backend::PulseAudio ? "PulseAudio: " : "PipeWire: ") + error;
    const auto other = m_probe == Backend::PulseAudio ? Backend::PipeWire : Backend::PulseAudio;
    if (m_options.value("backend").toString() == "auto" && !m_attempted.contains(other)) { probe(other); return; }
    m_phase = Phase::Idle; m_pulseInfo.clear(); m_pending.reset();
    fail("No readable audio output. " + m_errors.join("; "));
}
void VolumeService::publishVolume(QVariantMap snapshot) {
    m_active = m_probe; m_phase = Phase::Idle; m_pulseInfo.clear();
    m_sink = snapshot.value("sinkIndex").toString();
    snapshot["backend"] = m_active == Backend::PulseAudio ? "pulseaudio" : "pipewire";
    snapshot["canSetVolume"] = !command(volumeKey()).isEmpty();
    snapshot["canMute"] = !command(muteKey()).isEmpty();
    publish(snapshot);
}
bool VolumeService::executeAction(QStringList argv) {
    m_phase = Phase::Action; setBusy(true);
    if (m_runner.run(argv, timeout())) return true;
    m_phase = Phase::Idle; setBusy(false); fail("Audio action command is empty or the previous process is stopping."); return false;
}
bool VolumeService::act(const QString &name, const QVariantMap &args) {
    if (!available()) return false;
    if (name == "setVolume") {
        bool ok = false; const double percent = args.value("percent").toDouble(&ok);
        if (command(volumeKey()).isEmpty() || !ok || !std::isfinite(percent) || percent < 0 || percent > m_options.value("max_percent").toDouble()) return false;
        m_pending = PendingVolume{percent, m_active, m_sink}; m_debounce.start(m_options.value("debounce_ms").toInt()); return true;
    }
    if (name != "toggleMute") return false;
    auto argv = command(muteKey()); if (argv.isEmpty()) return false;
    if (m_active == Backend::PulseAudio) argv << m_sink << "toggle";
    return executeAction(argv);
}
bool VolumeService::parsePulse(const QByteArray &infoBytes, const QByteArray &sinkBytes, QVariantMap &state, QString &error) {
    state.clear(); error.clear();
    const auto info = QJsonDocument::fromJson(infoBytes).object();
    const auto defaultSink = info.value("default_sink_name").toString();
    const auto sinks = QJsonDocument::fromJson(sinkBytes);
    if (defaultSink.isEmpty() || !sinks.isArray()) { error = "Invalid pactl info/sinks JSON or missing default sink."; return false; }
    for (const auto &entry : sinks.array()) {
        const auto sink = entry.toObject();
        if (sink.value("name").toString() != defaultSink) continue;
        const auto index = sink.value("index").toInteger(-1);
        const auto channels = sink.value("volume").toObject();
        if (index < 0 || index >= 0xffffffffLL || !sink.value("mute").isBool() || channels.isEmpty() || channels.size() > 32) { error = "Invalid default sink fields."; return false; }
        double total = 0;
        for (auto it = channels.begin(); it != channels.end(); ++it) {
            const auto value = it.value().toObject().value("value").toInteger(-1);
            if (value < 0 || value > 0x7fffffffLL) { error = "Invalid PulseAudio channel volume."; return false; }
            total += static_cast<double>(value);
        }
        state = {{"percent", std::round(total / channels.size() * 10000. / 65536.) / 100.}, {"muted", sink.value("mute").toBool()},
                 {"sinkIndex", QString::number(index)}, {"sinkName", defaultSink}, {"description", sink.value("description").toString()},
                 {"serverName", info.value("server_name").toString()}};
        return true;
    }
    error = "PulseAudio default sink is missing from the current sink list."; return false;
}
}
