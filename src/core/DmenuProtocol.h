#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace Alure {
struct DmenuOptions {
    bool wallpaper = false, preview = false, help = false;
    bool noCustom = false, onlyMatch = false;
    std::optional<bool> caseSensitive, showIcons;
    QString configPath, prompt = "dmenu", message, filter, format = "s";
    QByteArray separator = "\n";
    int selectedRow = 0, lines = 0, quitAfterMs = 0;
    static bool parse(const QStringList &arguments, DmenuOptions &options, QString &error);
};
struct DmenuRow {
    QString raw, display, meta, icon;
    int index = 0;
    bool selectable = true, permanent = false, urgent = false, active = false;
};
// Incremental byte framing; decode only complete UTF-8 records. No shell parsing.
class DmenuInput {
public:
    DmenuInput(QByteArray separator = "\n", qsizetype maxBytes = 8388608, int maxRows = 10000);
    bool append(const QByteArray &chunk);
    bool finish();
    const QList<DmenuRow> &rows() const { return m_rows; }
    QString error() const { return m_error; }
private:
    bool record(const QByteArray &bytes);
    QByteArray m_separator, m_pending;
    qsizetype m_maxBytes, m_bytes = 0;
    int m_maxRows;
    QList<DmenuRow> m_rows;
    QString m_error;
};
QString dmenuQuote(QString text);
QString dmenuOutput(const QString &format, const DmenuRow *row, const QString &filter);
bool dmenuMatches(const DmenuRow &row, const QString &filter, bool caseSensitive, bool searchDisplay);
}
