#include "ConfigStore.h"
#include "IconProvider.h"
#include "PanelHost.h"
#include "Services.h"
#include <LayerShellQt/Shell>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <memory>

int main(int argc, char **argv) {
    // Inspect only our exact switch before constructing a GUI application. Validation
    // needs no display server or QPA plugin, including when the file is invalid.
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--config" || arg == "--quit-after-ms") { ++i; continue; }
        if (arg == "--") break;
        if (arg == "--validate-config" || arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v") headless = true;
    }
    std::unique_ptr<QCoreApplication> app;
    if (headless) app = std::make_unique<QCoreApplication>(argc, argv);
    else app = std::make_unique<QGuiApplication>(argc, argv);
    QCoreApplication::setApplicationName("alure");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Alure configurable Wayland shell (foundation)");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"config", "Use this TOML file", "path", QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/alure/config.toml"});
    parser.addOption({"validate-config", "Validate configuration without a display; missing file uses defaults"});
    parser.addOption({"settings", "Open a standalone settings editor, without layer-shell"});
    parser.addOption({"preview", "Use normal windows instead of layer-shell"});
    parser.addOption({"quit-after-ms", "Exit after a bounded interval (testing only, 1..600000)", "ms"});
    parser.process(*app);
    if (!parser.positionalArguments().isEmpty()) { QTextStream(stderr) << "Unexpected positional arguments; use --config PATH\n"; return 2; }
    if (parser.isSet("settings") && parser.isSet("preview")) { QTextStream(stderr) << "--settings and --preview are mutually exclusive\n"; return 2; }
    int quitAfter = 0;
    if (parser.isSet("quit-after-ms")) {
        bool ok; quitAfter = parser.value("quit-after-ms").toInt(&ok);
        if (!ok || quitAfter < 1 || quitAfter > 600000) { QTextStream(stderr) << "--quit-after-ms must be 1..600000\n"; return 2; }
    }
    Alure::ConfigStore config(parser.value("config"));
    const bool valid = config.reload();
    if (!valid) QTextStream(stderr) << config.path() << ": " << config.diagnostic() << '\n';
    if (parser.isSet("validate-config")) {
        if (valid) QTextStream(stdout) << "Configuration valid: " << config.path() << '\n';
        return valid ? 0 : 1;
    }
    // Settings can open an invalid file for repair; panels fail loudly on bad startup.
    if (!valid && !parser.isSet("settings")) return 1;
    if (!parser.isSet("settings") && !parser.isSet("preview") && QGuiApplication::platformName() != "wayland") {
        QTextStream(stderr) << "Panel mode requires Wayland. Use --preview or --settings for normal windows.\n"; return 2;
    }
    QQuickWindow::setDefaultAlphaBuffer(true);
    if (!parser.isSet("settings") && !parser.isSet("preview")) LayerShellQt::Shell::useLayerShell();
    QGuiApplication::setQuitOnLastWindowClosed(parser.isSet("settings") || parser.isSet("preview"));
    // Settings and validation never acquire names or start service processes.
    std::unique_ptr<Alure::Services> services;
    if (!parser.isSet("settings")) services = std::make_unique<Alure::Services>(config);
    QQmlApplicationEngine engine;
    engine.addImageProvider("icons", new Alure::IconProvider); // ownership transferred to engine
    engine.rootContext()->setContextProperty("Config", &config);
    if (services) engine.rootContext()->setContextProperty("Services", services.get());
    QObject::connect(&config, &Alure::ConfigStore::diagnosticChanged, &engine, [&config] {
        if (!config.diagnostic().isEmpty()) qWarning().noquote() << config.diagnostic();
    });
    std::unique_ptr<Alure::PanelHost> host;
    if (parser.isSet("settings")) {
        engine.load(QUrl("qrc:/qml/Settings.qml"));
        if (engine.rootObjects().isEmpty()) return 1;
    } else {
        host = std::make_unique<Alure::PanelHost>(config, engine, parser.isSet("preview"));
        config.startWatching();
    }
    if (quitAfter) QTimer::singleShot(quitAfter, app.get(), &QCoreApplication::quit);
    return app->exec();
}
