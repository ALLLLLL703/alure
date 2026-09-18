#include "DmenuProtocol.h"
#include <QStringDecoder>

namespace Alure {
bool DmenuOptions::parse(const QStringList &args, DmenuOptions &o, QString &error) {
    bool mode = false;
    const bool wallpaperRequested = args.contains("wp-launcher");
    auto fail = [&](const QString &message) { error = message; return false; };
    for (int i = 0; i < args.size(); ++i) {
        const auto &arg = args[i];
        if (arg == "dmenu" || arg == "-dmenu" || arg == "wp-launcher") {
            if (mode && arg != "-dmenu") return fail("Conflicting menu modes");
            if (mode && o.wallpaper) return fail("-dmenu is not a wallpaper option");
            mode = true; o.wallpaper = arg == "wp-launcher"; continue;
        }
        if (wallpaperRequested && arg != "--config" && arg != "--preview" && arg != "--help" && arg != "-h" && arg != "--quit-after-ms")
            return fail("wp-launcher accepts only --config, --preview, --help and --quit-after-ms");
        if (arg == "--help" || arg == "-h") { o.help = true; continue; }
        if (arg == "--preview") { o.preview = true; continue; }
        if (arg == "-i") { o.caseSensitive = false; continue; }
        if (arg == "-no-custom") { o.noCustom = true; continue; }
        if (arg == "-only-match") { o.onlyMatch = true; o.noCustom = true; continue; }
        if (arg == "-show-icons" || arg == "-no-show-icons") { o.showIcons = arg == "-show-icons"; continue; }
        const QStringList valued{"--config", "--quit-after-ms", "-p", "-mesg", "-filter", "-selected-row", "-format", "-sep", "-l"};
        if (!valued.contains(arg)) return fail("Unsupported menu argument: " + arg);
        if (++i == args.size()) return fail("Missing value for " + arg);
        const auto value = args[i];
        if (arg == "--config") o.configPath = value;
        else if (arg == "-p") o.prompt = value;
        else if (arg == "-mesg") o.message = value;
        else if (arg == "-filter") o.filter = value;
        else if (arg == "-format") {
            if (value.contains('p')) return fail("Unsupported format p (markup is not supported)");
            o.format = value;
        } else if (arg == "-sep") {
            o.separator = value == "\\n" ? QByteArray("\n") : value == "\\0" ? QByteArray(1, '\0') : value.toUtf8();
            if (o.separator.size() != 1) return fail("-sep requires one byte (or \\n / \\0)");
        } else {
            bool ok = false;
            const int number = value.toInt(&ok);
            const int minimum = arg == "-selected-row" ? 0 : 1;
            const int maximum = arg == "--quit-after-ms" ? 600000 : arg == "-l" ? 1000 : 1000000;
            if (!ok || number < minimum || number > maximum) return fail("Invalid number for " + arg);
            if (arg == "-selected-row") o.selectedRow = number;
            else if (arg == "-l") o.lines = number;
            else o.quitAfterMs = number;
        }
    }
    if (!mode) return fail("Expected dmenu, -dmenu or wp-launcher");
    return true;
}
DmenuInput::DmenuInput(QByteArray separator, qsizetype maxBytes, int maxRows)
    : m_separator(std::move(separator)), m_maxBytes(maxBytes), m_maxRows(maxRows) {}
bool DmenuInput::append(const QByteArray &chunk) {
    if (!m_error.isEmpty()) return false;
    if (chunk.size() > m_maxBytes - m_bytes) { m_error = "Input exceeds max_bytes"; return false; }
    m_bytes += chunk.size(); m_pending += chunk;
    qsizetype start = 0, end;
    while ((end = m_pending.indexOf(m_separator, start)) >= 0) {
        if (!record(m_pending.mid(start, end - start))) return false;
        start = end + m_separator.size();
    }
    m_pending.remove(0, start);
    return true;
}
bool DmenuInput::finish() {
    if (!m_error.isEmpty()) return false;
    if (!m_pending.isEmpty() && !record(m_pending)) return false;
    m_pending.clear(); return true;
}
bool DmenuInput::record(const QByteArray &bytes) {
    if (m_rows.size() >= m_maxRows) { m_error = "Input exceeds max_rows"; return false; }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError()) { m_error = "Input is not valid UTF-8"; return false; }
    const auto nul = text.indexOf(QChar::Null);
    DmenuRow row;
    row.index = m_rows.size(); row.raw = nul < 0 ? text : text.left(nul); row.display = row.raw;
    if (nul >= 0) {
        const auto fields = text.mid(nul + 1).split(QChar(0x1f));
        for (int i = 0; i + 1 < fields.size(); i += 2) {
            const auto &key = fields[i], &value = fields[i + 1];
            if (key == "display") row.display = value;
            else if (key == "meta") row.meta = value;
            else if (key == "icon") row.icon = value;
            else if (key == "nonselectable") row.selectable = value != "true";
            else if (key == "permanent") row.permanent = value == "true";
            else if (key == "urgent") row.urgent = value == "true";
            else if (key == "active") row.active = value == "true";
        }
    }
    m_rows.append(std::move(row)); return true;
}
QString dmenuQuote(QString text) { text.replace('\'', "'\\''"); return '\'' + text + '\''; }
QString dmenuOutput(const QString &format, const DmenuRow *row, const QString &filter) {
    const auto raw = row ? row->raw : filter;
    QString output;
    for (const auto ch : format) {
        switch (ch.unicode()) {
        case 's': output += raw; break;
        case 'i': output += QString::number(row ? row->index : -1); break;
        case 'd': output += QString::number(row ? row->index + 1 : 0); break;
        case 'q': output += dmenuQuote(raw); break;
        case 'f': output += filter; break;
        case 'F': output += dmenuQuote(filter); break;
        default: output += ch; break;
        }
    }
    return output + '\n';
}
bool dmenuMatches(const DmenuRow &row, const QString &filter, bool caseSensitive, bool searchDisplay) {
    if (row.permanent) return true;
    const auto sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    return row.raw.contains(filter, sensitivity) || row.meta.contains(filter, sensitivity)
        || (searchDisplay && row.display.contains(filter, sensitivity));
}
}
