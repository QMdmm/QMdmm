// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmCore/QMdmmDebug>

#include <QBuffer>
#include <QRegularExpression>
#include <QTest>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace Qt::StringLiterals;
using namespace QMdmmCore;

namespace {

// The console side of message output, the way a case can read it.
//
// The module installs a handler of its own and hands the lines it copies out to whichever handler
// was in place before it, so a case that wants to read that side puts this one there first. It has
// to be in place before the module installs its own -- that is the handler the module records as
// the one to copy out through -- and it must not be swapped in again afterwards: that would take
// the module's handler out of the chain instead of sitting behind it.
QStringList consoleLines;
bool consoleCaptured = false;
QtMessageHandler consolePrevious = nullptr;

void captureConsole(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    consoleLines.append(msg);

    if (consolePrevious != nullptr && consolePrevious != &captureConsole)
        (*consolePrevious)(type, context, msg);
    else
        qt_message_output(type, context, msg);
}

void installConsoleCapture()
{
    if (consoleCaptured)
        return;

    consolePrevious = qInstallMessageHandler(&captureConsole);
    consoleCaptured = true;
}

} // namespace

class tst_QMdmmDebug : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmDebug() = default;

    QBuffer buf;

private slots:
    // called before each test case is run
    void init()
    {
        installConsoleCapture();
        consoleLines.clear();

        buf.setData({});
        buf.open(QIODevice::ReadWrite);
    }

    // called after each test case is run
    void cleanup()
    {
        // detach from the log device before it goes out of scope, and leave the level where a run
        // that asked for nothing would have it
        qMdmmDebugSetDevice(nullptr);
        qMdmmDebugSetConsoleLevel(LogLevel::Off);
        buf.close();
    }

    void QMdmmDebugqMdmmDebugSetDevice()
    {
        qMdmmDebugSetDevice(&buf);
        qDebug("hello QMdmm");

        // messages are written to the device, one line each
        QByteArray data = buf.data();
        QVERIFY(data.contains("hello QMdmm"));
        QVERIFY(data.endsWith('\n'));

        qint64 sizeAfterFirst = data.size();
        qWarning("second message");
        data = buf.data();
        QVERIFY(data.contains("second message"));
        QVERIFY(data.size() > sizeAfterFirst);
        QCOMPARE(data.count('\n'), 2);
    }

    void QMdmmDebugopensDeviceIfNotOpen()
    {
        QBuffer closedBuf;
        QVERIFY(!closedBuf.isOpen());

        qMdmmDebugSetDevice(&closedBuf);
        QVERIFY(closedBuf.isOpen());
        QVERIFY(closedBuf.isWritable());

        qDebug("opened by setDevice");
        QVERIFY(closedBuf.data().contains("opened by setDevice"));

        qMdmmDebugSetDevice(nullptr);
    }

    void QMdmmDebugunsetDevice()
    {
        qMdmmDebugSetDevice(&buf);
        qDebug("before unset");
        QVERIFY(buf.data().contains("before unset"));

        QByteArray dataBeforeUnset = buf.data();

        // after unsetting, messages fall back to Qt's own handler and must not reach the device
        qMdmmDebugSetDevice(nullptr);
        qDebug("after unset");
        QCOMPARE(buf.data(), dataBeforeUnset);
    }

    void QMdmmDebugdeletedDeviceIsNotUsed()
    {
        // the log device is held by QPointer, so a destroyed device must not be written to
        {
            QBuffer temporaryBuf;
            temporaryBuf.open(QIODevice::ReadWrite);
            qMdmmDebugSetDevice(&temporaryBuf);
            qDebug("while alive");
            QVERIFY(temporaryBuf.data().contains("while alive"));
        }

        // must not crash: falls back to the default handler
        qDebug("after device destroyed");
    }

    void QMdmmDebugswitchDevice()
    {
        QBuffer buf2;
        buf2.open(QIODevice::ReadWrite);

        qMdmmDebugSetDevice(&buf);
        qDebug("to first");

        qMdmmDebugSetDevice(&buf2);
        qDebug("to second");

        QVERIFY(buf.data().contains("to first"));
        QVERIFY(!buf.data().contains("to second"));
        QVERIFY(buf2.data().contains("to second"));
        QVERIFY(!buf2.data().contains("to first"));

        qMdmmDebugSetDevice(nullptr);
    }

    void QMdmmDebugmessageTypes()
    {
        qMdmmDebugSetDevice(&buf);

        qDebug("a debug message");
        qInfo("an info message");
        qWarning("a warning message");
        qCritical("a critical message");

        QByteArray data = buf.data();
        QVERIFY(data.contains("a debug message"));
        QVERIFY(data.contains("an info message"));
        QVERIFY(data.contains("a warning message"));
        QVERIFY(data.contains("a critical message"));
        QCOMPARE(data.count('\n'), 4);
    }

    void QMdmmDebuglinesCarryTimeLevelAndCategory()
    {
        qMdmmDebugSetDevice(&buf);

        qDebug("a debug line");
        qInfo("an info line");
        qWarning("a warning line");

        const QStringList lines = QString::fromUtf8(buf.data()).split(u'\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 3);

        // <time> <level> <category>: <message>, with the time in ISO 8601.
        static const QRegularExpression lineFormat(uR"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3} \S+ \S+: )"_s);

        for (const QString &line : lines)
            QVERIFY2(lineFormat.match(line).hasMatch(), qPrintable(line));

        QVERIFY(lines.at(0).endsWith(u"debug default: a debug line"_s));
        QVERIFY(lines.at(1).endsWith(u"info default: an info line"_s));
        QVERIFY(lines.at(2).endsWith(u"warning default: a warning line"_s));
    }

    // What the terminal is told is the level's business; what the file is told is not. Each row
    // asks both halves: the four lines reach the device whatever the level says, and the console
    // side holds exactly the ones at or above the level.
    void QMdmmDebugconsoleLevel_data()
    {
        QTest::addColumn<int>("level");
        QTest::addColumn<bool>("debugCopied");
        QTest::addColumn<bool>("infoCopied");
        QTest::addColumn<bool>("warningCopied");
        QTest::addColumn<bool>("criticalCopied");

        // The level goes over the column as a number: the enum is not a metatype, and the rows are
        // read by the case as the level they name.
        QTest::newRow("off") << static_cast<int>(LogLevel::Off) << false << false << false << false;
        QTest::newRow("debug") << static_cast<int>(LogLevel::Debug) << true << true << true << true;
        QTest::newRow("info") << static_cast<int>(LogLevel::Info) << false << true << true << true;
        QTest::newRow("warning") << static_cast<int>(LogLevel::Warning) << false << false << true << true;
        QTest::newRow("critical") << static_cast<int>(LogLevel::Critical) << false << false << false << true;
    }

    void QMdmmDebugconsoleLevel()
    {
        QFETCH(int, level);
        QFETCH(bool, debugCopied);
        QFETCH(bool, infoCopied);
        QFETCH(bool, warningCopied);
        QFETCH(bool, criticalCopied);

        qMdmmDebugSetDevice(&buf);
        qMdmmDebugSetConsoleLevel(static_cast<LogLevel>(level));

        qDebug("a console debug line");
        qInfo("a console info line");
        qWarning("a console warning line");
        qCritical("a console critical line");

        // The record keeps every line, whichever ones the terminal was told about.
        const QByteArray data = buf.data();
        QVERIFY(data.contains("a console debug line"));
        QVERIFY(data.contains("a console info line"));
        QVERIFY(data.contains("a console warning line"));
        QVERIFY(data.contains("a console critical line"));

        QCOMPARE(consoleLines.contains(u"a console debug line"_s), debugCopied);
        QCOMPARE(consoleLines.contains(u"a console info line"_s), infoCopied);
        QCOMPARE(consoleLines.contains(u"a console warning line"_s), warningCopied);
        QCOMPARE(consoleLines.contains(u"a console critical line"_s), criticalCopied);
    }

    // With no device the console is the only place a line has to go, so nothing is filtered out of
    // it: that is what handing the device back means, and it is what a run that could not open its
    // log file keeps doing.
    void QMdmmDebugnoDeviceKeepsEverythingOnTheConsole()
    {
        qMdmmDebugSetDevice(nullptr);
        qMdmmDebugSetConsoleLevel(LogLevel::Critical);

        qDebug("a line with no record behind it");

        QVERIFY(consoleLines.contains(u"a line with no record behind it"_s));
    }

    // The names --log-level takes are read here rather than in each program that carries the
    // option: one table, so a name added to the level reaches both programs at once. Case is not
    // part of a name, and a word no level has to be nothing rather than some level.
    void QMdmmDebuglogLevelFromString_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("level");

        // -1 stands for "no level", the way the other table carries the enum over as a number.
        QTest::newRow("off") << u"off"_s << static_cast<int>(LogLevel::Off);
        QTest::newRow("debug") << u"debug"_s << static_cast<int>(LogLevel::Debug);
        QTest::newRow("info, in capitals") << u"INFO"_s << static_cast<int>(LogLevel::Info);
        QTest::newRow("warning, mixed case") << u"Warning"_s << static_cast<int>(LogLevel::Warning);
        QTest::newRow("critical") << u"critical"_s << static_cast<int>(LogLevel::Critical);
        QTest::newRow("a word no level has") << u"chatty"_s << -1;
        QTest::newRow("nothing at all") << QString() << -1;
    }

    void QMdmmDebuglogLevelFromString()
    {
        QFETCH(QString, name);
        QFETCH(int, level);

        const std::optional<LogLevel> read = qMdmmDebugLogLevelFromString(name);

        QCOMPARE(read.has_value(), level >= 0);
        if (read.has_value())
            QCOMPARE(static_cast<int>(read.value()), level);
    }
};

namespace {
// Not _a: the void ** parameter moc gives the qt_static_metacall() it generates is named _a,
// and MSVC's /W4 reports that parameter as hiding this global (C4459).
RegisterTestObject<tst_QMdmmDebug> _;
} // namespace
#include "tst_qmdmmdebug.moc"

// NOLINTEND
