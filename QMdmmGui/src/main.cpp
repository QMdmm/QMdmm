// SPDX-License-Identifier: AGPL-3.0-or-later

#include "mainwindow.h"

#include <QMdmmCoreGlobal>

#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    [[maybe_unused]] QApplication a(argc, argv);

    // Where a local game starts its server and its bots from. Given here, they are used as
    // they are; left out, they are looked for next to this program (see
    // QMdmmGameClient::setProgramPaths). A path that leads nowhere is not an error yet --
    // nothing needs either program until a local game is asked for.
    QCommandLineParser parser;
    parser.setApplicationDescription(u"QMdmm client"_s);
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(u"server"_s, u"Path to the program that runs the server of a local game."_s, u"path"_s));
    parser.addOption(QCommandLineOption(u"bot"_s, u"Path to the program that fills a seat of a local game."_s, u"path"_s));
    // Leaves as soon as the window is up (see the end of main), for the test that reads
    // what the startup wrote to the log. Out of the usage text: it is not a way to use
    // the client, it is a way to end a run of it that would otherwise only end by hand.
    QCommandLineOption probeOption(u"probe"_s, u"Leave as soon as the window is up."_s);
    probeOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(probeOption);
    parser.process(a);

    // Load the translation matching the system locale (falls back to the English source text).
    QTranslator translator;
    if (translator.load(QLocale(), u"qmdmm"_s, u"_"_s, u":/i18n"_s))
        QApplication::installTranslator(&translator);

    MainWindow mainwindow(parser.value(u"server"_s), parser.value(u"bot"_s));

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    mainwindow.showMaximized();
#else
    mainwindow.show();
#endif

    // The view is built while the window is constructed, so whatever the startup has to say
    // about it is said before the loop starts. One turn of the loop lets the load's queued
    // work have its say and the program leaves there; a run without the switch stays in the
    // loop until the window is closed, which is what a user's run does.
    if (parser.isSet(probeOption))
        QTimer::singleShot(0, &a, &QCoreApplication::quit);

    // NOLINTNEXTLINE(clang-analyzer-core.StackAddressEscape)
    return QApplication::exec();
}
