#include "ClipboardService.h"
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringDecoder>
#include <utility>

namespace Alure {
ClipboardService::ClipboardService(QObject *parent) : Service(parent) {
    m_deadline.setSingleShot(true);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::started, this, [this] {
        m_process.write(m_input); m_input.clear(); m_process.closeWriteChannel();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ClipboardService::drain);
    // Never put clipboard/tool output into logs or diagnostics.
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &ClipboardService::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { m_error = "Clipboard executable could not be started; check cliphist/copy_command."; finish(-1, QProcess::CrashExit); }
    });
    connect(&m_deadline, &QTimer::timeout, this, [this] { m_error = "Clipboard command timed out."; m_process.kill(); });
}
ClipboardService::~ClipboardService() { stop(); }
QString ClipboardService::databasePath() const {
    auto path = m_options.value("database_path").toString();
    if (path.isEmpty()) path = qEnvironmentVariable("CLIPHIST_DB_PATH");
    return path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/cliphist/db" : path;
}
QStringList ClipboardService::command(const QString &operation, const QString &id) const {
    QStringList argv;
    for (const auto &value : m_options.value("cliphist_command").toList()) argv << value.toString();
    argv << "-db-path" << databasePath() << operation;
    if (!id.isEmpty()) argv << id;
    return argv;
}
bool ClipboardService::contains(const QString &id) const {
    for (const auto &entry : items()) if (entry.toMap().value("id").toString() == id) return true;
    return false;
}
void ClipboardService::openView() { if (!enabled()) return; m_open = true; refresh(); }
void ClipboardService::closeView() { stop(); fail("Open clipboard history to load entries."); }
void ClipboardService::clearPreview() { m_preview.clear(); m_image = {}; emit previewChanged(); }
void ClipboardService::stop() {
    m_open = false; m_active = false; m_done = {}; m_pendingPreview.clear(); m_deadline.stop();
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    m_output.clear(); m_input.clear(); m_images.clear(); m_previews.clear(); clearPreview(); setBusy(false);
}
bool ClipboardService::run(const QStringList &argv, const QByteArray &input, std::function<void(QByteArray)> done) {
    if (argv.isEmpty() || argv.first().isEmpty() || m_process.state() != QProcess::NotRunning) return false;
    m_active = true; m_done = std::move(done); m_input = input; m_output.clear(); m_error.clear();
    setBusy(true); m_deadline.start(timeout());
    auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("LC_ALL", "C");
    m_process.setProcessEnvironment(environment);
    m_process.start(argv.first(), argv.mid(1));
    return true;
}
void ClipboardService::drain() {
    if (!m_active) { m_process.readAllStandardOutput(); return; }
    const auto remaining = m_options.value("max_bytes").toLongLong() - m_output.size();
    m_output += m_process.read(remaining + 1);
    if (m_output.size() > m_options.value("max_bytes").toLongLong()) {
        m_error = "Clipboard data exceeds max_bytes; no content was copied."; m_process.kill();
    }
}
void ClipboardService::finish(int exitCode, QProcess::ExitStatus status) {
    if (!m_active) return;
    drain(); m_deadline.stop(); m_active = false;
    auto done = std::move(m_done); auto output = std::move(m_output);
    if (!m_error.isEmpty() || exitCode != 0 || status != QProcess::NormalExit) {
        clearPreview(); m_pendingPreview.clear();
        fail(m_error.isEmpty() ? "Clipboard command failed; the database may be busy, or the entry may have disappeared." : m_error);
    } else if (done) done(std::move(output));
    if (!m_active) {
        setBusy(false);
        while (m_open && !m_active && !m_pendingPreview.isEmpty()) { const auto id = m_pendingPreview.takeFirst(); previewItem(id); }
    }
}
void ClipboardService::poll() {
    if (!m_open || m_process.state() != QProcess::NotRunning) return;
    // cliphist's own read-only initialization can create directories: avoid it on a missing DB.
    if (!QFileInfo::exists(databasePath())) { clearPreview(); fail("No cliphist database. Configure your existing recorder/database_path first."); return; }
    run(command("list"), {}, [this](const QByteArray &output) {
        QVariantList rows;
        static const QRegularExpression idPattern("^[0-9]{1,20}$");
        for (const auto &line : output.split('\n')) {
            if (line.isEmpty()) continue;
            const auto tab = line.indexOf('\t');
            const auto id = QString::fromLatin1(line.first(qMax<qsizetype>(0, tab)));
            if (tab < 1 || !idPattern.match(id).hasMatch()) { fail("Unrecognized cliphist list format (expected ID<TAB>preview)."); return; }
            rows << QVariantMap{{"id", id}, {"label", QString::fromUtf8(line.sliced(tab + 1)).left(1024)}};
            if (rows.size() >= m_options.value("max_items").toInt()) break;
        }
        const auto selected = m_preview.value("id").toString();
        const auto rowFor = [&selected](const QVariantList &values) {
            for (const auto &entry : values) if (entry.toMap().value("id").toString() == selected) return entry;
            return QVariant{};
        };
        if (items() != rows) { m_previews.clear(); m_images.clear(); m_pendingPreview.clear(); clearPreview(); }
        else if (rowFor(items()) != rowFor(rows)) clearPreview();
        publish({{"count", rows.size()}}, rows);
    });
}
void ClipboardService::previewItem(const QString &id) {
    if (!m_open || !contains(id)) return;
    const auto cached = m_previews.value(id).toMap();
    if (!cached.isEmpty() && (cached.value("kind") != "image" || m_images.contains(id))) {
        if (m_preview != cached) { m_preview = cached; m_image = previewImage(id); emit previewChanged(); }
        return;
    }
    if (busy()) { if (!m_pendingPreview.contains(id)) m_pendingPreview.append(id); return; }
    clearPreview();
    run(command("decode", id), {}, [this, id](const QByteArray &bytes) { renderPreview(id, bytes); });
}
void ClipboardService::renderPreview(const QString &id, const QByteArray &bytes) {
    QVariantMap preview{{"id", id}, {"bytes", bytes.size()}, {"kind", "binary"}, {"text", "Binary content; copying restores the original bytes."}};
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer); reader.setDecideFormatFromContent(true);
    const auto format = reader.format();
    if (QList<QByteArray>{"png", "jpeg", "gif", "bmp", "tiff", "webp"}.contains(format)) {
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > m_options.value("max_image_pixels").toLongLong())
            preview["text"] = "Image preview exceeds max_image_pixels or has invalid dimensions.";
        else {
            const int bound = m_options.value("preview_image_size").toInt();
            if (size.width() > bound || size.height() > bound) reader.setScaledSize(size.scaled(QSize(bound, bound), Qt::KeepAspectRatio));
            m_image = reader.read();
            if (!m_image.isNull()) {
                preview["kind"] = "image"; preview["width"] = size.width(); preview["height"] = size.height();
                m_images.setMaxCost(m_options.value("preview_cache_items").toInt());
                m_images.insert(id, new QImage(m_image)); // QCache owns this bounded thumbnail.
                preview["imageUrl"] = QString("image://clipboard/%1/%2").arg(id).arg(++m_imageRevision);
            } else preview["text"] = "Image data could not be decoded.";
        }
    } else {
        QStringDecoder decoder(QStringDecoder::Utf8); const QString text = decoder(bytes);
        if (!decoder.hasError() && !bytes.contains('\0')) {
            const int limit = m_options.value("preview_text_chars").toInt();
            preview["kind"] = "text"; preview["text"] = text.left(limit); preview["truncated"] = text.size() > limit;
        }
    }
    m_preview = preview; m_previews[id] = preview; emit previewChanged();
}
bool ClipboardService::act(const QString &name, const QVariantMap &args) {
    if (!m_open) return false;
    const auto id = args.value("id").toString();
    const bool confirmed = args.value("confirmed").metaType().id() == QMetaType::Bool && args.value("confirmed").toBool();
    if (name == "wipe") {
        if (!m_options.value("allow_delete").toBool() || !confirmed) return false;
        return run(command("wipe"), {}, [this](const QByteArray &) { clearPreview(); poll(); });
    }
    if (!contains(id)) return false;
    if (name == "delete") {
        if (!m_options.value("allow_delete").toBool() || (m_options.value("confirm_delete").toBool() && !confirmed)) return false;
        return run(command("delete"), id.toLatin1() + '\n', [this](const QByteArray &) { clearPreview(); poll(); });
    }
    if (name != "copy") return false;
    // Always decode fresh: a concurrently removed/replaced entry must not copy stale cached data.
    return run(command("decode", id), {}, [this](const QByteArray &bytes) {
        QStringList argv; for (const auto &value : m_options.value("copy_command").toList()) argv << value.toString();
        const auto mime = QMimeDatabase().mimeTypeForData(bytes).name();
        argv << "--type" << (mime == "text/plain" ? QString("text/plain;charset=utf-8") : mime);
        // wl-copy's documented forking handoff lets the selection outlive a closed popup.
        if (!run(argv, bytes, [this](const QByteArray &) { emit copied(); })) fail("Could not start clipboard copy.");
    });
}
}
