#include "OsdController.h"

namespace Alure {
void OsdController::configure(const QVariantMap &options) {
    m_options = options; m_baselines.clear(); emit reset();
}
void OsdController::observe(const QString &kind, const QString &identity, const QVariantMap &value) {
    if (value.isEmpty()) { m_baselines.remove(kind); return; }
    const auto previous = m_baselines.value(kind);
    m_baselines[kind] = {identity, value};
    if (previous.first != identity || previous.second.isEmpty() || previous.second == value ||
        !m_options.value("enabled").toBool() || !m_options.value(kind + "_enabled").toBool()) return;
    auto snapshot = value; snapshot["kind"] = kind; emit requested(snapshot);
}
void OsdController::observeVolume(bool available, const QVariantMap &state) {
    if (!available || !state.contains("percent")) { observe("volume", {}, {}); return; }
    observe("volume", state.value("backend").toString() + ':' + state.value("sinkIndex").toString() + ':' + state.value("sinkName").toString(),
            {{"percent", state.value("percent")}, {"muted", state.value("muted").toBool()}});
}
void OsdController::observeBrightness(const QVariantMap &state) {
    for (const auto &kind : {QString("screen"), QString("keyboard")}) {
        const auto value = state.value(kind).toMap();
        if (!value.value("available").toBool()) { observe(kind, {}, {}); continue; }
        observe(kind, value.value("device").toString() + ':' + value.value("maximum").toString(),
                {{"percent", value.value("percent")}, {"level", value.value("level")}, {"maximum", value.value("maximum")}});
    }
}
}
