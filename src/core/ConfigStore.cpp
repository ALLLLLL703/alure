#include "ConfigStore.h"
#include <QColor>
#include <QKeySequence>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <toml++/toml.hpp>
#include <cmath>
#include <stdexcept>
#include <sstream>

static void initDefaults() {
    static const bool initialized = [] { Q_INIT_RESOURCE(defaults); return true; }();
    Q_UNUSED(initialized)
}
namespace Alure {
namespace {
constexpr qint64 maxConfigSize = 1024 * 1024;
QByteArray resource(const char *name) {
    QFile file(QString::fromUtf8(name));
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open built-in defaults");
    return file.readAll();
}
QVariant convert(const toml::node &node) {
    if (const auto *table = node.as_table()) {
        QVariantMap result;
        for (const auto &[key, value] : *table) result.insert(QString::fromUtf8(key.str().data(), key.str().size()), convert(value));
        return result;
    }
    if (const auto *array = node.as_array()) {
        QVariantList result;
        for (const auto &value : *array) result.push_back(convert(value));
        return result;
    }
    if (auto v = node.value<std::string>(); node.is_string()) return QString::fromStdString(*v);
    if (auto v = node.value<int64_t>(); node.is_integer()) return QVariant::fromValue<qlonglong>(*v);
    if (auto v = node.value<double>(); node.is_floating_point()) return *v;
    if (auto v = node.value<bool>(); node.is_boolean()) return *v;
    // Unknown datetime values need not be interpreted; the original TOML remains intact.
    return QVariant();
}
[[noreturn]] void invalid(const QString &key, const QString &why) {
    throw std::runtime_error((key + ": " + why).toStdString());
}
QVariantMap table(const QVariant &v, const QString &key) {
    if (v.metaType().id() != QMetaType::QVariantMap) invalid(key, "expected table");
    return v.toMap();
}
QVariantMap merge(QVariantMap base, const QVariantMap &user, const QString &path = {}) {
    for (auto it = user.begin(); it != user.end(); ++it) {
        const QString key = path + it.key();
        if (!base.contains(it.key())) { base.insert(it.key(), it.value()); continue; }
        const auto expected = base.value(it.key()).metaType().id();
        const auto actual = it.value().metaType().id();
        if (expected == QMetaType::QVariantMap) {
            base[it.key()] = merge(base.value(it.key()).toMap(), table(it.value(), key), key + '.');
        } else {
            if (expected != actual && !(expected == QMetaType::Double && actual == QMetaType::LongLong))
                invalid(key, "wrong type");
            base[it.key()] = it.value();
        }
    }
    return base;
}
void range(const QVariantMap &m, const QString &key, double low, double high, const QString &path) {
    const double value = m.value(key).toDouble();
    if (!std::isfinite(value) || value < low || value > high)
        invalid(path + key, QString("must be between %1 and %2").arg(low).arg(high));
}
void choice(const QVariantMap &m, const QString &key, const QStringList &values, const QString &path) {
    if (!values.contains(m.value(key).toString())) invalid(path + key, "expected one of " + values.join(", "));
}
void nonempty(const QVariantMap &m, const QString &key, const QString &path) {
    if (m.value(key).toString().trimmed().isEmpty()) invalid(path + key, "must not be empty");
}
bool readDisk(const QString &path, QByteArray &data, bool &exists, QString &error) {
    const QFileInfo info(path);
    exists = info.exists();
    if (!exists) { data.clear(); return true; }
    if (!info.isFile()) { error = "Configuration path is not a regular file"; return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    data = file.read(maxConfigSize + 1);
    if (data.size() > maxConfigSize) { error = "Configuration exceeds 1 MiB"; return false; }
    if (file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
    return true;
}
}
QByteArray ConfigStore::defaultSource() { initDefaults(); return resource(":/config/default.toml"); }
bool ConfigStore::parse(const QByteArray &text, QVariantMap &model, QString &error) {
    try {
        initDefaults();
        if (text.size() > maxConfigSize) invalid("config", "exceeds 1 MiB");
        const auto defaults = convert(toml::parse(defaultSource().toStdString())).toMap();
        const auto user = convert(toml::parse(text.toStdString())).toMap();
        auto result = merge(defaults, user);
        range(result, "version", 1, 1, "");
        const auto runtime = result.value("runtime").toMap();
        range(runtime, "reload_delay_ms", 50, 10000, "runtime.");
        auto theme = result.value("theme").toMap();
        const auto themes = convert(toml::parse(resource(":/config/themes.toml").toStdString())).toMap();
        choice(theme, "name", themes.keys(), "theme.");
        auto palette = themes.value(theme.value("name").toString()).toMap();
        if (theme.contains("palette")) palette = merge(palette, table(theme.value("palette"), "theme.palette"), "theme.palette.");
        for (auto it = palette.begin(); it != palette.end(); ++it)
            if (!QColor::isValidColorName(it.value().toString())) invalid("theme.palette." + it.key(), "invalid Qt color");
        theme["palette"] = palette;
        nonempty(theme, "font", "theme.");
        range(theme, "font_size", 6, 72, "theme.");
        range(theme, "spacing", 0, 128, "theme.");
        range(theme, "padding", 0, 128, "theme.");
        range(theme, "radius", 0, 128, "theme.");
        range(theme, "border_width", 0, 16, "theme.");
        range(theme, "opacity", 0, 1, "theme.");
        range(theme, "icon_size", 8, 128, "theme.");
        choice(theme, "icon_mode", {"builtin", "theme"}, "theme.");
        result["theme"] = theme;
        const auto ui = result.value("ui").toMap();
        range(ui, "panel_padding", 0, 128, "ui.");
        range(ui, "module_height", 16, 256, "ui.");
        range(ui, "popup_width", 240, 1920, "ui.");
        range(ui, "popup_height", 240, 2160, "ui.");
        range(ui, "popup_gap", 0, 256, "ui.");
        choice(ui, "popup_alignment", {"center", "start", "end"}, "ui.");
        choice(ui, "popup_direction", {"inward", "top", "bottom", "left", "right"}, "ui.");
        range(ui, "animation_ms", 0, 2000, "ui.");
        const auto toast = ui.value("toast").toMap();
        range(toast, "width", 240, 1920, "ui.toast.");
        range(toast, "height", 80, 1080, "ui.toast.");
        range(toast, "margin", 0, 4096, "ui.toast.");
        range(toast, "duration_ms", 100, 600000, "ui.toast.");
        choice(toast, "edge", {"top", "bottom"}, "ui.toast.");
        nonempty(toast, "output", "ui.toast.");
        auto settings = result.value("settings").toMap();
        choice(settings, "controls_style", {"Basic", "Fusion"}, "settings.");
        range(settings, "width", 400, 7680, "settings.");
        range(settings, "height", 300, 4320, "settings.");
        range(settings, "editor_font_size", 6, 72, "settings.");
        nonempty(settings, "editor_font", "settings.");
        const auto shortcut = settings.value("close_shortcut").toString();
        const auto sequence = QKeySequence::fromString(shortcut, QKeySequence::PortableText);
        if (!shortcut.isEmpty() && (sequence.isEmpty() || sequence.toString(QKeySequence::PortableText) != shortcut))
            invalid("settings.close_shortcut", "expected a canonical Qt portable shortcut, e.g. Ctrl+W, or empty to disable");
        const auto foundation = result.value("foundation").toMap();
        static const QRegularExpression iconName(QStringLiteral("^[A-Za-z0-9_.-]+$"));
        if (!iconName.match(foundation.value("icon").toString()).hasMatch())
            invalid("foundation.icon", "expected a nonempty icon name (letters, digits, _, . or -), not a path");
        if (!iconName.match(ui.value("settings_icon").toString()).hasMatch()) invalid("ui.settings_icon", "expected icon name");
        auto modules = result.value("modules").toMap();
        auto moduleDefaults = defaults.value("modules").toMap().value("media").toMap();
        auto behaviorDefaults = moduleDefaults.value("behavior").toMap();
        behaviorDefaults["format"] = QString();
        moduleDefaults["behavior"] = behaviorDefaults;
        for (auto it = modules.begin(); it != modules.end(); ++it) {
            auto module = merge(moduleDefaults, table(it.value(), "modules." + it.key()), "modules." + it.key() + '.');
            const auto style = module.value("style").toMap();
            const QString stylePath = "modules." + it.key() + ".style.";
            range(style, "icon_size", 8, 128, stylePath);
            range(style, "min_width", 16, 1024, stylePath);
            range(style, "max_width", 16, 2048, stylePath);
            if (style.value("min_width").toInt() > style.value("max_width").toInt()) invalid(stylePath + "min_width", "exceeds max_width");
            if (!iconName.match(style.value("icon").toString()).hasMatch()) invalid(stylePath + "icon", "expected icon name, not a path");
            for (const auto &key : {"foreground", "background"})
                if (!style.value(key).toString().isEmpty() && !QColor::isValidColorName(style.value(key).toString())) invalid(stylePath + key, "invalid Qt color");
            auto behavior = module.value("behavior").toMap();
            const QString path = "modules." + it.key() + ".behavior.";
            range(behavior, "interval_ms", 100, 86400000, path);
            range(behavior, "timeout_ms", 100, 600000, path);
            const auto knownBehavior = defaults.value("modules").toMap().value(it.key()).toMap().value("behavior").toMap();
            for (auto option = behavior.begin(); option != behavior.end(); ++option) {
                if (option.key() != "command" && !(option.key().endsWith("_command") && knownBehavior.contains(option.key()))) continue;
                if (option.value().metaType().id() != QMetaType::QVariantList) invalid(path + option.key(), "expected argv array");
                const auto argv = option.value().toList();
                if (argv.size() > 128) invalid(path + option.key(), "maximum 128 arguments");
                for (const auto &arg : argv)
                    if (arg.metaType().id() != QMetaType::QString || arg.toString().contains(QChar::Null)) invalid(path + option.key(), "expected strings without NUL");
                if (!argv.isEmpty() && argv.first().toString().trimmed().isEmpty()) invalid(path + option.key(), "executable must not be empty");
            }
            if (it.key() == "calendar") range(behavior, "first_day_of_week", 0, 1, path);
            if (it.key() == "tray") {
                range(behavior, "menu_width", 160, 1920, path);
                range(behavior, "menu_height", 100, 2160, path);
            }
            if (it.key() == "volume") {
                range(behavior, "max_percent", 1, 150, path);
                range(behavior, "debounce_ms", 10, 2000, path);
            }
            if (it.key() == "notifications") {
                range(behavior, "history_limit", 1, 1000, path);
                range(behavior, "default_expire_ms", 100, 86400000, path);
                range(behavior, "max_expire_ms", 100, 86400000, path);
                if (behavior.value("default_expire_ms").toInt() > behavior.value("max_expire_ms").toInt()) invalid(path + "default_expire_ms", "exceeds max_expire_ms");
            }
            if (it.key() == "workspaces") {
                choice(behavior, "ordering", {"output-index", "provider"}, path);
                const auto socket = behavior.value("socket_path").toString();
                if (socket.contains(QChar::Null) || (!socket.isEmpty() && !QDir::isAbsolutePath(socket))) invalid(path + "socket_path", "expected empty or absolute socket path without NUL");
            }
            if (it.key() == "battery") {
                nonempty(behavior, "sysfs_path", path);
                if (behavior.value("sysfs_path").toString().contains(QChar::Null) || !QDir::isAbsolutePath(behavior.value("sysfs_path").toString())) invalid(path + "sysfs_path", "expected absolute directory path");
            }
            it.value() = module;
        }
        result["modules"] = modules;
        QVariantList panels;
        const auto panelDefaults = defaults.value("panels").toList().first().toMap();
        QSet<QString> ids;
        const auto configuredPanels = result.value("panels").toList();
        if (configuredPanels.size() > 128) invalid("panels", "maximum 128 definitions");
        for (const auto &entry : configuredPanels) {
            auto panel = merge(panelDefaults, table(entry, "panels[]"), "panels[].");
            const QString id = panel.value("id").toString();
            nonempty(panel, "id", "panels[].");
            if (ids.contains(id)) invalid("panels.id", "duplicate " + id);
            ids.insert(id);
            nonempty(panel, "output", "panels[].");
            choice(panel, "edge", {"top", "bottom", "left", "right"}, "panels[].");
            choice(panel, "layer", {"background", "bottom", "top", "overlay"}, "panels[].");
            range(panel, "thickness", 16, 512, "panels[].");
            range(panel, "length", 0, 32768, "panels[].");
            range(panel, "exclusive_zone", -1, 32768, "panels[].");
            const auto margins = panel.value("margins").toMap();
            for (const auto &key : {"top", "right", "bottom", "left"}) range(margins, key, 0, 4096, "panels[].margins.");
            QSet<QString> used;
            for (const auto &name : panel.value("modules").toList()) {
                if (name.metaType().id() != QMetaType::QString || !modules.contains(name.toString())) invalid("panels[].modules", "unknown module " + name.toString());
                if (used.contains(name.toString())) invalid("panels[].modules", "duplicate module " + name.toString());
                used.insert(name.toString());
            }
            panels.append(panel);
        }
        result["panels"] = panels;
        model = result;
        error.clear();
        return true;
    } catch (const toml::parse_error &e) {
        std::ostringstream out; out << e; error = QString::fromStdString(out.str());
    } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
    return false;
}
ConfigStore::ConfigStore(QString path, QObject *parent) : QObject(parent), m_path(QFileInfo(path).absoluteFilePath()) {
    QString error;
    parse(defaultSource(), m_model, error);
    m_source = defaultSource();
    m_reloadTimer.setSingleShot(true);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ConfigStore::reload);
    const auto schedule = [this] {
        updateWatch();
        const auto runtime = m_model.value("runtime").toMap();
        if (runtime.value("watch").toBool()) m_reloadTimer.start(runtime.value("reload_delay_ms").toInt());
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, schedule);
}
void ConfigStore::setDiagnostic(QString message) {
    if (m_diagnostic == message) return;
    m_diagnostic = std::move(message); emit diagnosticChanged();
}
bool ConfigStore::reload() {
    QByteArray bytes; bool existed; QString error;
    if (!readDisk(m_path, bytes, existed, error)) { setDiagnostic(error); return false; }
    QVariantMap model;
    // Keep the raw editor document/snapshot even if parsing fails; only the
    // published runtime model retains its last good value.
    const auto source = existed ? bytes : defaultSource();
    const bool sourceChangedValue = m_source != source;
    m_source = source;
    m_diskSnapshot = bytes; m_diskExisted = existed; m_hasSnapshot = true;
    if (!parse(m_source, model, error)) {
        setDiagnostic(error); if (sourceChangedValue) emit sourceChanged(); updateWatch(); return false;
    }
    const bool changed = m_model != model;
    m_model = std::move(model);
    setDiagnostic({});
    if (sourceChangedValue) emit sourceChanged();
    // Directory watches also fire for unrelated sibling files. Do not rebuild
    // panels (and dismiss details) unless the effective configuration changed.
    if (changed) emit modelChanged();
    updateWatch();
    return true;
}
bool ConfigStore::saveText(const QString &text) {
    const auto bytes = text.toUtf8();
    QVariantMap model; QString error;
    if (!parse(bytes, model, error)) { setDiagnostic(error); return false; }
    if (!m_hasSnapshot) { setDiagnostic("Reload the configuration before saving"); return false; }
    // Settings does not auto-reload: this snapshot is the editor's explicit read.
    const auto directory = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(directory)) { setDiagnostic("Cannot create configuration directory"); return false; }
    QLockFile lock(m_path + ".lock");
    if (!lock.tryLock(0)) { setDiagnostic("Configuration is locked by another writer"); return false; }
    QByteArray current; bool existed;
    if (!readDisk(m_path, current, existed, error)) { setDiagnostic(error); return false; }
    if (existed != m_diskExisted || current != m_diskSnapshot) {
        setDiagnostic("Configuration changed externally; copy your edits, then Reload before saving"); return false;
    }
    // Do not replace a symlink with a regular file or follow it into another config.
    if (QFileInfo(m_path).isSymLink()) { setDiagnostic("Saving through a symlink is not supported"); return false; }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        setDiagnostic(file.errorString()); return false;
    }
    if (!readDisk(m_path, current, existed, error)) { setDiagnostic(error); return false; }
    if (existed != m_diskExisted || current != m_diskSnapshot || QFileInfo(m_path).isSymLink()) {
        setDiagnostic("Configuration changed while preparing save; Reload before saving"); return false;
    }
    if (!file.commit()) { setDiagnostic(file.errorString()); return false; }
    m_model = std::move(model); m_source = bytes; m_diskSnapshot = bytes; m_diskExisted = true;
    setDiagnostic({}); emit sourceChanged(); emit modelChanged(); updateWatch();
    return true;
}
void ConfigStore::startWatching() { m_watching = true; updateWatch(); }
void ConfigStore::updateWatch() {
    if (!m_watching) return;
    const auto oldFiles = m_watcher.files(); const auto oldDirs = m_watcher.directories();
    if (!oldFiles.isEmpty()) m_watcher.removePaths(oldFiles);
    if (!oldDirs.isEmpty()) m_watcher.removePaths(oldDirs);
    QString directory = QFileInfo(m_path).absolutePath();
    while (!QFileInfo::exists(directory)) {
        const auto parent = QFileInfo(directory).absolutePath();
        if (parent == directory) break;
        directory = parent;
    }
    m_watcher.addPath(directory);
    if (QFileInfo::exists(m_path)) m_watcher.addPath(m_path);
}
}
