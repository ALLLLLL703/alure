#include "BrightnessService.h"
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Alure {
namespace {
std::optional<int> readNumber(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    bool ok = false;
    const auto value = file.read(64).trimmed().toLongLong(&ok);
    if (!ok || value < 0 || value > std::numeric_limits<int>::max()) return {};
    return static_cast<int>(value);
}
bool keyboardDevice(const QString &name) { return name.endsWith(":kbd_backlight"); }
}
BrightnessService::BrightnessService(QObject *parent) : Service(parent) {
    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, &BrightnessService::dispatch);
    connect(&m_runner, &CommandRunner::completed, this, [this](int code, const QByteArray &output, const QString &error) {
        const auto generation = m_generation;
        QTimer::singleShot(0, this, [this, generation, code, output, error] {
            if (generation != m_generation || !enabled()) return;
            m_dispatched.reset();
            if (code != 0 || !error.isEmpty()) {
                m_pending.reset(); m_debounce.stop();
                m_actionError = "Brightness action failed (check brightnessctl permissions): " +
                    (error.isEmpty() ? QString("exit %1: %2").arg(code).arg(QString::fromUtf8(output).left(512)) : error);
            } else m_actionError.clear();
            setBusy(false);
            poll(); // Never publish the requested value as a hardware reading.
        });
    });
}
BrightnessService::~BrightnessService() { stop(); }
void BrightnessService::stop() {
    m_debounce.stop(); m_pending.reset(); m_dispatched.reset(); m_runner.cancel(); m_actionError.clear();
}
QStringList BrightnessService::command() const {
    QStringList result;
    for (const auto &arg : m_options.value("set_command").toList()) result << arg.toString();
    return result;
}
QVariantMap BrightnessService::readDevice(const QString &kind) const {
    const auto options = m_options.value(kind).toMap();
    QVariantMap result{{"available", false}, {"diagnostic", "Disabled"}};
    if (!options.value("enabled").toBool()) return result;
    const QDir directory(options.value("sysfs_path").toString());
    auto device = options.value("device").toString();
    if (device.isEmpty()) {
        for (const auto &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (kind == "keyboard" && !keyboardDevice(name)) continue;
            const auto maximum = readNumber(directory.filePath(name + "/max_brightness"));
            const auto level = readNumber(directory.filePath(name + "/brightness"));
            if (maximum && *maximum > 0 && level && *level <= *maximum) { device = name; break; }
        }
    }
    if (device.isEmpty() || (kind == "keyboard" && !keyboardDevice(device))) {
        result["diagnostic"] = "No readable " + kind + " backlight device in " + directory.path(); return result;
    }
    const auto maximum = readNumber(directory.filePath(device + "/max_brightness"));
    const auto level = readNumber(directory.filePath(device + "/brightness"));
    if (!maximum || *maximum == 0 || !level || *level > *maximum) {
        result["diagnostic"] = "Missing, unreadable or invalid sysfs brightness: " + directory.filePath(device); return result;
    }
    const int minimum = kind == "keyboard" ? options.value("min_level").toInt()
        : static_cast<int>(std::ceil(*maximum * options.value("min_percent").toDouble() / 100.));
    const int limit = kind == "keyboard" ? (options.value("max_level").toInt() == -1 ? *maximum : std::min(*maximum, options.value("max_level").toInt()))
        : static_cast<int>(std::floor(*maximum * options.value("max_percent").toDouble() / 100.));
    return {{"available", true}, {"device", device}, {"level", *level}, {"maximum", *maximum},
            {"percent", std::round(*level * 10000. / *maximum) / 100.}, {"minimum", minimum}, {"limit", limit},
            {"canSet", options.value("allow_actions").toBool() && minimum <= limit && !command().isEmpty()},
            {"diagnostic", minimum > limit ? "Configured limits contain no hardware levels" : ""}};
}
void BrightnessService::poll() {
    const auto screen = readDevice("screen"), keyboard = readDevice("keyboard");
    const auto primary = screen.value("available").toBool() ? screen : keyboard;
    // Partial availability retains both per-kind diagnostics, including write failures.
    publish({{"screen", screen}, {"keyboard", keyboard}, {"percent", primary.value("percent")}, {"actionError", m_actionError}});
}
bool BrightnessService::canQueueAction(const QString &name) const {
    return name == "setBrightness" || name == "adjustBrightness";
}
bool BrightnessService::act(const QString &name, const QVariantMap &args) {
    if (!available() || !canQueueAction(name)) return false;
    const auto kind = args.value("kind").toString();
    if (kind != "screen" && kind != "keyboard") return false;
    const auto observed = state().value(kind).toMap();
    if (!observed.value("available").toBool() || !observed.value("canSet").toBool()) return false;
    bool ok = false;
    double value = args.value(name == "adjustBrightness" ? "delta" : kind == "keyboard" ? "level" : "percent").toDouble(&ok);
    if (!ok || !std::isfinite(value) || (kind == "keyboard" && std::floor(value) != value)) return false;
    const auto device = observed.value("device").toString();
    const int maximum = observed.value("maximum").toInt();
    const auto &desired = m_pending && m_pending->kind == kind ? m_pending : m_dispatched;
    if (name == "adjustBrightness") {
        const int base = desired && desired->kind == kind && desired->device == device && desired->maximum == maximum
            ? desired->level : observed.value("level").toInt();
        // Keyboard input is in native levels, never 1% steps on a 0..2 LED.
        if (kind == "keyboard") value = base + value;
        else value = base * 100. / maximum + value;
    }
    if (kind == "screen") value = value * maximum / 100.;
    const int minimum = observed.value("minimum").toInt(), limit = observed.value("limit").toInt();
    value = std::clamp(value, static_cast<double>(minimum), static_cast<double>(limit));
    m_pending = Target{kind, device, static_cast<int>(std::round(value)), maximum};
    if (!m_debounce.isActive()) m_debounce.start(m_options.value("debounce_ms").toInt());
    emit changed(); return true;
}
void BrightnessService::dispatch() {
    if (!m_pending || !enabled()) return;
    if (busy() || m_runner.running()) { m_debounce.start(m_options.value("debounce_ms").toInt()); return; }
    const auto target = std::exchange(m_pending, std::nullopt).value();
    const auto current = readDevice(target.kind);
    if (!m_options.value("allow_actions").toBool() || !current.value("available").toBool() || !current.value("canSet").toBool()
        || current.value("device").toString() != target.device || current.value("maximum").toInt() != target.maximum) {
        m_actionError = "Backlight changed before adjustment; retry the control."; poll(); return;
    }
    auto argv = command();
    argv << "--class" << (target.kind == "keyboard" ? "leds" : "backlight") << "--device" << target.device << "set" << QString::number(target.level);
    m_dispatched = target; m_actionError.clear(); setBusy(true);
    if (!m_runner.run(argv, timeout())) {
        m_dispatched.reset(); setBusy(false); m_actionError = "Brightness command unavailable or still stopping"; poll();
    }
}
}
