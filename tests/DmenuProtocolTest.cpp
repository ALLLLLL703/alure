#include "DmenuProtocol.h"
#include <QtTest>
using namespace Alure;
class DmenuProtocolTest : public QObject {
    Q_OBJECT
private slots:
    void framing() {
        QByteArray input = QString("  'raw 雪'  ").toUtf8();
        input += '\0'; input += "display\x1f"; input += QString("人間").toUtf8();
        input += "\x1fmeta\x1ftrees\x1ficon\x1f/tmp/icon.png\x1fnonselectable\x1ftrue\x1fpermanent\x1ftrue\x1furgent\x1ftrue\x1f";
        input += "active\x1ftrue\x1funknown\x1fignored\nsecond\nsecond";
        DmenuInput parser;
        for (char c : input) QVERIFY(parser.append(QByteArray(1,c)));
        QVERIFY(parser.finish()); QCOMPARE(parser.rows().size(), 3);
        const auto row = parser.rows()[0];
        QCOMPARE(row.raw, "  'raw 雪'  "); QCOMPARE(row.display, "人間"); QCOMPARE(row.meta, "trees");
        QVERIFY(!row.selectable); QVERIFY(row.permanent); QVERIFY(row.urgent); QVERIFY(row.active);
        QCOMPARE(parser.rows()[2].index, 2);
        QVERIFY(dmenuMatches(row, "absent", true, false));
        QCOMPARE(dmenuOutput("i:d:s", &parser.rows()[2], "sec"), "2:3:second\n");
    }
    void formatsAndSearch() {
        DmenuRow row; row.raw = "  a'b  "; row.display = "Blue"; row.meta = "azure"; row.index = 9;
        QCOMPARE(dmenuOutput("s|i|d|q|f|F", &row, "x'y"), "  a'b  |9|10|'  a'\\''b  '|x'y|'x'\\''y'\n");
        QCOMPARE(dmenuOutput("i:d:s", nullptr, "typed"), "-1:0:typed\n");
        QVERIFY(dmenuMatches(row, "AZURE", false, false)); QVERIFY(!dmenuMatches(row,"Blue",true,false));
        QVERIFY(dmenuMatches(row,"Blue",true,true));
    }
    void limitsAndSeparators() {
        DmenuInput input("|", 20, 3);
        QVERIFY(input.append("a||雪|")); QVERIFY(input.finish()); QCOMPARE(input.rows().size(),3);
        QCOMPARE(input.rows()[1].raw, "");
        DmenuInput nul(QByteArray(1,'\0')); QVERIFY(nul.append(QByteArray("a\0b\0",4))); QVERIFY(nul.finish()); QCOMPARE(nul.rows().size(),2);
        DmenuInput bytes("\n",2); QVERIFY(!bytes.append("abc")); QVERIFY(!bytes.finish());
        DmenuInput rows("\n",10,1); QVERIFY(!rows.append("a\nb\n"));
        DmenuInput utf; QVERIFY(!utf.append(QByteArray("\xff\n",2)));
        DmenuInput final; QVERIFY(final.append("last")); QVERIFY(final.finish()); QCOMPARE(final.rows()[0].raw,"last");
    }
    void cli() {
        DmenuOptions options; QString error;
        QVERIFY(DmenuOptions::parse({"-dmenu","-i","-p","Pick","-mesg","plain","-filter","雪","-selected-row","2","-format","i:s","-no-custom","-sep","|","-show-icons","-l","5"}, options,error));
        QCOMPARE(options.prompt,"Pick"); QCOMPARE(options.selectedRow,2); QVERIFY(options.noCustom); QVERIFY(!*options.caseSensitive); QCOMPARE(options.lines,5);
        for (const auto &args : QList<QStringList>{{"dmenu","-theme","x"},{"dmenu","-format","p"},{"dmenu","-sep","ab"},{"dmenu","-l","0"},{"dmenu","-selected-row","-1"},{"dmenu","-p"},{"wp-launcher","-i"}}) {
            DmenuOptions bad; QVERIFY(!DmenuOptions::parse(args,bad,error)); QVERIFY(!error.isEmpty());
        }
        DmenuOptions only; QVERIFY(DmenuOptions::parse({"dmenu","-only-match"},only,error)); QVERIFY(only.onlyMatch && only.noCustom);
    }
};
QTEST_GUILESS_MAIN(DmenuProtocolTest)
#include "DmenuProtocolTest.moc"
