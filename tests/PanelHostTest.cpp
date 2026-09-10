#include "ConfigStore.h"
#include "PanelHost.h"
#include <QtTest>
class PanelHostTest : public QObject {
    Q_OBJECT
private slots:
    void placement_data() {
        QTest::addColumn<QString>("edge"); QTest::addColumn<bool>("vertical"); QTest::addColumn<int>("anchor");
        QTest::newRow("top") << "top" << false << 1;
        QTest::newRow("bottom") << "bottom" << false << 2;
        QTest::newRow("left") << "left" << true << 4;
        QTest::newRow("right") << "right" << true << 8;
    }
    void placement() {
        QFETCH(QString, edge); QFETCH(bool, vertical); QFETCH(int, anchor);
        QVariantMap model; QString error; QVERIFY(Alure::ConfigStore::parse({}, model, error));
        auto panel = model.value("panels").toList().first().toMap(); panel["edge"] = edge;
        auto p = Alure::panelPlacement(panel, {1920, 1080});
        QCOMPARE(p.vertical, vertical); QCOMPARE(int(p.edge), anchor);
        QCOMPARE(p.size, vertical ? QSize(44, 1064) : QSize(1904, 44));
        QCOMPARE(p.exclusiveZone, 52); QCOMPARE(p.anchors.toInt(), anchor | (vertical ? 3 : 12));
        panel["length"] = 500; panel["exclusive_zone"] = 0; panel["layer"] = "overlay";
        p = Alure::panelPlacement(panel, {1920, 1080});
        QCOMPARE(p.size, vertical ? QSize(44, 500) : QSize(500, 44));
        QCOMPARE(p.anchors.toInt(), anchor); QCOMPARE(p.exclusiveZone, 0);
        QCOMPARE(p.layer, LayerShellQt::Window::LayerOverlay);
        panel["length"] = 50000; panel["thickness"] = 512;
        p = Alure::panelPlacement(panel, {100, 100}); QCOMPARE(p.size, QSize(84, 84));
    }
};
QTEST_GUILESS_MAIN(PanelHostTest)
#include "PanelHostTest.moc"
