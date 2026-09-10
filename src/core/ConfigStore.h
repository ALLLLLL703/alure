#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

namespace Alure {
// One shared, fully defaulted model. No UI writes through QVariantMap copies.
class ConfigStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap model READ model NOTIFY modelChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY diagnosticChanged)
    Q_PROPERTY(QString path READ path CONSTANT)
public:
    explicit ConfigStore(QString path, QObject *parent = nullptr);
    QVariantMap model() const { return m_model; }
    QString source() const { return QString::fromUtf8(m_source); }
    QString diagnostic() const { return m_diagnostic; }
    QString path() const { return m_path; }
    static QByteArray defaultSource();
    static bool parse(const QByteArray &text, QVariantMap &model, QString &error);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool saveText(const QString &text);
    void startWatching(); // panel process only; editor uses explicit reload
signals:
    void modelChanged();
    void sourceChanged();
    void diagnosticChanged();
private:
    void setDiagnostic(QString message);
    void updateWatch();
    QString m_path;
    QVariantMap m_model;
    QByteArray m_source;
    QByteArray m_diskSnapshot;
    bool m_diskExisted = false;
    bool m_hasSnapshot = false;
    bool m_watching = false;
    QString m_diagnostic;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
};
}
