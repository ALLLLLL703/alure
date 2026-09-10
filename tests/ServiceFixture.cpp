#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QFile>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto mode = app.arguments().value(1);
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
