#pragma once
#include <QObject>
#include <QVariant>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QTimer>
#include <functional>

namespace Alure {
struct MenuLayout {
    int id = 0;
    QVariantMap properties;
    QVariantList children;
};
QDBusArgument &operator<<(QDBusArgument &, const MenuLayout &);
const QDBusArgument &operator>>(const QDBusArgument &, MenuLayout &);

// A single open DBusMenu. No provider-owned ContextMenu windows are requested.
class TrayMenu : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
public:
    explicit TrayMenu(QObject *parent = nullptr);
    void open(const QString &destination, const QString &path, int timeout);
    void clear(const QString &error = {});
    void setAllowActions(bool allowed) { m_allowActions = allowed; }
    QVariantList items() const { return m_items; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool canGoBack() const { return m_parents.size() > 1; }
    Q_INVOKABLE bool select(int id);
    Q_INVOKABLE void back();
    Q_INVOKABLE void close() { clear(); }
signals:
    void changed();
    void activated();
private slots:
    void menuUpdated(const QDBusMessage &);
    void ownerChanged(const QString &, const QString &, const QString &);
private:
    void load(bool aboutToShow);
    void request(const QString &, const QVariantList &, std::function<void(const QVariantList &)>);
    QString m_destination, m_path, m_error;
    QVariantList m_items;
    QList<int> m_parents;
    QTimer m_reload;
    int m_timeout = 3000;
    quint64 m_generation = 0;
    bool m_allowActions = true;
    bool m_loading = false, m_refreshPending = false;
};
}
Q_DECLARE_METATYPE(Alure::MenuLayout)
