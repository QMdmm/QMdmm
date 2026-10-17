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

// The round events are the only packets a room broadcasts, and every connection in the room builds
// the very same one. Building them here, rather than inside a connection, gives that wire form a
// single source: ServerConnectionP sends what these return, and the game recorder writes the same
// packet down, so a record cannot drift away from what the clients actually received.

QMdmmCore::Packet rockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies);
QMdmmCore::Packet actionOrderNotified(const QStringList &result);
QMdmmCore::Packet actionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace);
QMdmmCore::Packet upgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades);

} // namespace p
} // namespace QMdmmNetworking

#endif
