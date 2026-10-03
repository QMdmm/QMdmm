// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSETTINGS_P
#define QMDMMSETTINGS_P

#include "qmdmmsettings.h"

#include <memory>

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): This is private header

namespace QMdmmCore {

namespace p {

struct QMDMMCORE_PRIVATE_EXPORT SettingsWrapperP
{
    Q_DISABLE_COPY_MOVE(SettingsWrapperP);

    SettingsWrapperP() = default;
    virtual ~SettingsWrapperP();

    virtual void setValue(const QString &key, const QVariant &value) = 0;
    [[nodiscard]] virtual QVariant value(const QString &key, const QVariant &defaultValue = {}) const = 0;

    virtual void beginGroup(const QString &prefix) = 0;
    virtual void endGroup() = 0;
    [[nodiscard]] virtual QString group() const = 0;

    [[nodiscard]] virtual bool contains(const QString &key) const = 0;
};

struct QMDMMCORE_PRIVATE_EXPORT SettingsWrapperP_QSettings : public SettingsWrapperP
{
    Q_DISABLE_COPY_MOVE(SettingsWrapperP_QSettings);

    QSettings settings;

    // The five overloads below mirror the QSettings constructor surface. Nothing in this tree
    // names them -- SettingsP builds its two QSettings-backed instances through the four-argument
    // form left outside this block -- and tests cannot reach them either (the private symbols of
    // this header are not exported unless the build asks for it). They are kept as text so the
    // mirror stays visible, but they are not compiled.
    // NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if): deliberate, see above
#if 0
    explicit SettingsWrapperP_QSettings(const QString &organization, const QString &application = {});
    SettingsWrapperP_QSettings(QSettings::Scope scope, const QString &organization, const QString &application = {});
    SettingsWrapperP_QSettings(const QString &fileName, QSettings::Format format);
    SettingsWrapperP_QSettings();
    explicit SettingsWrapperP_QSettings(QSettings::Scope scope);
#endif
    SettingsWrapperP_QSettings(QSettings::Format format, QSettings::Scope scope, const QString &organization, const QString &application = {});
    ~SettingsWrapperP_QSettings() override;

    void setValue(const QString &key, const QVariant &value) override;
    [[nodiscard]] QVariant value(const QString &key, const QVariant &defaultValue) const override;
    void beginGroup(const QString &prefix) override;
    void endGroup() override;
    [[nodiscard]] QString group() const override;
    [[nodiscard]] bool contains(const QString &key) const override;
};

struct QMDMMCORE_PRIVATE_EXPORT SettingsWrapperP_QVariantMap : public SettingsWrapperP
{
    Q_DISABLE_COPY_MOVE(SettingsWrapperP_QVariantMap);

    QVariantMap map;
    QStringList currentGroup;

    SettingsWrapperP_QVariantMap() = default;
    ~SettingsWrapperP_QVariantMap() override;

    void setValue(const QString &key, const QVariant &value) override;
    [[nodiscard]] QVariant value(const QString &key, const QVariant &defaultValue) const override;
    void beginGroup(const QString &prefix) override;
    void endGroup() override;
    [[nodiscard]] QString group() const override;
    [[nodiscard]] bool contains(const QString &key) const override;

    [[nodiscard]] QString keyWithGroup(const QString &key) const;
};

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
