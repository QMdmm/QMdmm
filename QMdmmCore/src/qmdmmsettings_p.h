// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSETTINGS_P
#define QMDMMSETTINGS_P

#include "qmdmmsettings.h"

#include <memory>

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): This is private header

namespace QMdmmCore {

namespace p {

// the SettingsWrapperP class is meant to be exporting a unified interface for the QMdmmCore::Settings
// but it is proven to be slightly over designed since some interfaces are actually not used
// the interfaces which are not used are temporarily removed since the unit test can't cover them.
// they are marked inside #if 0 so that the original design can be seen without checking VCS

// NOLINTBEGIN(readability-avoid-unconditional-preprocessor-if)

struct QMDMMCORE_PRIVATE_EXPORT SettingsWrapperP
{
    Q_DISABLE_COPY_MOVE(SettingsWrapperP);

    SettingsWrapperP() = default;
    virtual ~SettingsWrapperP();

#if 0
    virtual void setValue(const QString &key, const QVariant &value) = 0;
#endif
    [[nodiscard]] virtual QVariant value(const QString &key, const QVariant &defaultValue = {}) const = 0;

    virtual void beginGroup(const QString &prefix) = 0;
    virtual void endGroup() = 0;
#if 0
    [[nodiscard]] virtual QString group() const = 0;
#endif

    [[nodiscard]] virtual bool contains(const QString &key) const = 0;
};

struct QMDMMCORE_PRIVATE_EXPORT SettingsWrapperP_QSettings : public SettingsWrapperP
{
    Q_DISABLE_COPY_MOVE(SettingsWrapperP_QSettings);

    QSettings settings;

#if 0
    explicit SettingsWrapperP_QSettings(const QString &organization, const QString &application = {});
    SettingsWrapperP_QSettings(QSettings::Scope scope, const QString &organization, const QString &application = {});
    SettingsWrapperP_QSettings(const QString &fileName, QSettings::Format format);
    SettingsWrapperP_QSettings();
    explicit SettingsWrapperP_QSettings(QSettings::Scope scope);
#endif
    SettingsWrapperP_QSettings(QSettings::Format format, QSettings::Scope scope, const QString &organization, const QString &application = {});
    ~SettingsWrapperP_QSettings() override;
#if 0
    void setValue(const QString &key, const QVariant &value) override;
#endif
    [[nodiscard]] QVariant value(const QString &key, const QVariant &defaultValue) const override;
    void beginGroup(const QString &prefix) override;
    void endGroup() override;
#if 0
    [[nodiscard]] QString group() const override;
#endif
    [[nodiscard]] bool contains(const QString &key) const override;
};

struct QMDMMCORE_PRIVATE_EXPORT SettingsWrapperP_QVariantMap : public SettingsWrapperP
{
    Q_DISABLE_COPY_MOVE(SettingsWrapperP_QVariantMap);

    QVariantMap map;
    QStringList currentGroup;

    SettingsWrapperP_QVariantMap() = default;
    ~SettingsWrapperP_QVariantMap() override;

#if 0
    void setValue(const QString &key, const QVariant &value) override;
#else
    void setValue(const QString &key, const QVariant &value);
#endif
    [[nodiscard]] QVariant value(const QString &key, const QVariant &defaultValue) const override;
    void beginGroup(const QString &prefix) override;
    void endGroup() override;
#if 0
    [[nodiscard]] QString group() const override;
#endif
    [[nodiscard]] bool contains(const QString &key) const override;

    [[nodiscard]] QString keyWithGroup(const QString &key) const;
};

// NOLINTEND(readability-avoid-unconditional-preprocessor-if)

struct QMDMMCORE_PRIVATE_EXPORT SettingsP
{
    Q_DISABLE_COPY_MOVE(SettingsP);

    std::unique_ptr<SettingsWrapperP_QSettings> globalConfig;
    std::unique_ptr<SettingsWrapperP_QSettings> userConfig;
    std::unique_ptr<SettingsWrapperP_QVariantMap> specifiedConfig;

    SettingsP();
    ~SettingsP();

    QSettings::Status saveConfig(Settings::Instance instance);
};

} // namespace p

} // namespace QMdmmCore

// NOLINTEND(misc-non-private-member-variables-in-classes): This is private header

#endif
