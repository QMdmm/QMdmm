// SPDX-License-Identifier: AGPL-3.0-or-later

#include "mainwindow.h"

#include <QMdmmCoreGlobal>

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QLocale>
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
    parser.process(a);

    // Load the translation matching the system locale (falls back to the English source text).
    QTranslator translator;
    if (translator.load(QLocale(), u"qmdmm"_s, u"_"_s, u":/i18n"_s))
        QApplication::installTranslator(&translator);

    // Make font suitable for displaying
    QFont font = QApplication::font();
    font.setPixelSize(50);
    QApplication::setFont(font);

    MainWindow mainwindow(parser.value(u"server"_s), parser.value(u"bot"_s));

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    mainwindow.showMaximized();
#else
    mainwindow.show();
#endif

    // NOLINTNEXTLINE(clang-analyzer-core.StackAddressEscape)
    return QApplication::exec();
}
