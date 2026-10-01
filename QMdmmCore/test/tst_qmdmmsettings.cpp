// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmCore/QMdmmSettings>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    // A resolver anchored on the working directory passes wherever the two happen to coincide
    // -- a build tree is one such place, because the tests run from within it -- and breaks the
    // moment the program is started from elsewhere, which is the ordinary way to start an
    // installed one. Moving the working directory has to change nothing.
    const QString previous = QDir::currentPath();
    const auto restore [[maybe_unused]] = qScopeGuard([previous] { QDir::setCurrent(previous); });
    QVERIFY(QDir::setCurrent(QDir::rootPath()));
    QCOMPARE(Global::configurationDirectory(), expected);
}

namespace {
RegisterTestObject<tst_QMdmmSettings> _a;
} // namespace
#include "tst_qmdmmsettings.moc"

// NOLINTEND
