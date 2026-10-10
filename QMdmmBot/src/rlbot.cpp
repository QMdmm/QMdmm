// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bot.h"

#include <QMdmmCoreGlobal>

#include <cstdlib>
#include <iostream>

using namespace Qt::StringLiterals;

RlBot::RlBot(QMdmmNetworking::Client *parent)
    : Bot(parent)
{
    // The rl style is recognized by the configuration but not implemented yet.
    // Constructing one is a hard failure instead of silently joining a game with
    // no strategy, and it is refused the way every other unusable command line is
    // -- the reason on stderr and exit 3, the shape configErrorImpl() in config.cpp
    // has (that one is file-local, hence the repetition) -- rather than by dying on
    // a signal, which reads to a caller as a crash instead of as a refusal.
    const QString message = u"The Reinforcement Learning playing style is not implemented yet."_s;
    std::cerr << qPrintable(message) << '\n' << std::flush;
    qWarning().noquote() << message;
    std::exit(3);
}

// Unreachable at runtime (the constructor terminates), but the handlers must
// still be defined so that RlBot stays a concrete class.
void RlBot::handleRockPaperScissorsRequest(const QStringList &playerNames, int strivedOrder)
{
    Q_UNUSED(playerNames);
    Q_UNUSED(strivedOrder);
    Q_UNREACHABLE();
}

void RlBot::handleActionOrderRequest(const QList<int> &remainedOrders, int maximumOrder, int selectionNum)
{
    Q_UNUSED(remainedOrders);
    Q_UNUSED(maximumOrder);
    Q_UNUSED(selectionNum);
    Q_UNREACHABLE();
}

void RlBot::handleActionRequest(int currentOrder)
{
    Q_UNUSED(currentOrder);
    Q_UNREACHABLE();
}

void RlBot::handleUpgradeRequest(int remainingTimes)
{
    Q_UNUSED(remainingTimes);
    Q_UNREACHABLE();
}
