#include "Services.h"
namespace Alure {
Services::Services(ConfigStore &config, QObject *parent) : QObject(parent) {
    connect(&config, &ConfigStore::modelChanged, this, [this, &config] { apply(config.model()); });
    apply(config.model());
}
void Services::apply(const QVariantMap &model) {
    const auto modules = model.value("modules").toMap();
    const QMap<QString, Service *> services{{"workspaces", &m_workspaces}, {"media", &m_media}, {"tray", &m_tray}, {"volume", &m_volume},
        {"updates", &m_updates}, {"wifi", &m_wifi}, {"bluetooth", &m_bluetooth}, {"notifications", &m_notifications}, {"battery", &m_battery}};
    for (auto it = services.begin(); it != services.end(); ++it) {
        auto module = modules.value(it.key()).toMap();
        if (it.key() == "notifications" && !module.value("behavior").toMap().value("server_enabled").toBool()) module["enabled"] = false;
        it.value()->configure(module);
    }
}
}
