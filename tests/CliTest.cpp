#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>
class CliTest : public QObject {
    Q_OBJECT
    QByteArray output;
    int run(const QStringList &args, bool gui = false) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("DISPLAY"); env.remove("WAYLAND_DISPLAY");
        env.insert("QT_QPA_PLATFORM", gui ? "offscreen" : "does-not-exist");
        env.insert("QT_QUICK_BACKEND", "software");
        process.setProcessEnvironment(env); process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(ALURE_EXECUTABLE), args);
        if (!process.waitForStarted(3000) || !process.waitForFinished(8000)) {
            process.kill(); process.waitForFinished(); output = process.readAll(); return -1;
        }
        output = process.readAll();
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
    }
private slots:
    void validationAndHelp() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        QCOMPARE(run({"--validate-config", "--config", path}), 0); QVERIFY(!QFile::exists(path));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("[broken"); file.close();
        QCOMPARE(run({"--validate-config", "--config", path}), 1); QVERIFY(output.contains("config.toml"));
        QCOMPARE(run({"--help"}), 0); QVERIFY(output.contains("--preview"));
        QCOMPARE(run({"--validate-config", "unexpected.toml"}), 2);
        QCOMPARE(run({"--validate-config", "--quit-after-ms", "-1", "--config", path}), 2);
    }
    void previewRebuildsOnReload() {
        QTemporaryDir dir; const auto path = dir.filePath("config.toml");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("version = 1\n"); file.close();
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen"); env.insert("QT_QUICK_BACKEND", "software");
        process.setProcessEnvironment(env); process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(ALURE_EXECUTABLE), {"--preview", "--config", path, "--quit-after-ms", "1200"});
        QVERIFY(process.waitForStarted(3000)); QTest::qWait(250);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[[panels]]\nid = 'left'\nedge = 'left'\n[[panels]]\nid = 'bottom'\nedge = 'bottom'\n"); file.close();
        QTest::qWait(500);
        QCOMPARE(process.state(), QProcess::Running);
        QVERIFY(process.waitForFinished(3000)); QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        const auto log = process.readAll(); QVERIFY2(!log.contains("qrc:"), log.constData());
    }
    void previewAndSettings() {
        QTemporaryDir dir; const auto path = dir.filePath("missing.toml");
        for (const auto &mode : {"--preview", "--settings"}) {
            QCOMPARE(run({mode, "--config", path, "--quit-after-ms", "150"}, true), 0);
            QVERIFY2(!output.contains("Error") && !output.contains("ReferenceError") && !output.contains("TypeError") && !output.contains("failed") && !output.contains("qrc:"), output.constData());
        }
        QVERIFY(!QFile::exists(path));
        QCOMPARE(run({"--config", path, "--quit-after-ms", "150"}, true), 2);
        QVERIFY(output.contains("requires Wayland"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("[broken"); file.close();
        QCOMPARE(run({"--settings", "--config", path, "--quit-after-ms", "150"}, true), 0);
        QCOMPARE(run({"--preview", "--config", path, "--quit-after-ms", "150"}, true), 1);
    }
};
QTEST_GUILESS_MAIN(CliTest)
#include "CliTest.moc"
