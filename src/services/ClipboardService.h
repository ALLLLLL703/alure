#pragma once
#include "Service.h"
#include <QImage>

namespace Alure {
// A lazy view of an existing cliphist database. Never starts a clipboard recorder.
class ClipboardService final : public Service {
    Q_OBJECT
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
public:
    explicit ClipboardService(QObject *parent = nullptr);
    ~ClipboardService() override;
    QVariantMap preview() const { return m_preview; }
    QImage previewImage() const { return m_image; }
    Q_INVOKABLE void openView();
    Q_INVOKABLE void closeView();
    Q_INVOKABLE void previewItem(const QString &id);
    QString databasePath() const;
signals:
    void previewChanged();
    void copied();
protected:
    void poll() override;
    void stop() override;
    bool act(const QString &name, const QVariantMap &args) override;
private:
    bool contains(const QString &id) const;
    QStringList command(const QString &operation, const QString &id = {}) const;
    bool run(const QStringList &argv, const QByteArray &input, std::function<void(QByteArray)> done);
    void drain();
    void finish(int exitCode, QProcess::ExitStatus status);
    void clearPreview();
    void renderPreview(const QString &id, const QByteArray &bytes);
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output, m_input;
    std::function<void(QByteArray)> m_done;
    QString m_error, m_pendingPreview;
    QVariantMap m_preview;
    QImage m_image;
    quint64 m_imageRevision = 0;
    bool m_open = false, m_active = false;
};
}
