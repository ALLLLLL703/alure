#pragma once
#include "Service.h"
#include <QLocalSocket>
namespace Alure {
// JSON socket requests use stable workspace IDs, not per-output display indices.
class NiriService : public Service {
    Q_OBJECT
public:
    explicit NiriService(QObject *parent = nullptr);
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &, const QVariantMap &) override;
private:
    void request(const QByteArray &payload, bool action);
    QLocalSocket m_socket;
    QTimer m_deadline;
    QByteArray m_payload, m_response;
    bool m_action = false;
};
}
