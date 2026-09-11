#include "ConfigStore.h"
#include "PanelHost.h"
#include "PopupPlacement.h"
#include <QtTest>
class PanelHostTest : public QObject {
    Q_OBJECT
private slots:
    void osdLowerCenterPlacement() {
        QVariantMap model; QString error; QVERIFY(Alure::ConfigStore::parse({}, model, error));
        auto options = model.value("ui").toMap().value("osd").toMap();
        const auto p = Alure::osdPlacement(options, {1920, 1080});
        QCOMPARE(p.size, QSize(300, 100)); QCOMPARE(p.position, QPoint(810, 916));
        QCOMPARE(p.margins, QMargins(16, 0, 16, 64));
        options["margin_horizontal"] = 4096; options["margin_bottom"] = 4096;
        for (const auto &extent : {QSize(1, 1), QSize(240, 120), QSize(3840, 2160)}) {
            const auto clamped = Alure::osdPlacement(options, extent);
            QVERIFY(QRect(QPoint(), extent).contains(QRect(clamped.position, clamped.size)));
            QVERIFY(clamped.size.width() > 0); QVERIFY(clamped.size.height() > 0);
        }
    }
    void popupPlacement_data() {
        QTest::addColumn<QString>("edge"); QTest::addColumn<QString>("alignment"); QTest::addColumn<int>("offset");
        for (const auto &edge : {"top", "bottom", "left", "right"})
            for (const auto &alignment : {"start", "center", "end"})
                for (int offset : {0, 300, 640})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(edge, alignment).arg(offset))) << QString(edge) << QString(alignment) << offset;
    }
    void popupPlacement() {
        QFETCH(QString, edge); QFETCH(QString, alignment); QFETCH(int, offset);
        QVariantMap model; QString error; QVERIFY(Alure::ConfigStore::parse({}, model, error));
        auto ui = model.value("ui").toMap(); ui["popup_alignment"] = alignment;
        const bool vertical = edge == "left" || edge == "right";
        const QSize parent = vertical ? QSize(44, 700) : QSize(700, 44);
        const QRect clicked = vertical ? QRect(5, offset, 30, 60) : QRect(offset, 5, 60, 30);
        const auto p = Alure::popupPlacement(clicked, parent, {800, 800}, edge, ui);
        QVERIFY(QRect(QPoint(), parent).contains(p.anchorRect));
        QCOMPARE(p.padding, 8); QCOMPARE(p.size, QSize(456, 576));
        const auto direction = edge == "top" ? Qt::BottomEdge : edge == "bottom" ? Qt::TopEdge : edge == "left" ? Qt::RightEdge : Qt::LeftEdge;
        QVERIFY(p.anchor.testFlag(direction)); QVERIFY(p.gravity.testFlag(direction));
        if (edge == "top") QCOMPARE(p.position.y(), parent.height());
        if (edge == "bottom") QCOMPARE(p.position.y() + p.size.height(), 0);
        if (edge == "left") QCOMPARE(p.position.x(), parent.width());
        if (edge == "right") QCOMPARE(p.position.x() + p.size.width(), 0);
        const int along = vertical ? p.position.y() : p.position.x();
        const int length = vertical ? p.size.height() : p.size.width();
        QCOMPARE(along, alignment == "start" ? offset : alignment == "end" ? offset + 60 - length : offset + 30 - length / 2);
    }
    void popupConstraintsAndOverrides() {
        QVariantMap model; QString error; QVERIFY(Alure::ConfigStore::parse({}, model, error));
        auto ui = model.value("ui").toMap(); ui["popup_gap"] = 256;
        for (const auto &extent : {QSize(1, 1), QSize(100, 80), QSize(240, 240), QSize(3840, 2160)}) {
            for (const auto &direction : {"top", "bottom", "left", "right"}) {
                ui["popup_direction"] = direction;
                const auto p = Alure::popupPlacement({-10, -10, 60, 60}, {44, 44}, extent, "top", ui);
                QVERIFY(QRect(0, 0, 44, 44).contains(p.anchorRect));
                QVERIFY(p.size.width() <= extent.width()); QVERIFY(p.size.height() <= extent.height());
                QVERIFY(p.size.width() - 2 * p.padding >= 1); QVERIFY(p.size.height() - 2 * p.padding >= 1);
                QVERIFY(p.padding <= std::min(extent.width(), extent.height()) / 4);
            }
        }
        QVERIFY(Alure::popupPlacement({}, {44, 44}, {800, 600}, "top", ui).anchorRect.isEmpty());
        QVERIFY(Alure::popupPlacement({100, 100, 40, 40}, {44, 44}, {800, 600}, "top", ui).anchorRect.isEmpty());
    }
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
        QCOMPARE(p.exclusiveZone, 44); QCOMPARE(p.anchors.toInt(), anchor | (vertical ? 3 : 12));
        QCOMPARE(p.margins, QMargins(8, 8, 8, 8));
        QCOMPARE(p.exclusiveZone + 8, 52); // Total reservation includes the separately sent edge margin.
        panel["length"] = 500; panel["exclusive_zone"] = 0; panel["layer"] = "overlay";
        p = Alure::panelPlacement(panel, {1920, 1080});
        QCOMPARE(p.size, vertical ? QSize(44, 500) : QSize(500, 44));
        QCOMPARE(p.anchors.toInt(), anchor); QCOMPARE(p.exclusiveZone, 0);
        QCOMPARE(p.layer, LayerShellQt::Window::LayerOverlay);
        panel["length"] = 50000; panel["thickness"] = 512;
        panel["exclusive_zone"] = -1;
        panel["margins"] = QVariantMap{{"top", 3}, {"bottom", 7}, {"left", 11}, {"right", 13}};
        p = Alure::panelPlacement(panel, {100, 100}); QCOMPARE(p.size, QSize(76, 90));
        QCOMPARE(p.exclusiveZone, vertical ? 76 : 90);
        QCOMPARE(p.margins, QMargins(11, 3, 13, 7));
        panel["exclusive_zone"] = 123;
        QCOMPARE(Alure::panelPlacement(panel, {100, 100}).exclusiveZone, 123);
    }
};
QTEST_GUILESS_MAIN(PanelHostTest)
#include "PanelHostTest.moc"
