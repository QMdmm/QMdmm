// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QMdmmCore/QMdmmSettings>

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

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

namespace {
RegisterTestObject<tst_QMdmmSettings> _a;
} // namespace
#include "tst_qmdmmsettings.moc"

// NOLINTEND
