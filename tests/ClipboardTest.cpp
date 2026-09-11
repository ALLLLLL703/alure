#include "ClipboardService.h"
#include "ConfigStore.h"
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
using namespace Alure;
namespace {
bool write(const QString &path, const QByteArray &bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
QVariantMap module(const QString &root) {
    QVariantMap model; QString error; ConfigStore::parse({}, model, error);
    auto result = model.value("modules").toMap().value("clipboard").toMap(); result["enabled"] = true;
    auto options = result.value("behavior").toMap();
    options["database_path"] = root + "/db";
    options["cliphist_command"] = QVariantList{SERVICE_FIXTURE, "clipboard", root};
    options["copy_command"] = QVariantList{SERVICE_FIXTURE, "clipboard-copy", root};
    options["preview_image_size"] = 64;
    result["behavior"] = options; return result;
}
}
class ClipboardTest : public QObject {
    Q_OBJECT
private slots:
    void historyPreviewCopyDelete() {
        QTemporaryDir temp; const auto root = temp.path();
        QVERIFY(write(root + "/db", {})); QVERIFY(write(root + "/list", "3\timage\n2\tbinary\n1\ttext\n"));
        const QByteArray binary("\0a\xff\nb\0", 6);
        QVERIFY(write(root + "/2", binary)); QVERIFY(write(root + "/1", "<b>plain text</b>\nsecond line"));
        QImage image(128, 64, QImage::Format_ARGB32); image.fill(Qt::green); QVERIFY(image.save(root + "/3", "PNG"));
        ClipboardService service; service.configure(module(root));
        QVERIFY(!QFile::exists(root + "/calls")); // enabled module does not read history until opened
        service.openView(); QTRY_COMPARE(service.items().size(), 3); QTRY_VERIFY(!service.busy());
        service.previewItem("3"); QTRY_COMPARE(service.preview().value("kind").toString(), "image");
        QCOMPARE(service.previewImage().size(), QSize(64, 32)); QTRY_VERIFY(!service.busy());
        service.previewItem("1"); QTRY_COMPARE(service.preview().value("kind").toString(), "text");
        QCOMPARE(service.preview().value("text").toString(), "<b>plain text</b>\nsecond line"); QTRY_VERIFY(!service.busy());
        QVERIFY(!service.action("copy", {{"id", "2;bad"}}));
        QSignalSpy copied(&service, &ClipboardService::copied);
        QVERIFY(service.action("copy", {{"id", "2"}})); QTRY_COMPARE(copied.size(), 1); QTRY_VERIFY(!service.busy());
        QCOMPARE(read(root + "/copied"), binary); QVERIFY(read(root + "/calls").contains("--type|application/octet-stream"));
        QVERIFY(!service.action("delete", {{"id", "1"}}));
        QVERIFY(service.action("delete", {{"id", "1"}, {"confirmed", true}})); QTRY_COMPARE(service.items().size(), 2); QTRY_VERIFY(!service.busy());
        QVERIFY(!service.action("wipe")); QVERIFY(!service.action("wipe", {{"confirmed", 1}})); QVERIFY(service.action("wipe", {{"confirmed", true}}));
        QTRY_VERIFY(service.items().isEmpty()); QTRY_VERIFY(!service.busy());
        service.closeView(); QVERIFY(service.previewImage().isNull()); QVERIFY(service.preview().isEmpty());
    }
    void failuresAndBounds() {
        QTemporaryDir temp; const auto root = temp.path();
        ClipboardService service; auto config = module(root); service.configure(config); service.openView();
        QVERIFY(!service.available()); QVERIFY(!QFile::exists(root + "/db")); QVERIFY(!QFile::exists(root + "/calls"));
        QVERIFY(write(root + "/db", {})); QVERIFY(write(root + "/list", "1\tmissing\n"));
        service.refresh(); QTRY_COMPARE(service.items().size(), 1); QTRY_VERIFY(!service.busy());
        QVERIFY(service.action("copy", {{"id", "1"}})); QTRY_VERIFY(!service.busy()); QVERIFY(!service.available());
        QVERIFY(!QFile::exists(root + "/copied"));
        auto options = config.value("behavior").toMap(); options["cliphist_command"] = QVariantList{SERVICE_FIXTURE, "flood"}; options["max_bytes"] = 1024;
        config["behavior"] = options; service.configure(config); service.openView();
        QTRY_VERIFY(service.diagnostic().contains("max_bytes")); QVERIFY(!service.available());
        options["cliphist_command"] = QVariantList{SERVICE_FIXTURE, "sleep"}; options["timeout_ms"] = 100;
        config["behavior"] = options; service.configure(config); service.openView(); QTRY_VERIFY(service.diagnostic().contains("timed out"));
        options["cliphist_command"] = QVariantList{root + "/missing-executable"};
        config["behavior"] = options; service.configure(config); service.openView();
        QTRY_VERIFY(service.diagnostic().contains("could not be started"));
        service.closeView(); QVERIFY(service.items().isEmpty());
    }
    void oversizeImageAndDisabledActions() {
        QTemporaryDir temp; const auto root = temp.path();
        QVERIFY(write(root + "/db", {})); QVERIFY(write(root + "/list", "1\timage\n"));
        QImage image(64, 64, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(root + "/1", "PNG"));
        auto config = module(root); auto options = config.value("behavior").toMap();
        options["max_image_pixels"] = 1024; options["allow_actions"] = false; config["behavior"] = options;
        ClipboardService service; service.configure(config); service.openView(); QTRY_VERIFY(service.available()); QTRY_VERIFY(!service.busy());
        service.previewItem("1"); QTRY_VERIFY(service.preview().contains("kind"));
        QCOMPARE(service.preview().value("kind").toString(), "binary"); QVERIFY(service.previewImage().isNull());
        QVERIFY(!service.action("copy", {{"id", "1"}}));
        config["enabled"] = false; service.configure(config); QVERIFY(service.preview().isEmpty()); QVERIFY(service.items().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ClipboardTest)
#include "ClipboardTest.moc"
