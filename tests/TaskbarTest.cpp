#include "TaskbarService.h"
#include "ConfigStore.h"
#include <QLocalServer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace Alure;
namespace {
QVariantMap module(const QVariantMap &options = {}) {
    QVariantMap model; QString error;
    ConfigStore::parse({}, model, error);
    auto result = model.value("modules").toMap().value("taskbar").toMap();
    auto behavior = result.value("behavior").toMap();
    for (auto it = options.begin(); it != options.end(); ++it) behavior[it.key()] = it.value();
    result["behavior"] = behavior; return result;
}
class NiriFixture : public QObject {
public:
    QTemporaryDir directory;
    QLocalServer server;
    QPointer<QLocalSocket> stream;
    QList<QByteArray> requests;
    QByteArray actionReply = "{\"Ok\":\"Handled\"}\n";
    QByteArray initial = R"({"Ok":"Handled"}
{"WorkspacesChanged":{"workspaces":[{"id":10,"output":"A","is_active":true,"is_focused":true},{"id":11,"output":"A","is_active":false,"is_focused":false},{"id":20,"output":"B","is_active":true,"is_focused":false}]}}
{"WindowsChanged":{"windows":[{"id":9007199254740993,"title":"Zulu","app_id":"org.test.Z","workspace_id":10,"is_focused":true},{"id":2,"title":"Alpha","app_id":"org.test.A","workspace_id":20,"is_focused":false},{"id":3,"title":null,"app_id":null,"workspace_id":11,"is_focused":false}]}}
)";
    NiriFixture() {
        server.listen(directory.filePath("niri.sock"));
        connect(&server, &QLocalServer::newConnection, this, [this] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (!socket->canReadLine()) return;
                const auto request = socket->readLine().trimmed(); requests.append(request);
                if (request == "\"EventStream\"") { stream = socket; socket->write(initial); }
                else socket->write(actionReply);
            });
        });
    }
    void event(const QByteArray &data) { QVERIFY(stream); stream->write(data + '\n'); stream->flush(); }
    QVariantMap options() const { return {{"socket_path", server.fullServerName()}, {"interval_ms", 100}, {"timeout_ms", 150}}; }
};
}
class TaskbarTest : public QObject {
    Q_OBJECT
private slots:
    void configuration() {
        QVariantMap model; QString error;
        QVERIFY(ConfigStore::parse({}, model, error));
        auto task = model.value("modules").toMap().value("taskbar").toMap();
        QVERIFY(task.value("enabled").toBool());
        QVERIFY(!task.value("style").toMap().value("show_label").toBool());
        QCOMPARE(task.value("behavior").toMap().value("output_scope").toString(), "panel");
        for (const QByteArray &setting : {QByteArray("behavior.ordering='provider'"), QByteArray("behavior.workspace_scope='current'"), QByteArray("behavior.output_scope='A'"), QByteArray("behavior.focus_on_click=1"), QByteArray("behavior.socket_path='relative'"), QByteArray("style.label_size=5"), QByteArray("style.task_width=513"), QByteArray("style.active_indicator='none'"), QByteArray("style.show_label='yes'")}) {
            QVERIFY2(!ConfigStore::parse("[modules.taskbar]\n" + setting, model, error), setting.constData());
            QVERIFY2(error.contains("modules.taskbar"), qPrintable(error));
        }
        QVERIFY(ConfigStore::parse("[modules.workspaces]\nenabled=false\n[modules.taskbar.style]\nshow_label=true\nlabel_size=20\ntask_width=240\n[modules.taskbar.behavior]\nordering='title'\nworkspace_scope='focused'\noutput_scope='all'\nfocus_on_click=false\n[[panels]]\nmodules=['taskbar']", model, error));
        QCOMPARE(model.value("panels").toList().first().toMap().value("modules").toStringList(), QStringList{"taskbar"});
    }
    void eventsAndExactFocus() {
        NiriFixture fixture; TaskbarService service;
        service.configure(module(fixture.options())); QTRY_VERIFY(service.available());
        QCOMPARE(service.items().size(), 3);
        QCOMPARE(service.items().first().toMap().value("id").toString(), "2");
        QCOMPARE(service.items().last().toMap().value("id").toString(), "9007199254740993");
        QVERIFY(service.items().last().toMap().value("workspace_focused").toBool());
        QVERIFY(!service.action("minimize", {{"id", "2"}}));
        QVERIFY(!service.action("activate", {{"id", "999"}}));
        QVERIFY(service.action("activate", {{"id", "9007199254740993"}})); QTRY_VERIFY(!service.busy());
        QCOMPARE(fixture.requests.last(), QByteArray("{\"Action\":{\"FocusWindow\":{\"id\":9007199254740993}}}"));
        fixture.event(R"({"WindowFocusChanged":{"id":2}})");
        QTRY_VERIFY(service.items().first().toMap().value("is_focused").toBool());
        QVERIFY(!service.items().last().toMap().value("is_focused").toBool());
        fixture.event(R"({"WorkspaceActivated":{"id":20,"focused":true}})");
        QTRY_VERIFY(service.items().first().toMap().value("output_focused").toBool());
        QVERIFY(service.items().last().toMap().value("workspace_active").toBool());
        QVERIFY(!service.items().last().toMap().value("workspace_focused").toBool());
        fixture.event(R"({"WindowOpenedOrChanged":{"window":{"id":2,"title":"Moved","app_id":"org.test.A","workspace_id":11,"is_focused":true}}})");
        QTRY_COMPARE(service.items().first().toMap().value("title").toString(), "Moved");
        QCOMPARE(service.items().first().toMap().value("output").toString(), "A");
        QVERIFY(!service.items().first().toMap().value("workspace_active").toBool());
        fixture.event(R"({"WorkspaceActivated":{"id":11,"focused":false}})");
        QTRY_VERIFY(service.items().first().toMap().value("workspace_active").toBool());
        fixture.event(R"({"WindowClosed":{"id":2}})"); QTRY_COMPARE(service.items().size(), 2);
        QVERIFY(!service.action("activate", {{"id", "2"}}));
        fixture.event(R"({"WindowFocusChanged":{"id":null}})");
        QTRY_VERIFY(!service.items().last().toMap().value("is_focused").toBool());
        fixture.event(R"({"WindowsChanged":{"windows":[]}})"); QTRY_VERIFY(service.items().isEmpty());
        QVERIFY(service.available());
    }
    void orderingGatesReconnectAndFailures() {
        NiriFixture fixture; TaskbarService service; auto options = fixture.options(); options["ordering"] = "title";
        service.configure(module(options)); QTRY_VERIFY(service.available());
        QCOMPARE(service.items().first().toMap().value("id").toString(), "3");
        options["ordering"] = "app-id"; options["allow_actions"] = false;
        service.configure(module(options)); QTRY_VERIFY(service.available());
        QVERIFY(!service.action("activate", {{"id", "2"}}));
        options["allow_actions"] = true; options["focus_on_click"] = false;
        service.configure(module(options)); QTRY_VERIFY(service.available());
        QVERIFY(!service.action("activate", {{"id", "2"}}));
        options["focus_on_click"] = true; service.configure(module(options));
        fixture.actionReply = "{\"Err\":\"No window\"}\n";
        QVERIFY(service.action("activate", {{"id", "2"}})); QTRY_VERIFY(!service.busy());
        QVERIFY(service.state().value("actionError").toString().contains("No window"));
        QVERIFY(service.available());
        const auto requests = fixture.requests.size();
        fixture.stream->disconnectFromServer();
        QTRY_VERIFY(fixture.requests.size() > requests); QTRY_VERIFY(service.available());
        fixture.initial.clear(); fixture.event("not json"); QTRY_VERIFY(!service.available());
        QVERIFY(service.items().isEmpty());
        QTRY_VERIFY(service.diagnostic().contains("timed out"));
        auto disabled = module(options); disabled["enabled"] = false; service.configure(disabled);
        QCOMPARE(service.diagnostic(), "Disabled"); QVERIFY(service.items().isEmpty());
        const auto stopped = fixture.requests.size(); QTest::qWait(250); QCOMPARE(fixture.requests.size(), stopped);
    }
    void fragmentedAndBoundedStream() {
        NiriFixture fixture; const auto initial = fixture.initial; fixture.initial = initial.left(13);
        TaskbarService service; service.configure(module(fixture.options())); QTRY_VERIFY(fixture.stream);
        QVERIFY(!service.available()); fixture.stream->write(initial.mid(13)); QTRY_VERIFY(service.available());
        fixture.initial.clear(); fixture.stream->write(QByteArray(1024 * 1024 + 2, 'x'));
        QTRY_VERIFY(!service.available()); QVERIFY(service.items().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TaskbarTest)
#include "TaskbarTest.moc"
