// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmsettings.h"
#include "qmdmmsettings_p.h"

#include <QGlobalStatic>
#include <QSettings>

/**
 * @file qmdmmsettings.h
 * @brief Contains definitions of settings.
 */

namespace QMdmmCore {

#ifndef DOXYGEN
namespace v0 {
#endif

#ifndef DOXYGEN

// This can't be simply put into the private source file, since the instance is static so...

namespace {
// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks, cppcoreguidelines-avoid-non-const-global-variables): Q_GLOBAL_STATIC idiom
Q_GLOBAL_STATIC(p::SettingsP, d)
} // namespace

#endif

/**
 * @class Settings
 * @ingroup QMdmmCore
 * @brief The configuration files maintained by MDMM game or system
 *
 * This setting class maintains 3 configuration instance, in where one for global default (@c Settings::Global),
 * another one for per-user default (@c Settings::PerUser), and remained one for user specified (@c Settings::Specified).
 *
 * Global default and per-user default configurations are loaded when initialized. The specified one is initialized empty.
 *
 * The setter only sets the specified one. Other settings should be explicitly saved.
 *
 * The specified one is read by default, then the per-user default one, last the global default one.
 */

/**
 * @enum Settings::Instance
 * @ingroup QMdmmCore
 * @brief The identifier of the 3 instances
 */

/**
 * @var Settings::Instance Settings::Global
 * @brief Global default configuration
 *
 * @var Settings::Instance Settings::PerUser
 * @brief Per-user default configuration
 *
 * @var Settings::Instance Settings::Specified
 * @brief User specified configuration
 */

/**
 * @brief ctor.
 */
Settings::Settings() = default;

/**
 * @brief dtor.
 */
Settings::~Settings() = default;

/**
 * @brief Save the specified configuration into the given instance
 * @param instance The instance to be saved, must be @c Settings::Global or @c Settings::PerUser
 * @return the status as <tt>QSettings::Status</tt>
 *
 * This is the only method to modify global / per-user settings, which @c setValue() don't.
 *
 * The entire @c Settings::Specified map is flushed into @p instance verbatim. Every key the
 * specified instance currently holds is written out (including transient keys that QMdmm does
 * not interpret anywhere), because no filtering between recognized and stray keys is performed.
 *
 * @warning Calling this method while one or more group is active is undefined behavior.
 */
QSettings::Status Settings::saveConfig(Instance instance)
{
    Q_ASSERT(instance != Specified);

    return d->saveConfig(instance);
}

/**
 * @brief Set the specified value
 * @param key Key.
 * @param value Value.
 *
 * This can only modify modifies @c Settings::Specified one.
 *
 * @note To prevent unwanted configuration file saving, <tt>setValue(Instance)</tt> won't be implemented or supported.
 */
void Settings::setValue(const QString &key, const QVariant &value)
{
    d->specifiedConfig->setValue(key, value);
}

/**
 * @brief Get a value by key
 * @param key Key.
 * @param defaultValue The default value to be returned if the key does not exist on all 3 instances
 * @return The set value.
 */
QVariant Settings::value(const QString &key, const QVariant &defaultValue) const
{
    return d->specifiedConfig->value(key, d->userConfig->value(key, d->globalConfig->value(key, defaultValue)));
}

/**
 * @brief Get a value by key from a specific instance
 * @param instance The specified instance
 * @param key Key.
 * @param defaultValue The default value to be returned if the key does not exist
 * @return The set value.
 */
QVariant Settings::value(Instance instance, const QString &key, const QVariant &defaultValue) const
{
    switch (instance) {
    case Global:
        return d->globalConfig->value(key, defaultValue);
    case PerUser:
        return d->userConfig->value(key, defaultValue);
    case Specified:
        return d->specifiedConfig->value(key, defaultValue);
    default:
        break;
    }

    Q_UNREACHABLE();
    return {};
}

/**
 * @brief Replicates @c QSettings::beginGroup() on all 3 configuration instances
 * @param prefix
 *
 * @note To reduce desync, neither <tt>beginGroup(Instance)</tt> nor <tt>endGroup(Instance)</tt> will implemented or supported.
 */
void Settings::beginGroup(const QString &prefix)
{
    d->globalConfig->beginGroup(prefix);
    d->userConfig->beginGroup(prefix);
    d->specifiedConfig->beginGroup(prefix);
}

/**
 * @brief Replicates @c QSettings::endGroup() on all 3 configuration instances
 *
 * @note To reduce desync, neither <tt>beginGroup(Instance)</tt> nor <tt>endGroup(Instance)</tt> will implemented or supported.
 */
void Settings::endGroup()
{
    d->specifiedConfig->endGroup();
    d->userConfig->endGroup();
    d->globalConfig->endGroup();
}

/**
 * @brief Judge if a specific key is configured in either of the instances
 * @param key Key.
 * @return If @c key is set
 */
bool Settings::contains(const QString &key) const
{
    return d->specifiedConfig->contains(key) || d->userConfig->contains(key) || d->globalConfig->contains(key);
}

/**
 * @brief Judge if a specific key is configured in the specified instance
 * @param instance The specified instance
 * @param key Key.
 * @return If @c key is set
 */
bool Settings::contains(Instance instance, const QString &key) const
{
    switch (instance) {
    case Global:
        return d->globalConfig->contains(key);
    case PerUser:
        return d->userConfig->contains(key);
    case Specified:
        return d->specifiedConfig->contains(key);
    default:
        break;
    }

    Q_UNREACHABLE();
    return false;
}

#ifndef DOXYGEN
} // namespace v0
#endif

} // namespace QMdmmCore
