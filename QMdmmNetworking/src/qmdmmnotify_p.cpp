// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmnotify_p.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

QMdmmCore::Packet playerAddedNotified(const QString &playerName, const QString &screenName, const QMdmmCore::Data::AgentState &agentState)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    ob.insert(u"screenName"_s, screenName);
    ob.insert(u"agentState"_s, static_cast<int>(agentState.toInt()));
    return {QMdmmCore::Protocol::NotifyPlayerAdded, ob};
}

QMdmmCore::Packet gameStartNotified()
{
    return {QMdmmCore::Protocol::NotifyGameStart, {}};
}

QMdmmCore::Packet roundStartNotified()
{
    return {QMdmmCore::Protocol::NotifyRoundStart, {}};
}

QMdmmCore::Packet roundOverNotified()
{
    return {QMdmmCore::Protocol::NotifyRoundOver, {}};
}

QMdmmCore::Packet gameOverNotified(const QStringList &playerNames)
{
    return {QMdmmCore::Protocol::NotifyGameOver, QJsonArray::fromStringList(playerNames)};
}

QMdmmCore::Packet rockPaperScissorsNotified(const QHash<QString, QMdmmCore::Data::RockPaperScissors> &replies)
{
    QJsonObject ob;
    for (QHash<QString, QMdmmCore::Data::RockPaperScissors>::const_iterator it = replies.constBegin(); it != replies.constEnd(); ++it)
        ob.insert(it.key(), static_cast<int>(it.value()));
    return {QMdmmCore::Protocol::NotifyRockPaperScissors, ob};
}

QMdmmCore::Packet actionOrderNotified(const QStringList &result)
{
    QJsonArray arr;
    for (const QString &playerName : result)
        arr.append(playerName);
    return {QMdmmCore::Protocol::NotifyActionOrder, arr};
}

QMdmmCore::Packet actionNotified(const QString &playerName, QMdmmCore::Data::Action action, const QString &toPlayer, int toPlace)
{
    QJsonObject ob;
    ob.insert(u"playerName"_s, playerName);
    ob.insert(u"action"_s, static_cast<int>(action));

    switch (action) {
    case QMdmmCore::Data::Slash:
    case QMdmmCore::Data::Kick:
    case QMdmmCore::Data::LetMove:
        ob.insert(u"toPlayer"_s, toPlayer);
        break;
    default:
        break;
    }

    switch (action) {
    case QMdmmCore::Data::Move:
    case QMdmmCore::Data::LetMove:
        ob.insert(u"toPlace"_s, toPlace);
        break;
    default:
        break;
    }

    return {QMdmmCore::Protocol::NotifyAction, ob};
}

QMdmmCore::Packet upgradeNotified(const QHash<QString, QList<QMdmmCore::Data::UpgradeItem>> &upgrades)
{
    QJsonObject ob;
    for (QHash<QString, QList<QMdmmCore::Data::UpgradeItem>>::const_iterator it = upgrades.constBegin(); it != upgrades.constEnd(); ++it) {
        QJsonArray arr;
        foreach (QMdmmCore::Data::UpgradeItem up, it.value())
            arr.append(static_cast<int>(up));
        ob.insert(it.key(), arr);
    }
    return {QMdmmCore::Protocol::NotifyUpgrade, ob};
}

} // namespace p
} // namespace QMdmmNetworking
