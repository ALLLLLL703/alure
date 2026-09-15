#include "ConfigStore.h"
#include "TrayService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("alure-trayctl");
    QCommandLineParser parser;
    parser.setApplicationDescription("Observe tray apps and send DBusMenu clicked events (not physical mouse input).");
    parser.addHelpOption();
    parser.addOption({"config", "Alure TOML configuration", "path", QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/alure/config.toml"});
    parser.addOption({"timeout", "Overall deadline in milliseconds (100..600000)", "ms", "10000"});
    parser.addPositionalArgument("command", "list | menu <tray-id> [submenu-id ...] | click <tray-id> <menu-id ...>");
    const auto error = [](const QString &message, int code) { QTextStream(stderr) << "alure-trayctl: " << message << '\n'; return code; };
    if (!parser.parse(app.arguments())) return error(parser.errorText(), 2);
    if (parser.isSet("help") || parser.isSet("help-all")) { parser.process(app); return 0; }
    const auto args = parser.positionalArguments();
    bool ok = false;
    const int timeout = parser.value("timeout").toInt(&ok);
    if (!ok || timeout < 100 || timeout > 600000) return error("--timeout must be 100..600000", 2);
    if (args.isEmpty() || (args[0] != "list" && args[0] != "menu" && args[0] != "click") ||
        (args[0] == "list" && args.size() != 1) || (args[0] == "menu" && args.size() < 2) || (args[0] == "click" && args.size() < 3) || args.size() > 34)
        return error("expected list | menu <tray-id> [submenu-id ...] | click <tray-id> <menu-id ...> (maximum depth 32)", 2);
    QList<int> path;
    for (int i = 2; i < args.size(); ++i) {
        const int id = args[i].toInt(&ok);
        if (!ok || id <= 0) return error("menu IDs must be positive integers", 2);
        path << id;
    }
    Alure::ConfigStore config(parser.value("config"));
    if (!config.reload()) return error(config.diagnostic(), 2);
    auto module = config.model().value("modules").toMap().value("tray_launcher").toMap();
    if (!module.value("behavior").toMap().value("explicit_launch").toBool()) return error("explicit_launch is false", 1);
    module["enabled"] = true;
    const bool allowActions = module.value("behavior").toMap().value("allow_actions").toBool();
    if (args[0] == "click" && !allowActions) return error("allow_actions is false", 1);
    Alure::TrayService tray(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
    auto *menu = tray.menu();
    bool opened = false, waitingForEvent = false, finished = false;
    int next = 0;
    const auto finish = [&](int code, const QString &message = QString()) {
        if (finished) return;
        finished = true;
        if (!message.isEmpty()) error(message, code);
        app.exit(code);
    };
    const auto print = [&](const QVariantList &rows) {
        QTextStream(stdout) << QJsonDocument::fromVariant(rows).toJson(QJsonDocument::Compact) << '\n';
        finish(0);
    };
    // Defer each transition: TrayMenu may emit multiple changed signals per request.
    const auto advance = [&] {
        if (!opened || finished || menu->loading()) return;
        if (!menu->error().isEmpty()) { finish(1, menu->error()); return; }
        if (waitingForEvent) return; // only activated() confirms Event's reply
        if (next == path.size()) { print(menu->items()); return; }
        const int id = path[next];
        QVariantMap row;
        for (const auto &entry : menu->items()) if (entry.toMap().value("id").toInt() == id) row = entry.toMap();
        if (row.isEmpty()) { finish(1, "Stale or invisible menu ID: " + QString::number(id)); return; }
        if (!row.value("enabled").toBool() || row.value("type") == "separator") { finish(1, "Disabled item or separator: " + QString::number(id)); return; }
        const bool finalClick = args[0] == "click" && next == path.size() - 1;
        if (row.value("submenu").toBool() == finalClick) { finish(1, finalClick ? "Final click must be a leaf" : "Path traversal requires a submenu; no leaf was activated"); return; }
        ++next;
        waitingForEvent = finalClick;
        if (!menu->select(id)) finish(1, "Menu selection refused");
    };
    QObject::connect(menu, &Alure::TrayMenu::changed, &app, [&] { QTimer::singleShot(0, &app, advance); });
    QObject::connect(menu, &Alure::TrayMenu::activated, &app, [&] { finish(0); });
    QObject::connect(&tray, &Alure::Service::changed, &app, [&] {
        if (opened || finished || tray.busy()) return;
        if (!tray.available()) { if (tray.diagnostic() != "Connecting") finish(1, tray.diagnostic()); return; }
        if (args[0] == "list") { print(tray.items()); return; }
        opened = true;
        tray.openMenu(args[1]);
    });
    QTimer::singleShot(timeout, &app, [&] { finish(1, "Overall timeout; action completion is unconfirmed"); });
    QTimer::singleShot(0, &app, [&] { tray.configure(module); });
    return app.exec();
}
