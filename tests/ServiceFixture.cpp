#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QFile>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto mode = app.arguments().value(1);
    if (mode == "clipboard" || mode == "clipboard-copy") {
        const auto root = app.arguments().value(2);
        QFile log(root + "/calls"); if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return 1;
        log.write(app.arguments().mid(3).join('|').toUtf8() + '\n');
        if (mode == "clipboard-copy") {
            QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
            QFile copy(root + "/copied"); if (!copy.open(QIODevice::WriteOnly)) return 1;
            copy.write(input.readAll()); return 0;
        }
        const auto operation = app.arguments().value(5);
        if (app.arguments().value(3) != "-db-path") return 2;
        if (operation == "decode") {
            QFile entry(root + "/" + app.arguments().value(6)); if (!entry.open(QIODevice::ReadOnly)) return 3;
            QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1; output.write(entry.readAll()); return 0;
        }
        QFile listing(root + "/list"); if (!listing.open(QIODevice::ReadOnly)) return 4;
        auto rows = listing.readAll().split('\n'); listing.close();
        if (operation == "list") { QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1; output.write(rows.join('\n')); return 0; }
        if (operation == "wipe") rows.clear();
        else if (operation == "delete") {
            QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 1; const auto id = input.readAll().trimmed();
            rows.removeIf([&id](const QByteArray &line) { return line.startsWith(id + '\t'); });
        } else return 5;
        if (!listing.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 6;
        listing.write(rows.join('\n')); return 0;
    }
    if (mode.startsWith("audio-") && app.arguments().size() == 3) {
        QFile file(app.arguments().value(2)); if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return 1;
        file.write(mode.toUtf8() + '\n');
    }
    if (mode == "audio-fail") return 1;
    if (mode == "audio-pipewire") { QTextStream(stdout) << "Volume: 0.42 [MUTED]\n"; return 0; }
    if (mode == "audio-info") { QTextStream(stdout) << R"({"default_sink_name":"speakers","server_name":"pulseaudio"})"; return 0; }
    if (mode == "audio-sinks") {
        QTextStream(stdout) << R"([{"index":1,"name":"hdmi","mute":false,"volume":{"mono":{"value":65536}}},{"index":7,"name":"speakers","description":"Fixture speakers","mute":true,"volume":{"front-left":{"value":32768},"front-right":{"value":16384}}}])"; return 0;
    }
    if (mode == "capture-args" || mode == "capture-args-slow") {
        QFile file(app.arguments().value(2)); if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return 1;
        file.write(app.arguments().mid(3).join('|').toUtf8() + '\n'); file.close();
        if (mode == "capture-args-slow") { QTimer::singleShot(150, &app, &QCoreApplication::quit); return app.exec(); }
        return 0;
    }
    if (mode == "read-file") {
        QFile file(app.arguments().value(2)); if (!file.open(QIODevice::ReadOnly)) return 1;
        QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1; output.write(file.readAll()); return 0;
    }
    if (mode == "sleep") { QTimer::singleShot(10000, &app, &QCoreApplication::quit); return app.exec(); }
    if (mode == "flood") { QTextStream(stdout) << QString(1024 * 1024 + 100, 'x'); return 0; }
    if (mode == "emptyUpdates") return 2;
    if (mode == "radio") { QTextStream(stdout) << "enabled\n"; return 0; }
    if (mode == "wifi") { QTextStream(stdout) << "yes:Fixture:80\n"; return 0; }
    if (mode == "saved") { QTextStream(stdout) << "12345678-1234-1234-1234-123456789abc:Fixture:802-11-wireless\n"; return 0; }
    if (mode == "capture") {
        QFile file(app.arguments().value(2)); if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return 1;
        file.write(app.arguments().value(3).toUtf8() + '\n'); return 0;
    }
    if (mode == "volume") { QTextStream(stdout) << "Volume: 0.42 [MUTED]\n"; return 0; }
    return 1;
}
