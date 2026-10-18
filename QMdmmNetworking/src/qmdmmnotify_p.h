// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMNETWORKING_QMDMMNOTIFY_P_H
#define QMDMMNETWORKING_QMDMMNOTIFY_P_H

#include <QMdmmProtocol>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace QMdmmNetworking {
namespace p {

// The packets a room broadcasts, and every connection in the room builds the very same one.
// Building them here, rather than inside a connection, gives that wire form a single source:
// ServerConnectionP sends what these return, and the game recorder writes the same packet down, so
// a record cannot drift away from what the clients actually received.
//
// Two families live here: the four round events (rock paper scissors, action order, action,
// upgrade), which each connection also keeps in its reconnect catch-up log, and the five lifecycle
// notifications (player added, game start, round start, round over, game over), which a record
// needs to say what the game was and how it ended. The room also broadcasts notices that are not
// here -- a player removal, a chat line, an operate notice -- because they are not part of a game's
// record. See RecorderP for what a record holds.

QMdmmCore::Packet playerAddedNotified(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState);
QMdmmCore::Packet gameStartNotified();
QMdmmCore::Packet roundStartNotified();
QMdmmCore::Packet roundOverNotified();
QMdmmCore::Packet gameOverNotified(const QStringList &playerNames);

QMdmmCore::Packet rockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies);
QMdmmCore::Packet actionOrderNotified(const QStringList &result);
QMdmmCore::Packet actionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
QMdmmCore::Packet upgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades);

} // namespace p
} // namespace QMdmmNetworking

#endif
