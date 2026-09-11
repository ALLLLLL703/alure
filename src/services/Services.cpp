#include "Services.h"
namespace Alure {
Services::Services(ConfigStore &config, QObject *parent) : QObject(parent) {
    connect(&config, &ConfigStore::modelChanged, this, [this, &config] { apply(config.model()); });
    connect(&m_volume, &Service::changed, this, [this] { m_osd.observeVolume(m_volume.available(), m_volume.state()); });
    connect(&m_brightness, &Service::changed, this, [this] { m_osd.observeBrightness(m_brightness.state()); });
    apply(config.model());
}
void Services::apply(const QVariantMap &model) {
    m_osd.configure(model.value("ui").toMap().value("osd").toMap());
    const auto modules = model.value("modules").toMap();
    const QMap<QString, Service *> services{{"taskbar", &m_taskbar}, {"workspaces", &m_workspaces}, {"media", &m_media}, {"tray", &m_tray}, {"volume", &m_volume},
        {"brightness", &m_brightness}, {"updates", &m_updates}, {"wifi", &m_wifi}, {"bluetooth", &m_bluetooth}, {"notifications", &m_notifications}, {"battery", &m_battery}, {"clipboard", &m_clipboard}};
    for (auto it = services.begin(); it != services.end(); ++it) {
        auto module = modules.value(it.key()).toMap();
        if (it.key() == "notifications" && !module.value("behavior").toMap().value("server_enabled").toBool()) module["enabled"] = false;
        if (it.key() == "taskbar" && !module.value("enabled").toBool()) {
            // Dodge shares the existing event stream, independently of taskbar UI enablement.
            for (const auto &entry : model.value("panels").toList()) {
                const auto panel = entry.toMap();
                if (panel.value("enabled").toBool() && panel.value("visibility").toMap().value("mode") == "dodge-windows") {
                    module["enabled"] = true;
                    auto behavior = module.value("behavior").toMap();
                    behavior["allow_actions"] = false;
                    module["behavior"] = behavior;
                    break;
                }
            }
        }
        it.value()->configure(module);
    }
    m_osd.observeVolume(m_volume.available(), m_volume.state());
    m_osd.observeBrightness(m_brightness.state());
}
}
