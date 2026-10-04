// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmCore/QMdmmSettings>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#ifdef QMDMM_MACOS_APP_BUNDLE
#include <QStandardPaths>
#endif

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;

class tst_QMdmmSettings : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmSettings() = default;

private slots:
    void initTestCase();

    void perUserConfigurationIsAnIniFileUnderTheHomeDirectory();
    void globalConfigurationIsAnIniFileUnderSystemDirectory();
    void configurationDirectoryIsResolvedAgainstTheProcessNotTheWorkingDirectory();
    void runtimeDataDirectoryIsResolvedAlongsideTheConfigurationDirectory();
    void valueAndContainsReadEveryInstance();

private:
    QTemporaryDir home;
};

// Settings resolves QDir::home() once, when the first Settings object of this process is
// built, and the per-user path is derived from it there. So the home has to move before any
// case runs; nothing in this file touches Settings before this hook has returned.
void tst_QMdmmSettings::initTestCase()
{
    QVERIFY(home.isValid());
    qputenv("HOME", home.path().toLocal8Bit());
    QCOMPARE(QDir::homePath(), home.path());
}

void tst_QMdmmSettings::perUserConfigurationIsAnIniFileUnderTheHomeDirectory()
{
    // The keys are arbitrary: this case is about where the configuration file goes, not
    // about what the game puts in it.
    Settings settings;
    settings.beginGroup(u"logic"_s);
    settings.setValue(u"slash"_s, 3);
    settings.endGroup();
    QCOMPARE(static_cast<int>(settings.saveConfig(Settings::PerUser)), static_cast<int>(QSettings::NoError));

    // Only the INI side is pinned. The native side cannot be observed from here: it is
    // written through cfprefsd, which resolves against the logged-in user rather than this
    // process' HOME, so a regression to the native store leaves no trace under the temporary
    // home at all -- a negative assertion about it would hold no matter what the code does.
    const QString iniFile = home.filePath(u".QMdmm/Fsu0413.me/QMdmm.ini"_s);
    QVERIFY(QFile::exists(iniFile));
    QCOMPARE(QSettings(iniFile, QSettings::IniFormat).value(u"logic/slash"_s).toInt(), 3);
}

// The other half of the pair: the global instance goes through the system scope. Which
// directory that is depends on where the tree was installed -- Global::configurationDirectory()
// documents the rule -- so this one can still only say something on a machine where that
// directory can be created and written, and it skips rather than fails where it cannot: an
// installation under /usr is the ordinary case of that, and there is nothing to assert on it.
//
// In the bundle shape the directory is the platform's own, which on this platform means the
// real home even when this process was given another one: the file below is put back, but the
// directory tree it lives in is left behind, the same as a real run of the bundle would.
void tst_QMdmmSettings::globalConfigurationIsAnIniFileUnderSystemDirectory()
{
    QDir prefix(Global::configurationDirectory());

    if (!prefix.mkpath(u"."_s))
        QSKIP("the system configuration directory cannot be created here");

    if (!QFileInfo(prefix.absolutePath()).isWritable())
        QSKIP("the system configuration directory is not writable here");

    // The file name QSettings derives from that directory for the system scope: the same
    // organization and application the per-user case above pins, under the configured
    // directory instead of the home.
    const QString iniFile = prefix.absoluteFilePath(u"Fsu0413.me/QMdmm.ini"_s);

    // This is a directory on the machine rather than a temporary one, so what was in the
    // file is put back however this case ends -- including when an assertion below fails
    // and returns early. A value left behind would be read by every later run of the server
    // on this machine.
    QFile existing(iniFile);
    const bool hadFile = existing.exists();
    QByteArray previous;
    if (hadFile) {
        QVERIFY(existing.open(QIODevice::ReadOnly));
        previous = existing.readAll();
        existing.close();
    }
    const auto putBack [[maybe_unused]] = qScopeGuard([iniFile, hadFile, previous] {
        if (!hadFile) {
            QFile::remove(iniFile);
            return;
        }

        QFile file(iniFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(previous);
    });

    {
        // The keys are arbitrary: this case is about where the configuration file goes, not
        // about what the game puts in it.
        Settings settings;
        settings.beginGroup(u"logic"_s);
        settings.setValue(u"slash"_s, 3);
        settings.endGroup();
        QCOMPARE(static_cast<int>(settings.saveConfig(Settings::Global)), static_cast<int>(QSettings::NoError));
    }

    // Asserting this file name is what ties the two halves of the case together: that the
    // global instance is written as an INI file at all -- a regression to the native store
    // would leave this file unwritten and put the value in the platform's own preferences
    // instead -- and that it goes into the directory the build configured.
    QVERIFY(QFile::exists(iniFile));
    QCOMPARE(QSettings(iniFile, QSettings::IniFormat).value(u"logic/slash"_s).toInt(), 3);
}

// Which directory that is comes from the build and the machine rather than being written
// down, and the rule is stated a second time here on purpose: the cases above read the
// directory back through the same accessor that writes it, so they would agree with a
// resolver that named the wrong place. This one cannot -- it derives the expected path from
// the definitions and the executable, or, in the bundle, from the platform's own application
// directory the way the code does.
//
// The lookup is made once per process, and the cases above have made it long before this one
// runs, so moving the working directory here and asking again would only compare the stored
// answer with itself. What pins which of the two the definition was resolved against is
// therefore a comparison with an independently derived path, and not a before-and-after --
// and it tells the two apart only while the executable does not sit in the working directory,
// which is why the last check below is about this case being able to say anything at all,
// rather than about the code.
void tst_QMdmmSettings::configurationDirectoryIsResolvedAgainstTheProcessNotTheWorkingDirectory()
{
#ifdef QMDMM_MACOS_APP_BUNDLE
    const QString expected = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/"_s + u"" QMDMM_MACOS_BUNDLE_IDENTIFIER ""_s + u"/etc"_s);
#else
    const QString configured = u"" QMDMM_CONFIGURATION_PREFIX ""_s;

    // No shape names a directory under the home directory any more: what is not absolute is
    // relative to the executable.
    const QString expected = configured.startsWith(u'/') ? configured : QDir::cleanPath(QCoreApplication::applicationDirPath() + u"/"_s + configured);
#endif

    QVERIFY(QDir::isAbsolutePath(expected));
    QCOMPARE(Global::configurationDirectory(), expected);

#ifndef QMDMM_MACOS_APP_BUNDLE
    // An absolute definition is the directory itself and has no anchor to tell apart. For the
    // relative ones the same definition counted from the working directory would name another
    // place, unless the executable sits there -- which is the one case where the comparison
    // above holds whichever of the two the resolver was anchored on.
    if (!configured.startsWith(u'/')) {
        const QString againstTheWorkingDirectory = QDir::cleanPath(QDir::currentPath() + u"/"_s + configured);
        QVERIFY2(Global::configurationDirectory() != againstTheWorkingDirectory, "the working directory is the executable's own, so this case cannot tell the two anchors apart");
    }
#endif
}

// The runtime data directory is the configuration one's rule with var written for etc -- the
// same prefix, the same bundle -- and it carries one step the other has not got: where that
// directory cannot be created and written, the run falls back to $HOME/.QMdmm/var rather than
// having nowhere to report from. That fallback is the half worth a case of its own, because a
// run whose logs move elsewhere, or stop being written at all, says nothing about it by itself.
// Both the path and the fallback below are derived from the definitions and the executable
// rather than read back through the accessor, which would agree with a resolver that named the
// wrong place just as readily.
//
// Which of the two this case ends on is a property of where the tree is installed, not of the
// code: an ordinary build answers with the directory the definitions name, an installation
// under a prefix this process cannot write to answers with the fallback. Both answers are
// pinned here; only one of them is reachable on a machine whose build tree belongs to it.
void tst_QMdmmSettings::runtimeDataDirectoryIsResolvedAlongsideTheConfigurationDirectory()
{
#ifdef QMDMM_MACOS_APP_BUNDLE
    const QString expected = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/"_s + u"" QMDMM_MACOS_BUNDLE_IDENTIFIER ""_s + u"/var"_s);
#else
    const QString configured = u"" QMDMM_RUNTIME_DATA_PREFIX ""_s;

    const QString expected = configured.startsWith(u'/') ? configured : QDir::cleanPath(QCoreApplication::applicationDirPath() + u"/"_s + configured);
#endif

    QVERIFY(QDir::isAbsolutePath(expected));

    // One rule, two names: the configuration directory with its etc segment written var. The
    // segment is matched rather than the string "/etc/", because the bundle shape ends right
    // after that segment while the prefix shape has one more segment behind it.
    const QString alongsideTheConfiguration = QString(Global::configurationDirectory()).replace(QRegularExpression(u"/etc(?=/|$)"_s), u"/var"_s);

    if (QDir().mkpath(expected) && QFileInfo(expected).isWritable()) {
        QCOMPARE(Global::runtimeDataDirectory(), expected);
        QCOMPARE(Global::runtimeDataDirectory(), alongsideTheConfiguration);
        return;
    }

    // An installation under a prefix this process cannot write to -- /usr is the ordinary shape
    // of that -- takes the second attempt instead, and the same directory the resolver picks.
    QCOMPARE(Global::runtimeDataDirectory(), QDir::home().absoluteFilePath(u".QMdmm/var"_s));
}

// The cases above pin where two of the three instances live; this one pins that they are
// read at all. The lookup is a chain -- what the program set, then the user's file, then the
// machine's -- and nothing else in the tree calls value() or contains(), so without this case
// the whole reading half of this class would be exercised by nothing.
void tst_QMdmmSettings::valueAndContainsReadEveryInstance()
{
    Settings settings;

    // A key nothing has written: the caller's default is what comes back from every instance,
    // and every instance reports that it does not have the key.
    const QString absent = u"aKeyNobodySet"_s;
    QCOMPARE(settings.value(absent, 11).toInt(), 11);
    QCOMPARE(settings.value(Settings::Specified, absent, 11).toInt(), 11);
    QCOMPARE(settings.value(Settings::PerUser, absent, 11).toInt(), 11);
    QCOMPARE(settings.value(Settings::Global, absent, 11).toInt(), 11);
    QVERIFY(!settings.contains(absent));
    QVERIFY(!settings.contains(Settings::Specified, absent));
    QVERIFY(!settings.contains(Settings::PerUser, absent));
    QVERIFY(!settings.contains(Settings::Global, absent));

    // setValue touches the specified instance alone, so the other two do not have the key
    // until saveConfig puts it on disk -- and the specified instance has it from the start.
    const QString key = u"aKeySetHereOnly"_s;
    settings.setValue(key, 3);
    QCOMPARE(settings.value(key).toInt(), 3);
    QCOMPARE(settings.value(Settings::Specified, key).toInt(), 3);
    QVERIFY(settings.contains(key));
    QVERIFY(settings.contains(Settings::Specified, key));
    QVERIFY(!settings.contains(Settings::PerUser, key));

    QCOMPARE(static_cast<int>(settings.saveConfig(Settings::PerUser)), static_cast<int>(QSettings::NoError));
    QCOMPARE(settings.value(Settings::PerUser, key).toInt(), 3);
    QVERIFY(settings.contains(Settings::PerUser, key));

    // The one step of the chain that can be moved from here: the per-user file now holds 3
    // under this key, and setting 5 afterwards answers 5 from a plain lookup while the
    // per-user instance still says 3. The global step is left where it is -- moving it means
    // writing the machine's own file, which the case above already does for one key under a
    // guard that puts it back.
    settings.setValue(key, 5);
    QCOMPARE(settings.value(Settings::Specified, key).toInt(), 5);
    QCOMPARE(settings.value(Settings::PerUser, key).toInt(), 3);
    QCOMPARE(settings.value(key).toInt(), 5);
}

namespace {
RegisterTestObject<tst_QMdmmSettings> _a;
} // namespace
#include "tst_qmdmmsettings.moc"

// NOLINTEND
