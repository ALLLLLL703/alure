// Explicit synthetic tray provider for nested-desktop interaction tests only.
#include "TrayMenuFixture.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusMetaType>
#include <QTimer>

class DemoTrayItem : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString IconName READ icon)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
public:
    QString title() const { return "Alure test tray (synthetic)"; }
    QString icon() const { return "alure"; }
    QString status() const { return "Active"; }
    QDBusObjectPath menu() const { return QDBusObjectPath("/Menu"); }
public slots:
    void Activate(int, int) { }
    void SecondaryActivate(int, int) { }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); qDBusRegisterMetaType<Alure::MenuLayout>();
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("org.alure.TrayDemo")) return 1;
    DemoTrayItem item; FakeTrayMenu menu;
    if (!bus.registerObject("/StatusNotifierItem", &item, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots) ||
        !bus.registerObject("/Menu", &menu, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) return 1;
    const auto message = QDBusMessage::createMethodCall("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
    auto registration = message; registration.setArguments({"org.alure.TrayDemo"}); bus.asyncCall(registration);
    QTimer::singleShot(600000, &app, &QCoreApplication::quit);
    return app.exec();
}
#include "TrayMenuDemo.moc"
