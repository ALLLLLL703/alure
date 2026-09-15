#include "BackgroundBlur.h"
#include "TrayLauncherHost.h"
#include "ClipboardHost.h"
#include "ClipboardImageProvider.h"
#include "ConfigStore.h"
#include "IconProvider.h"
#include "NotificationImageProvider.h"
#include "PanelHost.h"
#include "Services.h"
#include "SingleInstance.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <memory>

int main(int argc, char **argv) {
  // Inspect only our exact switch before constructing a GUI application.
  // Validation needs no display server or QPA plugin, including when the file
  // is invalid.
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    const QByteArray arg(argv[i]);
    if (arg == "--config" || arg == "--quit-after-ms") {
      ++i;
      continue;
    }
    if (arg == "--")
      break;
    if (arg == "--validate-config" || arg == "--help" || arg == "-h" ||
        arg == "--version" || arg == "-v")
      headless = true;
  }
  std::unique_ptr<QCoreApplication> app;
  if (headless)
    app = std::make_unique<QCoreApplication>(argc, argv);
  else
    app = std::make_unique<QGuiApplication>(argc, argv);
  QCoreApplication::setApplicationName("alure");
  QCoreApplication::setApplicationVersion("0.1.0");
  QCommandLineParser parser;
  parser.setApplicationDescription("Alure configurable Wayland shell");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {"config", "Use this TOML file", "path",
       QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           "/alure/config.toml"});
  parser.addOption(
      {"validate-config",
       "Validate configuration without a display; missing file uses defaults"});
  parser.addOption(
      {"settings", "Open a standalone settings editor, without layer-shell"});
  parser.addOption({"clipboard", "Open standalone cliphist history at the "
                                 "cursor (no panels or other services)"});
  parser.addOption({"preview", "Use normal windows instead of layer-shell"});
  parser.addOption({"quit-after-ms",
                    "Exit after a bounded interval (testing only, 1..600000)",
                    "ms"});
  parser.addPositionalArgument("tray-launcher", "Open the centered, observer-only tray launcher");
  parser.process(*app);
  const bool trayLauncher = parser.positionalArguments() == QStringList{"tray-launcher"};
  if (!trayLauncher && !parser.positionalArguments().isEmpty()) {
    QTextStream(stderr)
        << "Unexpected positional arguments; use --config PATH\n";
    return 2;
  }
  if (trayLauncher && (parser.isSet("settings") || parser.isSet("clipboard") || parser.isSet("validate-config"))) {
    QTextStream(stderr) << "tray-launcher cannot be combined with settings, clipboard or validation\n";
    return 2;
  }
  if (parser.isSet("settings") && parser.isSet("clipboard")) {
    QTextStream(stderr)
        << "--settings and --clipboard are mutually exclusive\n";
    return 2;
  }
  if (parser.isSet("settings") && parser.isSet("preview")) {
    QTextStream(stderr) << "--settings and --preview are mutually exclusive\n";
    return 2;
  }
  int quitAfter = 0;
  if (parser.isSet("quit-after-ms")) {
    bool ok;
    quitAfter = parser.value("quit-after-ms").toInt(&ok);
    if (!ok || quitAfter < 1 || quitAfter > 600000) {
      QTextStream(stderr) << "--quit-after-ms must be 1..600000\n";
      return 2;
    }
  }
  std::unique_ptr<Alure::SingleInstance> instance;
  if (!parser.isSet("validate-config") &&
      (parser.isSet("settings") || parser.isSet("clipboard") || trayLauncher)) {
    instance = std::make_unique<Alure::SingleInstance>(
        trayLauncher ? "org.alure.TrayLauncher" : parser.isSet("settings") ? "org.alure.Settings"
                                 : "org.alure.Clipboard");
    const int state = instance->acquire();
    if (state != 0)
      return state > 0 ? 0 : 1;
  }
  Alure::ConfigStore config(parser.value("config"));
  const bool valid = config.reload();
  if (!valid)
    QTextStream(stderr) << config.path() << ": " << config.diagnostic() << '\n';
  if (parser.isSet("validate-config")) {
    if (valid)
      QTextStream(stdout) << "Configuration valid: " << config.path() << '\n';
    return valid ? 0 : 1;
  }
  // Settings can open an invalid file for repair; panels fail loudly on bad
  // startup.
  if (!valid && !parser.isSet("settings"))
    return 1;
  if (!parser.isSet("settings") && !parser.isSet("preview") &&
      QGuiApplication::platformName() != "wayland") {
    QTextStream(stderr) << "Panel mode requires Wayland. Use --preview or "
                           "--settings for normal windows.\n";
    return 2;
  }
  QQuickStyle::setStyle(config.model()
                            .value("settings")
                            .toMap()
                            .value("controls_style")
                            .toString());
  QQuickWindow::setDefaultAlphaBuffer(true);
  // LayerShellQt::Window::get selects layer-shell per panel/toast. Keep the
  // default xdg-shell integration for native transient module popups.
  QGuiApplication::setQuitOnLastWindowClosed(
      !parser.isSet("clipboard") &&
      (parser.isSet("settings") || parser.isSet("preview")));
  // Settings and validation never acquire names or start service processes.
  std::unique_ptr<Alure::Services> services;
  std::unique_ptr<Alure::TrayService> tray;
  std::unique_ptr<Alure::ClipboardService> clipboard;
  if (trayLauncher) {
    auto module = config.model().value("modules").toMap().value("tray_launcher").toMap();
    if (!module.value("behavior").toMap().value("explicit_launch").toBool()) {
      QTextStream(stderr) << "modules.tray_launcher.behavior.explicit_launch is false\n";
      return 1;
    }
    module["enabled"] = true;
    tray = std::make_unique<Alure::TrayService>(nullptr, Alure::TrayService::WatcherPolicy::ObserveOnly);
    tray->configure(module);
  } else if (parser.isSet("clipboard")) {
    clipboard = std::make_unique<Alure::ClipboardService>();
    auto module =
        config.model().value("modules").toMap().value("clipboard").toMap();
    module["enabled"] = true; // explicit standalone request is independent of
                              // panel-module enablement
    clipboard->configure(module);
  } else if (!parser.isSet("settings"))
    services = std::make_unique<Alure::Services>(config);
  Alure::BackgroundBlur::registerQmlType();
  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                   [](const QList<QQmlError> &errors) {
                     for (const auto &error : errors)
                       QTextStream(stderr) << error.toString() << '\n';
                   });
  engine.addImageProvider(
      "icons", new Alure::IconProvider); // ownership transferred to engine
  engine.rootContext()->setContextProperty("Config", &config);
  if (services) {
    engine.rootContext()->setContextProperty("Services", services.get());
    engine.addImageProvider(
        "clipboard", new Alure::ClipboardImageProvider(services->clipboard()));
    engine.addImageProvider(
        "notifications",
        new Alure::NotificationImageProvider(services->notifications()));
  }
  if (tray) engine.rootContext()->setContextProperty("Tray", tray.get());
  if (clipboard) {
    engine.rootContext()->setContextProperty(
        "Services",
        QVariantMap{{"clipboard", QVariant::fromValue(clipboard.get())}});
    engine.addImageProvider("clipboard",
                            new Alure::ClipboardImageProvider(clipboard.get()));
  }
  QObject::connect(&config, &Alure::ConfigStore::diagnosticChanged, &engine,
                   [&config] {
                     if (!config.diagnostic().isEmpty())
                       qWarning().noquote() << config.diagnostic();
                   });
  std::unique_ptr<Alure::PanelHost> host;
  std::unique_ptr<Alure::ClipboardHost> clipboardHost;
  std::unique_ptr<Alure::TrayLauncherHost> trayHost;
  if (tray) {
    trayHost = std::make_unique<Alure::TrayLauncherHost>(config, engine, parser.isSet("preview"));
    if (!trayHost->ready()) return 1;
    QObject::connect(trayHost.get(), &Alure::TrayLauncherHost::finished, app.get(), &QCoreApplication::quit);
    QObject::connect(instance.get(), &Alure::SingleInstance::activated, trayHost.get(), &Alure::TrayLauncherHost::activate);
  } else if (clipboard) {
    clipboardHost = std::make_unique<Alure::ClipboardHost>(
        config, engine, parser.isSet("preview"));
    QObject::connect(clipboardHost.get(), &Alure::ClipboardHost::finished,
                     app.get(), &QCoreApplication::quit);
    QObject::connect(instance.get(), &Alure::SingleInstance::activated,
                     clipboardHost.get(), &Alure::ClipboardHost::closePopup);
  } else if (parser.isSet("settings")) {
    engine.load(QUrl("qrc:/qml/Settings.qml"));
    if (engine.rootObjects().isEmpty())
      return 1;
    QObject::connect(
        instance.get(), &Alure::SingleInstance::activated, &engine, [&engine] {
          if (auto *window =
                  qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
            window->showNormal();
            window->raise();
            window->requestActivate();
          }
        });
  } else {
    host = std::make_unique<Alure::PanelHost>(config, engine,
                                              parser.isSet("preview"));
    QObject::connect(
        services->notifications(), &Alure::Service::changed, host.get(),
        [&] { host->syncNotifications(services->notifications()->items()); });
    QObject::connect(services->taskbar(), &Alure::TaskbarService::panelWindowsChanged,
                     host.get(), &Alure::PanelHost::syncPanelWindows);
    host->syncPanelWindows(services->taskbar()->panelWindows(), services->taskbar()->panelWindowsAvailable());
    QObject::connect(services->osd(), &Alure::OsdController::requested, host.get(), &Alure::PanelHost::showOsd);
    QObject::connect(services->osd(), &Alure::OsdController::reset, host.get(), &Alure::PanelHost::closeOsd);
    config.startWatching();
  }
  if (quitAfter)
    QTimer::singleShot(quitAfter, app.get(), &QCoreApplication::quit);
  return app->exec();
}
