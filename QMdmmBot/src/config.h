// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMBOT_CONFIG_H
#define QMDMMBOT_CONFIG_H

#include <QMdmmClient>
#include <QMdmmRoom>
#include <QMdmmSettings>

#include <QCommandLineParser>
#include <QSettings>

class Config
{
public:
    // The command line to read, in QCoreApplication::arguments()' shape: the
    // program name first, then the options. It is a parameter rather than a
    // read of the global application object, so that the values below can be
    // asked for without a process to run in.
    explicit Config(const QStringList &arguments);

    [[nodiscard]] QString host() const
    {
        return host_;
    }

    [[nodiscard]] QString name() const
    {
        return name_;
    }

    [[nodiscard]] QString playingStyle() const
    {
        return playingStyle_;
    }

private:
    void read_(QCommandLineParser *parser);

    QString host_;
    QString name_;
    QString playingStyle_;
};

#endif
