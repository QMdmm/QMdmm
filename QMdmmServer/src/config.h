// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSERVER_CONFIG_H
#define QMDMMSERVER_CONFIG_H

#include <QMdmmRoom>
#include <QMdmmServer>
#include <QMdmmSettings>

#include <QCommandLineParser>
#include <QSettings>

class Config
{
public:
    Config();

    [[nodiscard]] const QMdmmNetworking::ServerConfiguration &serverConfiguration() const;
    [[nodiscard]] const QMdmmCore::LogicConfiguration &logicConfiguration() const;
    [[nodiscard]] QMdmmCore::LogLevel logLevel() const;

private:
    QMdmmNetworking::ServerConfiguration serverConfiguration_;
    QMdmmCore::LogicConfiguration logicConfiguration_;

    // How much of the log the terminal is told. It is not a configuration item: nothing in
    // ServerConfiguration carries it, and it says how this run reports itself rather than what
    // the server does, so it comes from the command line only and -c does not write it out.
    QMdmmCore::LogLevel logLevel_ = QMdmmCore::LogLevel::Off;

    void read_(QMdmmCore::Settings *setting, QCommandLineParser *parser);
    int save_(QMdmmCore::Settings *setting, QMdmmCore::Settings::Instance toSave);
    void show_();
};

#endif
