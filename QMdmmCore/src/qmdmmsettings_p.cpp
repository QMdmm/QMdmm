// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmsettings_p.h"
#include "qmdmmsettings.h"

#include <QDir>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace QMdmmCore {

namespace p {

// for reading global / per-user configuration, QSettings is the suitable way
// for specified configuration, QVariantMap is more proper since QSettings always autosaves configuration (This is not desired behavior!)

// QSettings is NOT some type of QVariantMap, where it supports grouping, and some API names are different (insert vs. setValue)
// But it is needed to provide a unique API on top of QVariantMap and QSettings, where the API name difference should go away.
// For QVariantMap the grouping should be implemented by ourselves (not so complex, a few lines of code only).

// The unique API is following QMdmmCore::p::SettingsWrapperP

SettingsWrapperP::~SettingsWrapperP() = default;

SettingsWrapperP_QSettings::SettingsWrapperP_QSettings(const QString &organization, const QString &application)
    : settings(organization, application)
{
}

SettingsWrapperP_QSettings::SettingsWrapperP_QSettings(QSettings::Scope scope, const QString &organization, const QString &application)
    : settings(scope, organization, application)
{
}

SettingsWrapperP_QSettings::SettingsWrapperP_QSettings(QSettings::Format format, QSettings::Scope scope, const QString &organization, const QString &application)
    : settings(format, scope, organization, application)
{
}

SettingsWrapperP_QSettings::SettingsWrapperP_QSettings(const QString &fileName, QSettings::Format format)
    : settings(fileName, format)
{
}

SettingsWrapperP_QSettings::SettingsWrapperP_QSettings() = default;

SettingsWrapperP_QSettings::SettingsWrapperP_QSettings(QSettings::Scope scope)
    : settings(scope)
{
}

SettingsWrapperP_QSettings::~SettingsWrapperP_QSettings() = default;

void SettingsWrapperP_QSettings::setValue(const QString &key, const QVariant &value)
{
    settings.setValue(key, value);
}

QVariant SettingsWrapperP_QSettings::value(const QString &key, const QVariant &defaultValue) const
{
    return settings.value(key, defaultValue);
}

void SettingsWrapperP_QSettings::beginGroup(const QString &prefix)
{
    settings.beginGroup(prefix);
}

void SettingsWrapperP_QSettings::endGroup()
{
    settings.endGroup();
}

QString SettingsWrapperP_QSettings::group() const
{
    return settings.group();
}

bool SettingsWrapperP_QSettings::contains(const QString &key) const
{
    return settings.contains(key);
}

SettingsWrapperP_QVariantMap::~SettingsWrapperP_QVariantMap() = default;

void SettingsWrapperP_QVariantMap::setValue(const QString &key, const QVariant &value)
{
    map.insert(keyWithGroup(key), value);
}

QVariant SettingsWrapperP_QVariantMap::value(const QString &key, const QVariant &defaultValue) const
{
    return map.value(keyWithGroup(key), defaultValue);
}

void SettingsWrapperP_QVariantMap::beginGroup(const QString &prefix)
{
    currentGroup.append(prefix);
}

void SettingsWrapperP_QVariantMap::endGroup()
{
    Q_ASSERT(!currentGroup.isEmpty());
    currentGroup.removeLast();
}

QString SettingsWrapperP_QVariantMap::group() const
{
    return currentGroup.join(u"/"_s);
}

bool SettingsWrapperP_QVariantMap::contains(const QString &key) const
{
    return map.contains(keyWithGroup(key));
}

QString SettingsWrapperP_QVariantMap::keyWithGroup(const QString &key) const
{
    QStringList groupPlusKey = currentGroup;
    groupPlusKey.append(key);

    return groupPlusKey.join(u"/"_s);
}

namespace {
struct InitializeQSettings
{
    InitializeQSettings()
    {
        // A QSettings constructor that takes a Scope always goes to the native format -- the
        // call below does not reach it, and the format is what decides where a file lives. So
        // the two instances built in SettingsP name IniFormat explicitly: without that, both
        // paths set here are never used and the per-user configuration lands in the native
        // store instead (on macOS a plist under ~/Library/Preferences).
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, u"" QMDMM_CONFIGURATION_PREFIX ""_s);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::home().absoluteFilePath(u".QMdmm"_s));
    }
    ~InitializeQSettings() = default;
    Q_DISABLE_COPY_MOVE(InitializeQSettings);
};
} // namespace

SettingsP::SettingsP()
    : globalConfig(nullptr)
    , userConfig(nullptr)
    , specifiedConfig(nullptr)
{
    static InitializeQSettings initializeQSettings;

    // The format is named on purpose: it, not setDefaultFormat(), is what routes these two
    // through the paths InitializeQSettings sets.
    globalConfig = std::make_unique<SettingsWrapperP_QSettings>(QSettings::IniFormat, QSettings::SystemScope, u"Fsu0413.me"_s, u"QMdmm"_s);
    userConfig = std::make_unique<SettingsWrapperP_QSettings>(QSettings::IniFormat, QSettings::UserScope, u"Fsu0413.me"_s, u"QMdmm"_s);
    specifiedConfig = std::make_unique<SettingsWrapperP_QVariantMap>();
}

SettingsP::~SettingsP() = default;

// NOLINTNEXTLINE(readability-make-member-function-const)
QSettings::Status SettingsP::saveConfig(Settings::Instance instance)
{
    Q_ASSERT((instance == Settings::Global) || (instance == Settings::PerUser));

    QSettings *toBeSaved = nullptr;

    switch (instance) {
    case Settings::Global:
        toBeSaved = &globalConfig->settings;
        break;
    case Settings::PerUser:
        toBeSaved = &userConfig->settings;
        break;
    default:
        break;
    }

    Q_ASSERT(toBeSaved != nullptr);

    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    if (!toBeSaved->isWritable()) {
        qWarning("Setting file is not writable. Saving the file may fail. "
                 "PerUser configuration file overrides the Global one. You might use PerUser instead.");
    }

    // The specified config is a flat map whose keys must land at the top
    // level, so the target QSettings must have no current group. This runs
    // single-threaded right before process exit (only caller is Config::save_),
    // so there is no race to lock against; assert the empty-group precondition
    // rather than pop/restore the group, which would not be thread-safe.
    Q_ASSERT(toBeSaved->group().isEmpty());

    foreach (const QString &key, specifiedConfig->map.keys())
        toBeSaved->setValue(key, specifiedConfig->value(key, {}));

    // A file backend writes on sync, not on setValue; until then status() reports NoError
    // just as well. The one caller saves right before exiting with the returned status, and
    // std::exit() runs no destructor -- without this sync the configuration would be
    // silently dropped while the run still looks like a successful save.
    toBeSaved->sync();

    return toBeSaved->status();
}

} // namespace p

} // namespace QMdmmCore
