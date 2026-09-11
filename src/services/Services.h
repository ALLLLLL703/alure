#pragma once
#include "CommandServices.h"
#include "VolumeService.h"
#include "BrightnessService.h"
#include "OsdController.h"
#include "NiriService.h"
#include "TaskbarService.h"
#include "DBusServices.h"
#include "TrayService.h"
#include "NotificationService.h"
#include "ConfigStore.h"
#include "ClipboardService.h"
namespace Alure {
class Services : public QObject {
    Q_OBJECT
    Q_PROPERTY(Alure::Service* taskbar READ taskbar CONSTANT)
    Q_PROPERTY(Alure::Service* workspaces READ workspaces CONSTANT)
    Q_PROPERTY(Alure::Service* media READ media CONSTANT)
    Q_PROPERTY(Alure::Service* tray READ tray CONSTANT)
    Q_PROPERTY(Alure::Service* volume READ volume CONSTANT)
    Q_PROPERTY(Alure::Service* brightness READ brightness CONSTANT)
    Q_PROPERTY(Alure::Service* updates READ updates CONSTANT)
    Q_PROPERTY(Alure::Service* wifi READ wifi CONSTANT)
    Q_PROPERTY(Alure::Service* bluetooth READ bluetooth CONSTANT)
    Q_PROPERTY(Alure::Service* notifications READ notifications CONSTANT)
    Q_PROPERTY(Alure::Service* battery READ battery CONSTANT)
    Q_PROPERTY(Alure::ClipboardService* clipboard READ clipboard CONSTANT)
public:
    explicit Services(ConfigStore &config, QObject *parent = nullptr);
    Service *taskbar() { return &m_taskbar; }
    Service *workspaces() { return &m_workspaces; }
    Service *media() { return &m_media; }
    Service *tray() { return &m_tray; }
    Service *volume() { return &m_volume; }
    Service *brightness() { return &m_brightness; }
    OsdController *osd() { return &m_osd; }
    Service *updates() { return &m_updates; }
    Service *wifi() { return &m_wifi; }
    Service *bluetooth() { return &m_bluetooth; }
    NotificationService *notifications() { return &m_notifications; }
    Service *battery() { return &m_battery; }
    ClipboardService *clipboard() { return &m_clipboard; }
private:
    void apply(const QVariantMap &model);
    NiriService m_workspaces;
    TaskbarService m_taskbar;
    VolumeService m_volume;
    BrightnessService m_brightness;
    OsdController m_osd;
    CommandService m_updates{CommandService::Updates}, m_wifi{CommandService::Wifi};
    MediaService m_media;
    TrayService m_tray;
    BluetoothService m_bluetooth;
    NotificationService m_notifications;
    BatteryService m_battery;
    ClipboardService m_clipboard;
};
}
