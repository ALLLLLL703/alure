#include "BackgroundBlur.h"
#include "ConfigStore.h"
#include "IconProvider.h"
#include "MediaLauncherHost.h"
#include "DBusServices.h"
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

class FakeMpris;
class RootAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity)
public:
    explicit RootAdaptor(FakeMpris *player);
    QString identity() const;
private:
    FakeMpris *m_player;
};
class PlayerAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanSeek READ canSeek)
public:
    explicit PlayerAdaptor(FakeMpris *player);
    QString playbackStatus() const;
    QVariantMap metadata() const;
    qlonglong position() const;
    bool shuffle() const;
    void setShuffle(bool value);
    QString loopStatus() const;
    void setLoopStatus(const QString &value);
    bool canControl() const;
    bool canPlay() const;
    bool canPause() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canSeek() const;
public slots:
    void PlayPause();
    void Next();
    void Previous();
    void SetPosition(const QDBusObjectPath &track, qlonglong position);
private:
    FakeMpris *m_player;
};
class FakeMpris final : public QObject {
    Q_OBJECT
public:
    FakeMpris(QString suffix, QString identity, QString title, QString status, bool seekable)
        : service("org.mpris.MediaPlayer2." + suffix), identity(std::move(identity)), title(std::move(title)),
          status(std::move(status)), seekable(seekable), connection(QDBusConnection::connectToBus(QDBusConnection::SessionBus, "media-fixture-" + suffix)) {
        new RootAdaptor(this); new PlayerAdaptor(this);
        registered = connection.registerService(service)
            && connection.registerObject("/org/mpris/MediaPlayer2", this, QDBusConnection::ExportAdaptors);
    }
    ~FakeMpris() override {
        connection.unregisterService(service);
        connection.unregisterObject("/org/mpris/MediaPlayer2");
        QDBusConnection::disconnectFromBus(connection.name());
    }
    void remove() { connection.unregisterService(service); }
    QString service, identity, title, status;
    bool seekable = true, shuffle = false;
    QString loop = "None";
    qlonglong positionUs = 10000000;
    int playPauseCalls = 0, nextCalls = 0, previousCalls = 0, seekCalls = 0;
    bool registered = false;
    QDBusConnection connection;
};
RootAdaptor::RootAdaptor(FakeMpris *player) : QDBusAbstractAdaptor(player), m_player(player) {}
QString RootAdaptor::identity() const { return m_player->identity; }
PlayerAdaptor::PlayerAdaptor(FakeMpris *player) : QDBusAbstractAdaptor(player), m_player(player) {}
QString PlayerAdaptor::playbackStatus() const { return m_player->status; }
QVariantMap PlayerAdaptor::metadata() const {
    return {{"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/fixture/track"))},
            {"mpris:length", 180000000LL}, {"xesam:title", m_player->title},
            {"xesam:artist", QStringList{"Fixture Artist"}}, {"xesam:album", "Fixture Album"},
            {"mpris:artUrl", ""}};
}
qlonglong PlayerAdaptor::position() const { return m_player->positionUs; }
bool PlayerAdaptor::shuffle() const { return m_player->shuffle; }
void PlayerAdaptor::setShuffle(bool value) { m_player->shuffle = value; }
QString PlayerAdaptor::loopStatus() const { return m_player->loop; }
void PlayerAdaptor::setLoopStatus(const QString &value) { m_player->loop = value; }
bool PlayerAdaptor::canControl() const { return true; }
bool PlayerAdaptor::canPlay() const { return true; }
bool PlayerAdaptor::canPause() const { return true; }
bool PlayerAdaptor::canGoNext() const { return true; }
bool PlayerAdaptor::canGoPrevious() const { return true; }
bool PlayerAdaptor::canSeek() const { return m_player->seekable; }
void PlayerAdaptor::PlayPause() { ++m_player->playPauseCalls; m_player->status = m_player->status == "Playing" ? "Paused" : "Playing"; }
void PlayerAdaptor::Next() { ++m_player->nextCalls; }
void PlayerAdaptor::Previous() { ++m_player->previousCalls; }
void PlayerAdaptor::SetPosition(const QDBusObjectPath &track, qlonglong position) {
    if (track.path() == "/fixture/track") { ++m_player->seekCalls; m_player->positionUs = position; }
}

class PausableMediaService final : public Alure::MediaService {
public:
    void pauseActions() { setBusy(true); }
    void resumeActions() { setBusy(false); }
};

class MediaLauncherUiTest : public QObject {
    Q_OBJECT
    static QQuickItem *find(QQuickItem *root, const QString &name) {
        if (root->objectName() == name) return root;
        for (auto *child : root->childItems()) if (auto *result = find(child, name)) return result;
        return nullptr;
    }
private slots:
    void configurationFieldsAndStaticEntry() {
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("missing")); QVERIFY(config.reload());
        QQmlEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config);
        engine.rootContext()->setContextProperty("Services", QVariantMap{});
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent strip(&engine);
        strip.setData("import QtQuick\nimport \"qrc:/qml\"\nModuleStrip { moduleName: 'media_launcher'; vertical: false; crossSize: 40 }", QUrl());
        std::unique_ptr<QObject> entry(strip.create()); QVERIFY2(entry, qPrintable(strip.errorString()));
        QCOMPARE(entry->property("summary").toString(), "Media players"); QVERIFY(entry->property("service").isNull());
        QQmlComponent fields(&engine);
        fields.setData("import QtQml\nimport \"qrc:/qml/SettingsFields.js\" as F\nQtObject { property var options: F.fields(Config.model.modules.media_launcher, 'modules.media_launcher') }", QUrl());
        std::unique_ptr<QObject> specs(fields.create()); QVERIFY2(specs, qPrintable(fields.errorString()));
        const auto options = specs->property("options").value<QJSValue>().toVariant().toList();
        QSet<QString> paths; for (const auto &option : options) paths.insert(option.toMap().value("path").toString());
        for (const auto *key : {"explicit_launch", "search_case_sensitive", "show_artwork", "seek_step_seconds", "tab_shortcut", "reverse_tab_shortcut", "refresh_shortcut", "close_shortcut"})
            QVERIFY(paths.contains("modules.media_launcher.behavior." + QString(key)));
        QVERIFY(warnings.isEmpty());
    }
    void registryDetailKeyboardAndRemoval() {
        FakeMpris alpha("Alpha", "Alpha Player", "Alpha Song", "Playing", true);
        FakeMpris beta("Beta", "Beta Player", "Beta Song", "Paused", false);
        QVERIFY(alpha.registered); QVERIFY(beta.registered);
        QTemporaryDir dir; Alure::ConfigStore config(dir.filePath("missing")); QVERIFY(config.reload());
        QVERIFY(config.previewText("[modules.media]\nenabled=false\n[modules.media_launcher.behavior]\ninterval_ms=60000\nartwork_remote=false\n"));
        PausableMediaService media;
        auto module = config.model().value("modules").toMap().value("media_launcher").toMap(); module["enabled"] = true; media.configure(module);
        QTRY_VERIFY_WITH_TIMEOUT(media.available(), 5000); QTRY_COMPARE(media.items().size(), 2);
        QCOMPARE(media.items().first().toMap().value("identity").toString(), "Alpha Player");
        QQmlApplicationEngine engine; engine.addImageProvider("icons", new Alure::IconProvider);
        engine.rootContext()->setContextProperty("Config", &config); engine.rootContext()->setContextProperty("Media", &media);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        Alure::MediaLauncherHost host(config, engine, true); QVERIFY(host.ready());
        QQuickView *view = nullptr;
        for (auto *window : QGuiApplication::topLevelWindows()) if (window->title() == "Alure media launcher") view = qobject_cast<QQuickView *>(window);
        QVERIFY(view); QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
        auto *root = view->rootObject(); auto *search = find(root, "media-launcher-search"); QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus()); QTRY_COMPARE(root->property("selectedId").toString(), alpha.service);
        search->setProperty("text", "bEtA"); QTRY_COMPARE(root->property("selectedId").toString(), beta.service);
        QTest::keyClick(view, Qt::Key_Tab); QCOMPARE(root->property("selectedId").toString(), beta.service);
        search->setProperty("text", ""); QTRY_COMPARE(root->property("selectedId").toString(), beta.service);
        QTest::keyClick(view, Qt::Key_Tab); QTRY_COMPARE(root->property("selectedId").toString(), alpha.service);
        QTest::keyClick(view, Qt::Key_Tab, Qt::ShiftModifier); QTRY_COMPARE(root->property("selectedId").toString(), beta.service);
        QTest::keyClick(view, Qt::Key_Up); QTRY_COMPARE(root->property("selectedId").toString(), alpha.service);
        QTest::keyClick(view, Qt::Key_Up); QCOMPARE(root->property("selectedId").toString(), alpha.service);
        QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(!root->property("registryPage").toBool());
        QCOMPARE(view->rootObject(), root); QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
        QCOMPARE(find(root, "media-launcher-title")->property("text").toString(), "Alpha Song");
        QTRY_COMPARE(QGuiApplication::focusObject()->objectName(), QString("media-launcher-back"));
        const auto selected = root->property("selectedId");
        QTest::keyClick(view, Qt::Key_Tab); QTRY_COMPARE(QGuiApplication::focusObject()->objectName(), QString("media-launcher-refresh"));
        QCOMPARE(root->property("selectedId"), selected); // Registry Tab shortcut is disabled here.
        QTest::keyClick(view, Qt::Key_Tab, Qt::ShiftModifier); QTRY_COMPARE(QGuiApplication::focusObject()->objectName(), QString("media-launcher-back"));
        QTest::keyClick(view, Qt::Key_Tab, Qt::ShiftModifier); QTRY_COMPARE(QGuiApplication::focusObject()->objectName(), QString("media-launcher-repeat"));
        for (const auto *name : {"media-launcher-back", "media-launcher-refresh", "media-launcher-close", "media-launcher-progress", "media-launcher-shuffle", "media-launcher-previous", "media-launcher-play-pause", "media-launcher-next", "media-launcher-repeat", "media-launcher-back"}) {
            QTest::keyClick(view, Qt::Key_Tab); QTRY_COMPARE(QGuiApplication::focusObject()->objectName(), QString(name));
        }
        auto *play = find(root, "media-launcher-play-pause"); QVERIFY(play); play->forceActiveFocus();
        media.pauseActions(); QVERIFY(media.busy());
        QVERIFY(QMetaObject::invokeMethod(root, "dispatch", Q_ARG(QVariant, "next"), Q_ARG(QVariant, QVariantMap{})));
        QVERIFY(root->property("queuedAction").isValid());
        QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(root->property("registryPage").toBool());
        search->setProperty("text", "Beta"); QTRY_COMPARE(root->property("selectedId").toString(), beta.service);
        QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(!root->property("registryPage").toBool());
        media.resumeActions(); QTRY_VERIFY(!media.busy()); QTest::qWait(30);
        QCOMPARE(alpha.nextCalls, 0); QCOMPARE(beta.nextCalls, 0);
        QTest::keyClick(view, Qt::Key_Escape); search->setProperty("text", "Alpha");
        QTRY_COMPARE(root->property("selectedId").toString(), alpha.service); QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(!root->property("registryPage").toBool());
        play = find(root, "media-launcher-play-pause"); play->forceActiveFocus(); QTest::keyClick(view, Qt::Key_Space);
        QTRY_COMPARE(alpha.playPauseCalls, 1); QTRY_VERIFY(!media.busy());
        auto *previous = find(root, "media-launcher-previous"); QVERIFY(previous); previous->forceActiveFocus(); QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(alpha.previousCalls, 1); QTRY_VERIFY(!media.busy());
        auto *progress = find(root, "media-launcher-progress"); QVERIFY(progress); progress->forceActiveFocus();
        const auto originalPosition = alpha.positionUs; QTest::keyClick(view, Qt::Key_Right);
        QTRY_VERIFY(alpha.seekCalls > 0); QVERIFY(alpha.positionUs > originalPosition);
        alpha.title = "Updated Alpha"; QTest::keyClick(view, Qt::Key_F5);
        QTRY_COMPARE(find(root, "media-launcher-title")->property("text").toString(), QString("Updated Alpha"));
        alpha.remove(); QTest::keyClick(view, Qt::Key_F5);
        QTRY_VERIFY(root->property("registryPage").toBool());
        QTRY_VERIFY(find(root, "media-launcher-status")->property("text").toString().contains("disappeared"));
        QTRY_COMPARE(root->property("selectedId").toString(), beta.service);
        QTest::keyClick(view, Qt::Key_Return); QTRY_VERIFY(!root->property("registryPage").toBool());
        QVERIFY(!find(root, "media-launcher-progress")->isEnabled());
        QTest::keyClick(view, Qt::Key_Escape); QTRY_VERIFY(root->property("registryPage").toBool());
        QSignalSpy finished(&host, &Alure::MediaLauncherHost::finished);
        QTest::keyClick(view, Qt::Key_Escape); QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!view->isVisible()); QVERIFY(warnings.isEmpty());
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv); QQuickStyle::setStyle("Basic"); Alure::BackgroundBlur::registerQmlType();
    MediaLauncherUiTest test; return QTest::qExec(&test, argc, argv);
}
#include "MediaLauncherUiTest.moc"
