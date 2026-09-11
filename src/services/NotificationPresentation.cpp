#include "NotificationService.h"
#include <QDBusArgument>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <algorithm>
#include <cstring>

namespace Alure {
QString NotificationService::iconSource(uint id, const QString &icon, const QVariantMap &hints) {
    QImage image;
    const auto limit = m_options.value("icon_max_pixels").toLongLong();
    for (const auto *key : {"image-data", "image_data", "icon_data"}) {
        const auto value = unbox(hints.value(key));
        if (value.metaType() != QMetaType::fromType<QDBusArgument>()) continue;
        const auto argument = qvariant_cast<QDBusArgument>(value);
        if (argument.currentType() != QDBusArgument::StructureType) continue;
        int width = 0, height = 0, stride = 0, bits = 0, channels = 0; bool alpha = false; QByteArray bytes;
        argument.beginStructure(); argument >> width >> height >> stride >> alpha >> bits >> channels >> bytes; argument.endStructure();
        if (width <= 0 || height <= 0 || qint64(width) * height > limit || bits != 8 ||
            (channels != 3 && channels != 4) || (alpha && channels != 4) || stride < qint64(width) * channels ||
            qint64(stride) * (height - 1) + qint64(width) * channels > bytes.size() || bytes.size() > limit * 4) continue;
        const auto format = channels == 3 ? QImage::Format_RGB888 : alpha ? QImage::Format_RGBA8888 : QImage::Format_RGBX8888;
        QImage decoded(width, height, format);
        if (decoded.isNull()) continue;
        decoded.fill(Qt::transparent);
        // GdkPixbuf may omit padding after the final row; never read that padding.
        for (int row = 0; row < height; ++row) std::memcpy(decoded.scanLine(row), bytes.constData() + qint64(row) * stride, size_t(width) * channels);
        image = width > 128 || height > 128 ? decoded.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation) : decoded;
        if (!image.isNull()) break;
    }
    if (image.isNull()) {
        auto path = unbox(hints.value("image-path", hints.value("image_path"))).toString();
        if (path.isEmpty()) path = icon;
        const QUrl url(path);
        if (url.isLocalFile()) path = url.toLocalFile();
        const QFileInfo file(path);
        if (file.isAbsolute() && file.isFile() && file.size() <= limit * 4) {
            QImageReader reader(path);
            const auto size = reader.size();
            if (size.isValid() && qint64(size.width()) * size.height() <= limit) {
                if (size.width() > 128 || size.height() > 128) reader.setScaledSize(size.scaled(128, 128, Qt::KeepAspectRatio));
                image = reader.read();
            }
        } else if (!path.isEmpty() && !path.contains('/') && !path.contains(':')) {
            return "image://icons/theme/" + path;
        }
    }
    if (!image.isNull()) {
        const auto key = QString::number(id) + "/" + QString::number(++m_imageRevision);
        m_images.setMaxCost(m_options.value("icon_cache_kib").toInt());
        const int cost = std::max(1, int(image.sizeInBytes() / 1024));
        m_images.insert(key, new QImage(image), cost); // QCache owns memory; no on-disk thumbnails.
        return "image://notifications/" + key;
    }
    auto name = icon;
    if (name.isEmpty()) name = unbox(hints.value("desktop-entry")).toString();
    if (name.endsWith(".desktop")) name.chop(8);
    if (name.isEmpty() || name.contains('/') || name.contains(':')) name = "notifications";
    return "image://icons/theme/" + name;
}
void NotificationService::initPresentation() {
    m_focusDeadline.setSingleShot(true);
    m_focusSocket.setReadBufferSize(1024 * 1024 + 1);
    connect(&m_focusSocket, &QLocalSocket::connected, this, [this] { m_focusSocket.write(m_focusPayload + '\n'); });
    connect(&m_focusDeadline, &QTimer::timeout, this, [this] {
        m_focusNotification.clear(); m_focusSocket.abort(); qWarning() << "Notification window focus timed out.";
    });
    connect(&m_focusSocket, &QLocalSocket::errorOccurred, this, [this] {
        if (m_focusNotification.isEmpty()) return;
        m_focusDeadline.stop(); m_focusNotification.clear(); qWarning() << "Notification focus: Niri IPC unavailable.";
    });
    connect(&m_focusSocket, &QLocalSocket::readyRead, this, [this] {
        m_focusResponse += m_focusSocket.readAll();
        if (m_focusResponse.size() > 1024 * 1024) {
            m_focusDeadline.stop(); m_focusNotification.clear(); m_focusSocket.abort(); qWarning() << "Notification focus: Niri response too large."; return;
        }
        const auto end = m_focusResponse.indexOf('\n');
        if (end < 0) return;
        m_focusDeadline.stop(); m_focusSocket.abort();
        const auto reply = QJsonDocument::fromJson(m_focusResponse.first(end)).object();
        if (!reply.contains("Ok")) { m_focusNotification.clear(); qWarning() << "Notification focus: invalid Niri response."; return; }
        if (m_focusing) { m_focusNotification.clear(); return; }
        const auto normalize = [](QString name) { if (name.endsWith(".desktop", Qt::CaseInsensitive)) name.chop(8); return name.toCaseFolded(); };
        const auto desktop = normalize(m_focusNotification.value("desktopEntry").toString());
        const auto app = desktop.isEmpty() ? normalize(m_focusNotification.value("appName").toString()) : desktop;
        const auto pid = m_focusNotification.value("pid").toLongLong();
        QJsonArray pidMatches, appMatches;
        for (const auto &value : reply.value("Ok").toObject().value("Windows").toArray()) {
            const auto window = value.toObject();
            if (pid > 0 && window.value("pid").toInteger() == pid) pidMatches.append(window);
            if (!app.isEmpty() && normalize(window.value("app_id").toString()) == app) appMatches.append(window);
        }
        const auto matches = pidMatches.isEmpty() ? appMatches : pidMatches;
        QJsonObject target;
        if (matches.size() == 1) target = matches.first().toObject();
        else for (const auto &value : matches) if (value.toObject().value("is_focused").toBool()) { target = value.toObject(); break; }
        if (target.isEmpty()) { m_focusNotification.clear(); qWarning() << "Notification focus: no unambiguous sender window; default notification action was still delivered if supplied."; return; }
        m_focusing = true;
        requestFocus(QJsonDocument(QJsonObject{{"Action", QJsonObject{{"FocusWindow", QJsonObject{{"id", target.value("id")}}}}}}).toJson(QJsonDocument::Compact));
    });
}
void NotificationService::requestFocus(const QByteArray &payload) {
    m_focusSocket.abort(); m_focusResponse.clear(); m_focusPayload = payload;
    auto socket = m_options.value("niri_socket").toString();
    if (socket.isEmpty()) socket = qEnvironmentVariable("NIRI_SOCKET");
    if (socket.isEmpty()) { m_focusNotification.clear(); qWarning() << "Notification focus requires NIRI_SOCKET."; return; }
    m_focusDeadline.start(timeout()); m_focusSocket.connectToServer(socket);
}
void NotificationService::focusSender(const QVariantMap &notification) {
    m_focusNotification = notification; m_focusing = false; requestFocus("\"Windows\"");
}
}
