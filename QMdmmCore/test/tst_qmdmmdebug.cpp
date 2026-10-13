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
        buf.setData({});
        buf.open(QIODevice::ReadWrite);
    }

    // called after each test case is run
    void cleanup()
    {
        // detach from the log device before it goes out of scope
        qMdmmDebugSetDevice(nullptr);
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
};

namespace {
// Not _a: the void ** parameter moc gives the qt_static_metacall() it generates is named _a,
// and MSVC's /W4 reports that parameter as hiding this global (C4459).
RegisterTestObject<tst_QMdmmDebug> _;
} // namespace
#include "tst_qmdmmdebug.moc"

// NOLINTEND
