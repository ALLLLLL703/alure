#pragma once
#include "Service.h"
#include <QLocalSocket>
#include <QJsonObject>
#include <QMap>

namespace Alure {
// Independent Niri event subscription: workspace module enablement is irrelevant.
class TaskbarService : public Service {
    Q_OBJECT
public:
    explicit TaskbarService(QObject *parent = nullptr);
    ~TaskbarService() override;
    QVariantList panelWindows() const { return m_panelWindows; }
    bool panelWindowsAvailable() const { return m_haveWindows && m_haveWorkspaces; }
signals:
    // Geometry snapshots share the stream without invalidating taskbar delegates.
    void panelWindowsChanged(const QVariantList &windows, bool available);
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    QString socketPath() const;
    void streamFailed(const QString &message);
    void consume(const QJsonObject &event);
    void publishWindows(bool layoutOnly = false);
    QLocalSocket m_stream, m_actionSocket;
    QTimer m_streamDeadline, m_actionDeadline;
    QByteArray m_streamBuffer, m_actionBuffer, m_actionPayload;
    QMap<quint64, QVariantMap> m_windows, m_workspaces;
    QVariantList m_panelWindows;
    bool m_haveWindows = false, m_haveWorkspaces = false;
};
}
