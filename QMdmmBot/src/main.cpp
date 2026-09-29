// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bot.h"
#include "config.h"

#include <QMdmmClient>
#include <QMdmmGlobal>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setOrganizationName(u"Fsu0413.me"_s);
    QCoreApplication::setApplicationName(u"QMdmmBot"_s);
    QCoreApplication::setApplicationVersion(QMdmmCore::Global::version().toString());

    // Resolved rather than read from the definition: the directory may be relative to the
    // tree this program was installed into, and may fall back to the home directory when
    // that tree is not writable (see QMdmmCore::Global::runtimeDataDirectory()).
    QString logDirectory = QMdmmCore::Global::runtimeDataDirectory() + u"/log"_s;

    if (QDir().mkpath(logDirectory)) {
        QString logFilePath = QDir(logDirectory).absoluteFilePath(u"QMdmmBot-"_s + QString::number(QDateTime::currentMSecsSinceEpoch()));
        QFile *logFile = new QFile(logFilePath);

        if (logFile->open(QIODevice::WriteOnly)) {
            logFile->setParent(&a);
            QMdmmCore::qMdmmDebugSetDevice(logFile);
        } else {
            delete logFile;
            qCritical("Unable to create log file %s .", qPrintable(logFilePath));
        }
    } else {
        qCritical("Unable to create log directory %s .", qPrintable(logDirectory));
    }

    Config config;

    QMdmmNetworking::ClientConfiguration conf;
    conf.setScreenName(config.name());

    QMdmmNetworking::Client client(conf);

    Bot *bot = Bot::createBot(config.playingStyle(), &client);

    // The bot needs no further driving and the pointer is discarded on purpose:
    // it is a child of the client, so it is destroyed with the client and needs
    // no owner here, and its whole behaviour is the set of signal connections its
    // constructor makes. Signing in below is all that is left for this process.
    //
    // createBot() answers nullptr for a style it does not know, which would leave
    // this run a strategy-less client that still signs in and plays. That is out
    // of reach today because Config::read_() rejects an unknown style through
    // Bot::styleExist before we get here, but the guarantee is implicit and worth
    // stating.
    Q_UNUSED(bot);

    client.connectToHost(config.host(), QMdmmCore::Data::StateOnlineBot);

    return QCoreApplication::exec();
}
