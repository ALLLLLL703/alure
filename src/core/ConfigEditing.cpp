#include "ConfigStore.h"
#include <QColor>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace Alure {
QString ConfigStore::commandText(const QVariantList &argv) const {
  QStringList words;
  for (const auto &argument : argv) {
    auto word = argument.toString();
    if (word.isEmpty() ||
        word.contains(QRegularExpression("[^A-Za-z0-9_./:@%+=,-]"))) {
      word.replace("'", "'\\''");
      word = "'" + word + "'";
    }
    words << word;
  }
  return words.join(' ');
}
QVariantMap ConfigStore::commandArguments(const QString &text) const {
  QVariantList words;
  QString word;
  QChar quote;
  bool escaped = false, started = false;
  for (const QChar c : text) {
    if (c.isNull() || c == '\n' || c == '\r')
      return {{"error", "Enter a single-line command without NUL."}};
    if (escaped) {
      word += c;
      escaped = false;
      started = true;
      continue;
    }
    if (c == '\\' && quote != '\'') {
      escaped = true;
      started = true;
      continue;
    }
    if (!quote.isNull()) {
      if (c == quote)
        quote = {};
      else
        word += c;
      started = true;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      started = true;
      continue;
    }
    if (c.isSpace()) {
      if (started) {
        words << word;
        word.clear();
        started = false;
      }
    } else {
      word += c;
      started = true;
    }
  }
  if (escaped || !quote.isNull())
    return {{"error", "Finish the quoted argument or trailing escape."}};
  if (started)
    words << word;
  return {{"argv", words}};
}
namespace {
// toml++ columns count Unicode codepoints, not UTF-8 bytes or UTF-16 units.
qsizetype offset(const QString &text, toml::source_position position) {
  qsizetype at = 0;
  for (quint32 line = 1; line < position.line; ++line) {
    at = text.indexOf('\n', at);
    if (at < 0)
      throw std::runtime_error("Invalid source line");
    ++at;
  }
  for (quint32 column = 1; column < position.column && at < text.size();
       ++column) {
    if (text.at(at).isHighSurrogate() && at + 1 < text.size() &&
        text.at(at + 1).isLowSurrogate())
      ++at;
    ++at;
  }
  return at;
}
QString formatted(const toml::node &node) {
  std::ostringstream stream;
  stream << toml::toml_formatter(node);
  return QString::fromStdString(stream.str());
}
void collectTableOffsets(const QString &text, const toml::node &node,
                         QSet<qsizetype> &starts) {
  if (const auto *table = node.as_table()) {
    if (table->is_inline())
      return;
    starts.insert(offset(text, table->source().begin));
    for (const auto &[key, value] : *table) {
      Q_UNUSED(key)
      collectTableOffsets(text, value, starts);
    }
  } else if (const auto *array = node.as_array()) {
    for (const auto &value : *array)
      collectTableOffsets(text, value, starts);
  }
}
qsizetype keepTrailingComments(const QString &text, qsizetype begin,
                               qsizetype end) {
  qsizetype cursor = end;
  while (cursor > begin) {
    qsizetype previous = text.lastIndexOf('\n', cursor - 2) + 1;
    const auto line = text.mid(previous, cursor - previous).trimmed();
    if (!line.isEmpty() && !line.startsWith('#'))
      break;
    cursor = previous;
  }
  return cursor;
}
QVariantMap failed(const QString &text, const QString &error) {
  return {{"text", text}, {"error", error}};
}
QVariantMap checked(const QString &original, const QString &edited) {
  QVariantMap model;
  QString error;
  if (!ConfigStore::parse(edited.toUtf8(), model, error))
    return failed(original, error);
  return {{"text", edited}, {"error", QString()}};
}
void ensureValid(const QString &text) {
  QVariantMap model;
  QString error;
  if (!ConfigStore::parse(text.toUtf8(), model, error))
    throw std::runtime_error(error.toStdString());
}
} // namespace
bool ConfigStore::validColor(const QString &color) const {
  return QColor::isValidColorName(color);
}
QVariantMap ConfigStore::inspectText(const QString &text) const {
  QVariantMap model;
  QString error;
  parse(text.toUtf8(), model, error);
  return {{"model", model}, {"error", error}};
}
bool ConfigStore::previewText(const QString &text) {
  QVariantMap model;
  QString error;
  if (!parse(text.toUtf8(), model, error)) {
    setDiagnostic(error);
    return false;
  }
  m_model = std::move(model);
  setDiagnostic({});
  emit modelChanged();
  return true;
}
QVariantMap ConfigStore::editLiteral(const QString &text, const QString &path,
                                     const QString &literal) const {
  try {
    ensureValid(text);
    static const QRegularExpression safePath(
        "^[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+)*$");
    if (!safePath.match(path).hasMatch())
      throw std::runtime_error("Use the raw editor for quoted keys");
    const auto replacement = toml::parse(("value = " + literal).toStdString());
    if (replacement.size() != 1 || !replacement.contains("value"))
      throw std::runtime_error("Enter one TOML value");
    auto document = toml::parse(text.toStdString());
    QSet<qsizetype> tableStarts;
    collectTableOffsets(text, document, tableStarts);
    for (const auto start : tableStarts) {
      const auto header =
          text.mid(start, text.indexOf('\n', start) < 0
                              ? -1
                              : text.indexOf('\n', start) - start);
      if (header.startsWith('[') &&
          (header.contains('"') || header.contains('\'')))
        throw std::runtime_error(
            "Quoted tables: use the raw editor (no text changed)");
    }
    const auto parts = path.split('.');
    if (parts.first() == "panels" && !document.contains("panels")) {
      // The model inherits one default panel when the source has no panels key.
      // Materialize only the requested leaf; ConfigStore merges the other
      // defaults.
      if (parts.size() < 3 || parts[1] != "0")
        throw std::runtime_error(
            "Only inherited panel 0 exists; select one of its fields");
      const QString edited = text + "\n[[panels]]\n" + parts.mid(2).join('.') +
                             " = " + formatted(*replacement.get("value")) +
                             "\n";
      return checked(text, edited);
    }
    toml::node *node = &document;
    QStringList parentPath;
    int panelIndex = -1;
    struct Ancestor {
      const toml::table *table;
      QString path;
      int partIndex;
    };
    QList<Ancestor> ancestors;
    for (int i = 0; i < parts.size(); ++i) {
      if (auto *table = node->as_table()) {
        ancestors.append({table, parentPath.join('.'), i});
        auto *next = table->get(parts[i].toStdString());
        if (!next) {
          if (table->is_inline())
            throw std::runtime_error("New inline table members: use the raw "
                                     "editor (no text changed)");
          // An implicit table created by dotted keys has no header. Insert a
          // dotted leaf under the nearest explicit ancestor, never rewrite that
          // table.
          QString edited = text;
          bool inserted = false;
          for (auto ancestor = ancestors.crbegin();
               ancestor != ancestors.crend(); ++ancestor) {
            if (ancestor->table->is_inline())
              throw std::runtime_error("New inline table members: use the raw "
                                       "editor (no text changed)");
            const QString suffix = parts.mid(ancestor->partIndex).join('.');
            if (ancestor->path.isEmpty()) {
              edited.prepend(suffix + " = " +
                             formatted(*replacement.get("value")) + "\n");
              inserted = true;
              break;
            }
            const bool panelRoot =
                ancestor->path == "panels" && panelIndex >= 0;
            const QString pattern =
                panelRoot
                    ? "^\\[\\[panels\\]\\][ \\t]*(?:#[^\\n]*)?\\r?$"
                    : "^\\[" + QRegularExpression::escape(ancestor->path) +
                          "\\][ \\t]*(?:#[^\\n]*)?\\r?$";
            QRegularExpression regex(pattern,
                                     QRegularExpression::MultilineOption);
            auto matches = regex.globalMatch(text);
            QRegularExpressionMatch match;
            const auto tableStart =
                offset(text, ancestor->table->source().begin);
            while (matches.hasNext()) {
              auto m = matches.next();
              if (m.capturedStart() == tableStart) {
                match = m;
                break;
              }
            }
            if (!match.hasMatch())
              continue;
            edited.insert(match.capturedEnd(),
                          "\n" + suffix + " = " +
                              formatted(*replacement.get("value")));
            inserted = true;
            break;
          }
          if (!inserted)
            throw std::runtime_error("No unambiguous table header: use the raw "
                                     "editor (no text changed)");
          // Dotted insertion may conflict with explicit descendants; checked()
          // rejects it without mutation.
          return checked(text, edited);
        }
        node = next;
        parentPath.append(parts[i]);
      } else if (auto *array = node->as_array()) {
        bool ok;
        const int index = parts[i].toInt(&ok);
        if (!ok || index < 0 || index >= static_cast<int>(array->size()))
          throw std::runtime_error(
              "Array index no longer exists; inspect current draft");
        node = array->get(index);
        panelIndex = index;
      } else
        throw std::runtime_error("Not a table; use the raw editor");
    }
    if (node->is_table())
      throw std::runtime_error("Edit individual fields or use the raw editor");
    const auto begin = offset(text, node->source().begin),
               end = offset(text, node->source().end);
    QString edited = text;
    edited.replace(begin, end - begin, formatted(*replacement.get("value")));
    return checked(text, edited);
  } catch (const std::exception &e) {
    return failed(text, QString::fromUtf8(e.what()));
  }
}
QVariantMap ConfigStore::editPanels(const QString &text,
                                    const QString &operation, int index) const {
  try {
    ensureValid(text);
    auto document = toml::parse(text.toStdString());
    const auto defaults = toml::parse(defaultSource().toStdString());
    toml::array panels = document["panels"].is_array()
                             ? *document["panels"].as_array()
                             : *defaults["panels"].as_array();
    if (operation == "add") {
      auto panel = *defaults["panels"][0].as_table();
      int suffix = 1;
      std::string id;
      do {
        id = "panel-" + std::to_string(suffix++);
      } while (std::any_of(panels.begin(), panels.end(), [&id](const auto &p) {
        return (*p.as_table())["id"].template value_or<std::string>("") == id;
      }));
      panel.insert_or_assign("id", id);
      panels.push_back(panel);
    } else {
      if (index < 0 || index >= static_cast<int>(panels.size()))
        throw std::runtime_error("Select an existing panel");
      if (operation == "remove")
        panels.erase(panels.cbegin() + index);
      else if (operation == "up" || operation == "down") {
        const int target = index + (operation == "up" ? -1 : 1);
        if (target < 0 || target >= static_cast<int>(panels.size()))
          throw std::runtime_error("Panel is already at the boundary");
        const auto moved = *panels[index].as_table();
        panels.erase(panels.cbegin() + index);
        panels.insert(panels.cbegin() + target, moved);
      } else
        throw std::runtime_error("Unsupported panel operation");
    }
    QString edited = text;
    if (auto *old = document["panels"].as_array()) {
      // Restrict table-array editing to unquoted explicit headers; preserve
      // other sections byte-for-byte.
      QRegularExpression headers(
          "^[ \t]*\\[([^\\n]+)\\][ \\t]*(?:#[^\\n]*)?\\r?$",
          QRegularExpression::MultilineOption);
      QSet<qsizetype> tableStarts;
      collectTableOffsets(text, document, tableStarts);
      auto matches = headers.globalMatch(text);
      QList<QPair<qsizetype, qsizetype>> regions;
      qsizetype begin = -1;
      while (matches.hasNext()) {
        const auto match = matches.next();
        if (!tableStarts.contains(match.capturedStart(1) - 1))
          continue;
        const auto header = match.captured(1);
        const bool ours = header == "[panels]" ||
                          header.startsWith("panels.") ||
                          header.startsWith("[panels.");
        if (header.contains('"') || header.contains('\''))
          throw std::runtime_error("Quoted table headers: use the raw editor");
        if (begin >= 0 && !ours) {
          regions.append({begin, keepTrailingComments(text, begin,
                                                      match.capturedStart())});
          begin = -1;
        }
        if (ours && begin < 0)
          begin = match.capturedStart();
      }
      if (begin >= 0)
        regions.append({begin, keepTrailingComments(text, begin, text.size())});
      if (regions.isEmpty()) {
        for (const auto &entry : *old)
          if (entry.is_table() && !entry.as_table()->is_inline())
            throw std::runtime_error(
                "Unsupported panel headers: use the raw editor");
        // Inline panels arrays, including [], are replaced as a value; unknown
        // table keys survive.
        const auto a = offset(text, old->source().begin),
                   b = offset(text, old->source().end);
        for (auto &p : panels)
          p.as_table()->is_inline(true);
        edited.replace(a, b - a, formatted(panels));
        return checked(text, edited);
      }
      for (auto it = regions.crbegin(); it != regions.crend(); ++it)
        edited.remove(it->first, it->second - it->first);
    }
    toml::table wrapper;
    wrapper.insert("panels", panels);
    // Empty arrays are root assignments, not members of the preceding table.
    if (panels.empty())
      edited.prepend("panels = []\n");
    else
      edited += "\n" + formatted(wrapper) + "\n";
    return checked(text, edited);
  } catch (const std::exception &e) {
    return failed(text, QString::fromUtf8(e.what()));
  }
}
} // namespace Alure
