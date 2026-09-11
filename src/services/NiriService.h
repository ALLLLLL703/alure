#pragma once
#include "Service.h"
#include <QLocalSocket>
#include <QJsonObject>
namespace Alure {
// Standalone workspace subscription; actions need a separate Niri connection.
class NiriService : public Service {
    Q_OBJECT
public:
    explicit NiriService(QObject *parent = nullptr);
    ~NiriService() override;
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    QString socketPath() const;
    void streamFailed(const QString &message);
    void actionFinished(const QString &error = {});
    bool consume(const QJsonObject &event);
    void publishWorkspaces();
    QLocalSocket m_stream, m_actionSocket;
    QTimer m_streamDeadline, m_actionDeadline;
    QByteArray m_streamBuffer, m_actionBuffer, m_actionPayload;
    QVariantList m_workspaces;
    QString m_actionError;
    bool m_subscribed = false, m_haveWorkspaces = false, m_actionPending = false;
};
}
