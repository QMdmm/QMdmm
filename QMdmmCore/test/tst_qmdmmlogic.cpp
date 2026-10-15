#include "test.h"

#include <QMdmmCore/QMdmmLogic>
#include <QMdmmCore/QMdmmLogicConfiguration>
#include <QMdmmPlayer>

#include "qmdmmlogic_p.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

using namespace QMdmmCore;

class tst_QMdmmLogic : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmLogic() = default;

    std::unique_ptr<Logic> l;

private slots:
    // https://doc.qt.io/qt-5/qtest-overview.html#creating-a-test
    // called before each test case is run

    void init()
    {
        l.reset(new Logic(LogicConfiguration::defaults(), this));
        l->addPlayer(u"test1"_s);
        l->addPlayer(u"test2"_s);
        l->addPlayer(u"test3"_s);
    }

    void QMdmmLogicstate()
    {
        QCOMPARE(l->state(), Logic::BeforeRoundStart);
    }

    void QMdmmLogicaddPlayer()
    {
        // case 1
        {
            bool r = l->addPlayer(u"test11"_s);
            QVERIFY(r);
        }

        // case 2
        {
            l->d->state = Logic::RpsForAction;
            bool r = l->addPlayer(u"test12"_s);
            QVERIFY(!r);
        }

        // case 3
        {
            l->d->state = Logic::BeforeRoundStart;
            bool r = l->addPlayer(u"test11"_s);
            QVERIFY(!r);
        }
    }

    void QMdmmLogicremovePlayer()
    {
        // case 1
        {
            bool r = l->removePlayer(u"test1"_s);
            QVERIFY(r);
        }

        // case 2
        {
            l->d->state = Logic::RpsForAction;
            bool r = l->removePlayer(u"test2"_s);
            QVERIFY(!r);
        }

        // case 3
        {
            l->d->state = Logic::BeforeRoundStart;
            bool r = l->removePlayer(u"test1"_s);
            QVERIFY(!r);
        }
    }

    void QMdmmLogicroundStart()
    {
        // case 1
        {
            bool r = l->roundStart();
            QVERIFY(r);
        }

        // case 2
        {
            bool r = l->roundStart();
            QVERIFY(!r);
        }

        init();
        l->removePlayer(u"test1"_s);
        l->removePlayer(u"test2"_s);
        l->removePlayer(u"test3"_s);

        // case 3
        {
            bool r = l->roundStart();
            QVERIFY(!r);
        }
    }

    void QMdmmLogicrpsReply()
    {
        // preparation
        {
            l->roundStart();
        }

        // case 0
        {
            QSignalSpy s(l.get(), &Logic::rpsResult);

            bool r = l->rpsReply(u"test00"_s, Data::Rock);
            QVERIFY(!r);

            QCOMPARE(s.length(), 0);
        }

        {
            init();
            l->roundStart();
        }

        // case 1
        {
            QSignalSpy s(l.get(), &Logic::rpsResult);

            bool r = l->rpsReply(u"test1"_s, Data::Rock);
            QVERIFY(r);

            r = l->rpsReply(u"test1"_s, Data::Scissors);
            QVERIFY(!r);

            QCOMPARE(s.length(), 0);
        }

        {
            init();
            l->roundStart();
        }

        // case 2
        {
            QSignalSpy s(l.get(), &Logic::rpsResult);
            QSignalSpy q(l.get(), &Logic::requestRpsForAction);

            bool r1 = l->rpsReply(u"test1"_s, Data::Rock);
            QVERIFY(r1);
            bool r2 = l->rpsReply(u"test2"_s, Data::Rock);
            QVERIFY(r2);
            bool r3 = l->rpsReply(u"test3"_s, Data::Rock);
            QVERIFY(r3);

            QCOMPARE(s.length(), 1);
            QCOMPARE(q.length(), 1);
        }
    }

    // Drive the RPS -> action-order -> action -> upgrade loop so that the GUI,
    // once wired to this engine, is not bitten by a backend bug.
    //
    // Note: RPS choices must NOT be a Rock/Paper/Scissors cycle -- that yields
    // no winner and the engine restarts RPS forever. Two Rocks vs one Scissors
    // gives exactly two winners, which is what exercises the action-order phase.
    // The request* signals are emitted synchronously inside the reply calls, so
    // the spies must be connected *before* the triggering call.
    void QMdmmLogicactionOrderReply()
    {
        l->roundStart(); // init() already added test1/test2/test3

        QSignalSpy req(l.get(), &Logic::requestActionOrder);
        QSignalSpy act(l.get(), &Logic::requestAction);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        // requestActionOrder is emitted synchronously inside the 3rd rpsReply().
        QVERIFY(req.count() > 0);

        bool r = l->actionOrderReply(u"test1"_s, {1});
        QVERIFY(r);
        r = l->actionOrderReply(u"test2"_s, {2});
        QVERIFY(r);

        // After both winners pick an order the engine enters the Action phase and
        // emits requestAction for the order-1 player, again synchronously.
        QVERIFY(act.count() > 0);
    }

    // All winners yield (0 sentinel): leftover orders are assigned automatically
    // and the engine advances to Action.
    void QMdmmLogicactionOrderAllYield()
    {
        l->roundStart();

        QSignalSpy ord(l.get(), &Logic::actionOrderResult);
        QSignalSpy act(l.get(), &Logic::requestAction);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        QVERIFY(l->actionOrderReply(u"test1"_s, {0}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {0}));

        QVERIFY(ord.count() > 0);
        QVERIFY(act.count() > 0);
        QCOMPARE(l->d->confirmedActionOrders.value(1), u"test1"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(2), u"test2"_s);
    }

    // Partial yield: one winner yields, the other picks. The picker keeps its
    // order, the yielder receives the leftover one.
    void QMdmmLogicactionOrderPartialYield()
    {
        l->roundStart();

        QSignalSpy ord(l.get(), &Logic::actionOrderResult);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        QVERIFY(l->actionOrderReply(u"test1"_s, {0}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {1}));

        QVERIFY(ord.count() > 0);
        QCOMPARE(l->d->confirmedActionOrders.value(1), u"test2"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(2), u"test1"_s);
    }

    // Yield by times: a player with two action opportunities can yield one and
    // pick the other (D-024 "yield per opportunity").
    void QMdmmLogicactionOrderYieldByTimes()
    {
        l->addPlayer(u"test4"_s);
        l->roundStart();

        QSignalSpy ord(l.get(), &Logic::actionOrderResult);

        // test1/test2 win twice each (two losers), orders 1..4.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);
        l->rpsReply(u"test4"_s, Data::Scissors);

        // test1 yields once and picks order 1; test2 picks orders 2 and 3.
        QVERIFY(l->actionOrderReply(u"test1"_s, {0, 1}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {2, 3}));

        QVERIFY(ord.count() > 0);
        QCOMPARE(l->d->confirmedActionOrders.value(1), u"test1"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(2), u"test2"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(3), u"test2"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(4), u"test1"_s);
    }

    // Yield plus conflict: a yielder sits out while two others fight over the
    // same order. The conflict loser must re-pick (not be defaulted), and only
    // then does the yielder get the leftover order.
    void QMdmmLogicactionOrderYieldWithConflict()
    {
        l->addPlayer(u"test4"_s);
        l->roundStart();

        QSignalSpy tie(l.get(), &Logic::requestRpsForActionOrder);
        QSignalSpy req(l.get(), &Logic::requestActionOrder);
        QSignalSpy act(l.get(), &Logic::requestAction);

        // Three winners (test1/test2/test3), one loser -> orders 1..3.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Rock);
        l->rpsReply(u"test4"_s, Data::Scissors);

        // First request round: one request per winner.
        QCOMPARE(req.count(), 3);

        QVERIFY(l->actionOrderReply(u"test1"_s, {0}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {1}));
        QVERIFY(l->actionOrderReply(u"test3"_s, {1}));

        QVERIFY(tie.count() > 0);

        // test2 wins the tie-break RPS (Paper beats Rock).
        l->rpsReply(u"test2"_s, Data::Paper);
        l->rpsReply(u"test3"_s, Data::Rock);

        // The conflict loser (test3) is asked again to re-pick, rather than being
        // defaulted to a leftover order.
        QCOMPARE(req.count(), 4);
        QVERIFY(l->actionOrderReply(u"test3"_s, {2}));

        QVERIFY(act.count() > 0);
        QCOMPARE(l->d->confirmedActionOrders.value(1), u"test2"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(2), u"test3"_s);
        QCOMPARE(l->d->confirmedActionOrders.value(3), u"test1"_s);
    }

    // Invalid order lists fall back to yielding every selection (return false) so
    // the state machine still advances; unknown players and duplicates are still
    // rejected outright.
    void QMdmmLogicactionOrderReplyValidation()
    {
        // Out-of-range order (maximumOrderNum == 2) -> full yield.
        {
            l->roundStart();
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);

            // Asserted because QMdmmBot's whole-match case matches this warning's text to
            // catch illegal replies: a reword must fail loudly here rather than disarm it.
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Logic::actionOrderReply: player \"test1\" sent an invalid order list"_s));
            QVERIFY(!l->actionOrderReply(u"test1"_s, {3}));
            QCOMPARE(l->d->actionOrderYields.value(u"test1"_s), 1);
            QCOMPARE(l->state(), Logic::ActionOrder); // test2 still owes a pick
        }

        // Wrong length (selections == 1) -> full yield.
        {
            init();
            l->roundStart();
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);

            QVERIFY(!l->actionOrderReply(u"test1"_s, {1, 2}));
            QCOMPARE(l->d->actionOrderYields.value(u"test1"_s), 1);
            QCOMPARE(l->state(), Logic::ActionOrder);
        }

        // Unknown player -> rejected, nothing recorded.
        {
            init();
            l->roundStart();
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);

            QVERIFY(!l->actionOrderReply(u"ghost"_s, {0}));
            QCOMPARE(l->state(), Logic::ActionOrder);
        }

        // A second reply from an already-answered player is rejected.
        {
            init();
            l->roundStart();
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);

            QVERIFY(l->actionOrderReply(u"test1"_s, {1}));
            QVERIFY(!l->actionOrderReply(u"test1"_s, {1}));
        }
    }

    // A player with two opportunities must not pick the same order twice; a
    // duplicated order falls back to yielding both selections.
    void QMdmmLogicactionOrderReplyDuplicateOrder()
    {
        l->addPlayer(u"test4"_s);
        l->roundStart();

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);
        l->rpsReply(u"test4"_s, Data::Scissors);

        // Duplicated order within a single reply (selections == 2).
        QVERIFY(!l->actionOrderReply(u"test1"_s, {1, 1}));
        QCOMPARE(l->d->actionOrderYields.value(u"test1"_s), 2);
        QCOMPARE(l->state(), Logic::ActionOrder);
    }

    void QMdmmLogicactionReply()
    {
        l->roundStart();

        QSignalSpy act(l.get(), &Logic::requestAction);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        QVERIFY(l->actionOrderReply(u"test1"_s, {1}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {2}));

        QVERIFY(act.count() > 0);

        // The order-1 player (test1) is the one requested to act.
        bool r = l->actionReply(u"test1"_s, Data::DoNothing, {}, 0);
        QVERIFY(r);
    }

    void QMdmmLogicupgradeReply()
    {
        l->roundStart();

        // Negative contract: outside the Upgrade state the reply is rejected.
        QVERIFY(!l->upgradeReply(u"test1"_s, {Data::UpgradeMaxHp}));

        // Positive contract: upgrades happen at round end, when <= 1 player is
        // alive. Simulate that (kill the other two) and give test1 an upgrade
        // point, then verify the reply is accepted and upgradeResult emitted.
        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);
        l->d->room->player(u"test1"_s)->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);
        QVERIFY(l->upgradeReply(u"test1"_s, {Data::UpgradeMaxHp}));
        QVERIFY(up.count() > 0);
    }

    // --- Gap coverage: branches the first pass of tests never exercised ---

    // A. RPS tie (no winner) must restart rps instead of advancing.
    void QMdmmLogicrpsTieRestarts()
    {
        QSignalSpy req(l.get(), &Logic::requestRpsForAction);
        QSignalSpy res(l.get(), &Logic::rpsResult);

        l->roundStart();

        // All three pick Rock -> a tie, no winner.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Rock);

        // requestRpsForAction fired once at roundStart and once more on the restart.
        QCOMPARE(req.length(), 2);
        // rpsResult is emitted (reports the tie) but the engine does NOT advance.
        QCOMPARE(res.length(), 1);
        QCOMPARE(l->state(), Logic::RpsForAction);

        // The restarted round is still playable: a winning combo now advances.
        QSignalSpy ord(l.get(), &Logic::requestActionOrder);
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);
        QVERIFY(ord.count() > 0);
    }

    // B. Exactly one RPS winner takes every action order (no ActionOrder phase).
    void QMdmmLogicsingleWinnerNoActionOrder()
    {
        l->roundStart();

        QSignalSpy ord(l.get(), &Logic::requestActionOrder);
        QSignalSpy act(l.get(), &Logic::requestAction);

        // test1=Rock beats test2=test3=Scissors; the two Scissors tie among themselves.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Scissors);
        l->rpsReply(u"test3"_s, Data::Scissors);

        // Single winner -> no action-order negotiation, straight to Action.
        QCOMPARE(ord.length(), 0);
        QVERIFY(act.count() > 0);
    }

    // C. Two winners picking the SAME order must fight a tie-break RPS (RpsForActionOrder).
    void QMdmmLogicactionOrderTieBreak()
    {
        l->roundStart();

        QSignalSpy reqOrder(l.get(), &Logic::requestActionOrder);
        QSignalSpy reqTie(l.get(), &Logic::requestRpsForActionOrder);
        QSignalSpy act(l.get(), &Logic::requestAction);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        QVERIFY(reqOrder.count() > 0);

        // Both winners demand order 1 -> engine asks them to break the tie with RPS.
        l->actionOrderReply(u"test1"_s, {1});
        l->actionOrderReply(u"test2"_s, {1});

        QVERIFY(reqTie.count() > 0);

        // Non-tie RPS resolves the struggle (Paper beats Rock here). The loser
        // then re-picks the remaining order; only then does Action follow.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Paper);

        QVERIFY(l->actionOrderReply(u"test1"_s, {2}));

        QVERIFY(act.count() > 0);
    }

    // D. actionOrderReply must reject unknown players and wrong states.
    void QMdmmLogicactionOrderReplyNegative()
    {
        l->roundStart();

        // unknown player, even once we are in the right state
        {
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);

            QVERIFY(!l->actionOrderReply(u"ghost"_s, {1}));
        }

        // wrong state: actionOrderReply before any RPS negotiation
        {
            init();
            l->roundStart();
            QVERIFY(!l->actionOrderReply(u"test1"_s, {1}));
        }
    }

    // E. An infeasible action (no knife -> cannot Slash) falls back to DoNothing so
    //    the state machine still advances; the reply is reported as not accepted.
    void QMdmmLogicactionReplyInfeasible()
    {
        l->roundStart();

        QSignalSpy res(l.get(), &Logic::actionResult);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        QVERIFY(l->actionOrderReply(u"test1"_s, {1}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {2}));

        // test1 has no knife, so Slash is infeasible -> the reply is not accepted,
        // but DoNothing is applied instead and the round advances.
        // Asserted because QMdmmBot's whole-match case matches this warning's text to
        // catch illegal replies: a reword must fail loudly here rather than disarm it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Logic::actionReply: player \"test1\" sent infeasible action"_s));
        QVERIFY(!l->actionReply(u"test1"_s, Data::Slash, u"test2"_s, 0));
        QCOMPARE(res.length(), 1);
    }

    // A ghost target (a to-bearing action against an unknown player) must be
    // rejected without dereferencing a null Player. Before the guard this crashed
    // the server (remote-triggerable DoS); now the infeasible action is replaced
    // by DoNothing and the state machine still advances.
    void QMdmmLogicactionReplyGhostTarget()
    {
        const Data::Action toBearingActions[] = {Data::Slash, Data::Kick, Data::LetMove};

        for (const Data::Action action : toBearingActions) {
            init();

            QSignalSpy res(l.get(), &Logic::actionResult);
            QSignalSpy act(l.get(), &Logic::requestAction);

            l->roundStart();

            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);

            QVERIFY(l->actionOrderReply(u"test1"_s, {1}));
            QVERIFY(l->actionOrderReply(u"test2"_s, {2}));

            // test1 (order 1) is requested to act.
            QCOMPARE(act.count(), 1);

            // The to-bearing action against a non-existent player must not crash
            // and must be rejected (replaced by DoNothing); the engine advances to
            // test2 rather than stalling.
            QVERIFY(!l->actionReply(u"test1"_s, action, u"ghost"_s, 0));
            QCOMPARE(res.length(), 1);
            QCOMPARE(act.count(), 2);
        }
    }

    // F. A feasible Slash is applied, can kill, and triggers roundOver.
    void QMdmmLogicactionReplyKillsAndRoundOver()
    {
        l->roundStart();

        // Put attacker and victim in the same place (Village == 0) and arm the attacker.
        l->d->room->player(u"test1"_s)->setHasKnife(true);
        l->d->room->player(u"test1"_s)->setPlace(0);
        l->d->room->player(u"test2"_s)->setPlace(0);
        // Victim one hit from death.
        l->d->room->player(u"test2"_s)->setHp(1);

        QSignalSpy res(l.get(), &Logic::actionResult);
        QSignalSpy over(l.get(), &Logic::roundOver);

        // All three alive during rps -> two winners (Rocks) -> action-order phase.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);

        QVERIFY(l->actionOrderReply(u"test1"_s, {1}));
        QVERIFY(l->actionOrderReply(u"test2"_s, {2}));

        // Kill the third player now, so the round ends the moment the victim dies.
        l->d->room->player(u"test3"_s)->setHp(0);

        QVERIFY(l->actionReply(u"test1"_s, Data::Slash, u"test2"_s, 0));

        QVERIFY(res.count() > 0);
        QVERIFY(over.count() > 0);
        QVERIFY(l->d->room->player(u"test2"_s)->dead());
    }

    // G. When a fully-maxed player exists and the round is over, upgrade ends the game.
    void QMdmmLogicupgradeGameOver()
    {
        l->roundStart();

        // End the round: only test1 survives.
        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);

        // test1 is fully upgraded (max HP / knife / horse) but still earns a point.
        Player *p = l->d->room->player(u"test1"_s);
        p->setMaxHp(20);
        p->setKnifeDamage(10);
        p->setHorseDamage(10);
        p->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy gameOver(l.get(), &Logic::gameOver);
        QSignalSpy up(l.get(), &Logic::upgradeResult);

        // Reply with no items for a fully-maxed player: the empty list cannot spend
        // the point, so it is replaced by a (still empty) feasible default and
        // reported as not accepted; nothing to apply, but the game is already over.
        QVERIFY(!l->upgradeReply(u"test1"_s, {}));
        QVERIFY(gameOver.count() > 0);
        QCOMPARE(up.length(), 0);
    }

    // H. upgradeReply actually applies the chosen upgrade items.
    void QMdmmLogicupgradeAppliesItems()
    {
        l->roundStart();

        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);
        Player *p = l->d->room->player(u"test1"_s);
        const int beforeMaxHp = p->maxHp();
        p->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);
        QVERIFY(l->upgradeReply(u"test1"_s, {Data::UpgradeMaxHp}));
        QVERIFY(up.count() > 0);
        QCOMPARE(p->maxHp(), beforeMaxHp + 1);
    }

    // I. actionReply must reject unknown players and wrong states (guards that
    //    return false before any feasibility check).
    void QMdmmLogicactionReplyNegative()
    {
        l->roundStart();

        // Wrong state: still negotiating RPS/action-order, not yet in Action.
        {
            QSignalSpy res(l.get(), &Logic::actionResult);
            QVERIFY(!l->actionReply(u"test1"_s, Data::DoNothing, {}, 0));
            QCOMPARE(res.length(), 0);
        }

        // Unknown player while in the Action state.
        {
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Scissors);
            QVERIFY(l->actionOrderReply(u"test1"_s, {1}));
            QVERIFY(l->actionOrderReply(u"test2"_s, {2}));

            QSignalSpy res(l.get(), &Logic::actionResult);
            QVERIFY(!l->actionReply(u"ghost"_s, Data::DoNothing, {}, 0));
            QCOMPARE(res.length(), 0);
        }
    }

    // J. upgradeReply must reject an unknown player even in the Upgrade state.
    void QMdmmLogicupgradeReplyNegative()
    {
        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);
        l->d->room->player(u"test1"_s)->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);
        QVERIFY(!l->upgradeReply(u"ghost"_s, {Data::UpgradeMaxHp}));
        QCOMPARE(up.length(), 0);
    }

    // K. upgradeReply falls back to a feasible default list instead of rejecting an
    //    infeasible reply, so a disconnected/timeout player (whose default reply cannot
    //    know remaining upgrade counts) never stalls the upgrade phase.
    void QMdmmLogicupgradeReplyFallsBackOnOverAllocation()
    {
        l->roundStart();

        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);
        Player *p = l->d->room->player(u"test1"_s);
        p->setUpgradePoint(2);
        // One knife upgrade remains before the cap; horse still has headroom.
        p->setKnifeDamage(l->d->room->logicConfiguration().maximumKnifeDamage() - 1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);

        // Over-allocate a single stat (2 knives requested, 1 remaining): the reply
        // is not accepted, but the logic builds a feasible default (knife first,
        // then horse).
        // Asserted because QMdmmBot's whole-match case matches this warning's text to
        // catch illegal replies: a reword must fail loudly here rather than disarm it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Logic::upgradeReply: player \"test1\" sent an infeasible upgrade list"_s));
        QVERIFY(!l->upgradeReply(u"test1"_s, {Data::UpgradeKnife, Data::UpgradeKnife}));
        QCOMPARE(up.length(), 1);
        QCOMPARE(p->knifeDamage(), l->d->room->logicConfiguration().maximumKnifeDamage());
        QCOMPARE(p->horseDamage(), l->d->room->logicConfiguration().initialHorseDamage() + 1);
    }

    void QMdmmLogicupgradeReplyFallsBackOnUnknownItem()
    {
        l->roundStart();

        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);
        Player *p = l->d->room->player(u"test1"_s);
        p->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);

        // An unknown item value makes the reply infeasible; the reply is not
        // accepted, but the logic falls back to a default that spends the point on
        // knife.
        QVERIFY(!l->upgradeReply(u"test1"_s, {static_cast<Data::UpgradeItem>(0xff)}));
        QCOMPARE(up.length(), 1);
        QCOMPARE(p->knifeDamage(), l->d->room->logicConfiguration().initialKnifeDamage() + 1);
    }

    // An empty list is exactly what the server's default upgrade reply sends for a
    // disconnected / timed-out player (ServerConnectionP::defaultReplyUpgrade). This pins
    // down the decision that such a player's point is auto-spent on knife rather than
    // forfeited (Hunyuan 09-05 review A4 / feedback #19): under a "forfeit" reading the
    // empty reply would discard the point and leave knife damage unchanged, so this
    // assertion distinguishes the two opposite semantics.
    void QMdmmLogicupgradeReplyEmptyListAutoSpends()
    {
        l->roundStart();

        l->d->room->player(u"test2"_s)->setHp(0);
        l->d->room->player(u"test3"_s)->setHp(0);
        Player *p = l->d->room->player(u"test1"_s);
        p->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);

        QVERIFY(!l->upgradeReply(u"test1"_s, {}));
        QCOMPARE(up.length(), 1);
        QCOMPARE(p->knifeDamage(), l->d->room->logicConfiguration().initialKnifeDamage() + 1);
    }

    // L. upgradeReply must not process the same player twice in one upgrade phase.
    void QMdmmLogicupgradeReplyRejectsDuplicate()
    {
        l->roundStart();

        l->d->room->player(u"test3"_s)->setHp(0);
        l->d->room->player(u"test1"_s)->setUpgradePoint(1);
        l->d->room->player(u"test2"_s)->setUpgradePoint(1);
        l->d->state = Logic::Upgrade;

        QSignalSpy up(l.get(), &Logic::upgradeResult);
        QVERIFY(l->upgradeReply(u"test1"_s, {Data::UpgradeMaxHp}));
        QVERIFY(!l->upgradeReply(u"test1"_s, {Data::UpgradeMaxHp}));
        // Still incomplete: test2 has not replied yet.
        QCOMPARE(up.length(), 0);
    }

    // M. A long streak of RPS ties is counted per run, reported once when it
    //    reaches the threshold, and starts over on the next run.
    void QMdmmLogicrpsTieStreak()
    {
        const int threshold = p::LogicP::rpsForActionTieStreakWarningThreshold;
        QVERIFY(threshold > 1);

        l->roundStart();

        // Ties below the threshold stay silent and keep adding up.
        for (int i = 1; i < threshold; ++i) {
            l->rpsReply(u"test1"_s, Data::Rock);
            l->rpsReply(u"test2"_s, Data::Rock);
            l->rpsReply(u"test3"_s, Data::Rock);
            QCOMPARE(l->d->rpsForActionTieStreak, i);
        }

        // The tie that reaches the threshold is reported.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Logic::rpsForAction: no winner after %1 ties"_s.arg(threshold)));
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Rock);
        QCOMPARE(l->d->rpsForActionTieStreak, threshold);

        // There is still no cap: one more tie only moves the count along.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Rock);
        QCOMPARE(l->d->rpsForActionTieStreak, threshold + 1);
        QCOMPARE(l->state(), Logic::RpsForAction);

        // A winning combination still leaves the state, and the next run does
        // not inherit the count of the previous one.
        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Scissors);
        QCOMPARE(l->state(), Logic::ActionOrder);

        l->d->state = Logic::BeforeRoundStart;
        QVERIFY(l->roundStart());
        QCOMPARE(l->d->rpsForActionTieStreak, 0);

        l->rpsReply(u"test1"_s, Data::Rock);
        l->rpsReply(u"test2"_s, Data::Rock);
        l->rpsReply(u"test3"_s, Data::Rock);
        QCOMPARE(l->d->rpsForActionTieStreak, 1);
    }

    // N. Six rule configurations x twenty matches each, played by a seeded auto-player
    //    through the whole Logic state machine, with one invariant set checked after every
    //    single step. This is the #31-12 item's property sweep.
    //
    //    Three of the invariants the item lists do not hold in the form it states them, so
    //    each is checked here as the reading this tree does keep, with the difference spelled
    //    out:
    //
    //      * "HP >= 0" -> "HP never falls below the one-blow floor". HP does go negative on
    //        the killing blow: Player::setHp() stores hp - damage unclamped, and dead() is
    //        hp <= 0 under the default zeroHpAsDead, so a player sitting at 1 HP slashed for
    //        10 ends at -9. What cannot happen is a second blow landing on someone who is
    //        already dead, which is what the floor expresses.
    //      * "same seat is unique" -> "the seats handed out at round start are unique".
    //        Sharing a place mid-round is legal: canMove() only asks isPlaceAdjacent(), so
    //        two players do end up in the same city, and the Village is shared by design.
    //        What is a real invariant is the round-start seating, Room::prepareForRoundStart()
    //        handing out City No. 1..N one per player every round.
    //      * "the upgrade point is capped" -> "it is capped against the upgrades that were
    //        left when it was granted". PlayerP::applyDamage caps it at grant time, and
    //        nothing else touches it afterwards, so what a running round keeps is
    //        point <= remaining + spent, where spent is what this round's upgrade phase has
    //        already consumed. Read against the remaining count alone it breaks in the window
    //        between that phase and the next round start: the points just spent stay on the
    //        player until Room::prepareForRoundStart() clears them, so spending 2 of 3
    //        points leaves "2 points, 1 left" behind for the rest of the round boundary.
    //
    //    The game-over invariant is checked in the only direction that is observable:
    //    Room::isGameOver() may not stay true outside the upgrade phase that reports it.
    //    (Player stats are reset before Logic::gameOver is emitted, so the winners are not
    //    re-derivable from the room afterwards.)
    void QMdmmLogicinvariantSweep()
    {
        // K = 6 configurations, each one a different corner of the rule set.
        QList<QPair<QString, LogicConfiguration>> cases;
        cases << qMakePair(u"defaults"_s, LogicConfiguration::defaults());
        cases << qMakePair(u"v1"_s, LogicConfiguration::v1());
        {
            LogicConfiguration config = LogicConfiguration::defaults();
            config.setCanBuyOnlyInInitialCity(true);
            cases << qMakePair(u"buy-only-in-initial-city"_s, config);
        }
        {
            // A slash outside the Village costs the attacker its whole HP bar, so the
            // attacker dies to its own blow: nothing else in this sweep covers that branch.
            LogicConfiguration config = LogicConfiguration::defaults();
            config.setPunishHpModifier(1);
            config.setPunishHpRoundStrategy(LogicConfiguration::RoundDown);
            cases << qMakePair(u"heavy-punish-round-down"_s, config);
        }
        {
            LogicConfiguration config = LogicConfiguration::defaults();
            config.setPunishHpModifier(3);
            config.setPunishHpRoundStrategy(LogicConfiguration::PlusOne);
            cases << qMakePair(u"punish-plus-one"_s, config);
        }
        {
            // Short caps, so a match reaches its end in a handful of rounds.
            LogicConfiguration config = LogicConfiguration::defaults();
            config.setInitialMaxHp(2);
            config.setMaximumMaxHp(4);
            config.setInitialKnifeDamage(1);
            config.setMaximumKnifeDamage(3);
            config.setInitialHorseDamage(1);
            config.setMaximumHorseDamage(3);
            config.setPunishHpModifier(0);
            cases << qMakePair(u"short-caps"_s, config);
        }
        QCOMPARE(cases.size(), 6);

        // One match, driven to game over. Returns a fingerprint of the room, so a repeat of
        // the same (configuration, players, seed) can be compared, and appends the first
        // broken invariant to *problems.
        const auto playMatch = [](const QString &label, const LogicConfiguration &config, int playerCount, quint32 seed, QStringList *problems) {
            Logic logic(config);

            for (int i = 1; i <= playerCount; ++i)
                logic.addPlayer(u"p%1"_s.arg(i));

            Room *room = logic.d->room;

            bool broken = false;
            const auto note = [&](const QString &what) {
                broken = true;
                if (problems->size() < 200)
                    problems->append(u"%1 / %2 players / seed %3: %4"_s.arg(label).arg(playerCount).arg(seed).arg(what));
            };

            // The lowest HP a blow can leave behind: one blow struck at somebody who was
            // alive, i.e. 1 - the largest damage a single action can deal. Punishment is
            // taken from the attacker's maxHp, which is at most maximumMaxHp (one more for
            // the PlusOne strategy); knife and horse damage are capped by their own maxima.
            int maximumBlow = std::max(config.maximumKnifeDamage(), config.maximumHorseDamage());
            if (config.punishHpModifier() > 0)
                maximumBlow = std::max(maximumBlow, config.maximumMaxHp() + 1);
            const int minimumHp = 1 - maximumBlow;

            QRandomGenerator rng(seed);
            const auto throwFor = [&rng]() {
                switch (rng.bounded(3)) {
                case 0:
                    return Data::Rock;
                case 1:
                    return Data::Paper;
                default:
                    return Data::Scissors;
                }
            };

            // Requests are only recorded, never answered from inside the handler: the loop
            // below replies once and then reads the state back, so a reply that moves the
            // state (a tie restarting the RPS, the last action ending the round) cannot leave
            // this bookkeeping describing a state that is already gone. Action-order entries
            // are keyed by player and replaced in place, because a re-entered action-order
            // sub-round asks the same player again with a fresh selection count.
            struct OrderRequest
            {
                QString name;
                QList<int> available;
                int selections;
            };
            struct UpgradeRequest
            {
                QString name;
                int points;
            };
            QStringList rpsPending;
            QStringList rpsOrderPending;
            QList<OrderRequest> orderPending;
            QString actionPending;
            QList<UpgradeRequest> upgradePending;
            // What each player has spent in this round's upgrade phase. The phase lowers the
            // remaining counts at once but leaves the points on the player until the next round
            // start clears both, so the cap has to be read against "remaining + spent" (see
            // above). Cleared at every round start, where the points go away.
            QHash<QString, int> spent;
            int rounds = 0;
            bool gameOverSeen = false;
            QStringList reportedWinners;

            QObject::connect(&logic, &Logic::requestRpsForAction, &logic, [&rpsPending](const QStringList &names) { rpsPending = names; });
            QObject::connect(&logic, &Logic::requestRpsForActionOrder, &logic, [&rpsOrderPending](const QStringList &names, int strivedOrder) {
                Q_UNUSED(strivedOrder);
                rpsOrderPending = names;
            });
            QObject::connect(&logic, &Logic::requestActionOrder, &logic, [&orderPending](const QString &name, const QList<int> &available, int maximumOrderNum, int selections) {
                Q_UNUSED(maximumOrderNum);
                for (OrderRequest &request : orderPending) {
                    if (request.name == name) {
                        request.available = available;
                        request.selections = selections;
                        return;
                    }
                }
                orderPending.append(OrderRequest {name, available, selections});
            });
            QObject::connect(&logic, &Logic::requestAction, &logic, [&actionPending](const QString &name, int actionOrder) {
                Q_UNUSED(actionOrder);
                actionPending = name;
            });
            QObject::connect(&logic, &Logic::requestUpgrade, &logic, [&upgradePending](const QString &name, int points) {
                for (UpgradeRequest &request : upgradePending) {
                    if (request.name == name) {
                        request.points = points;
                        return;
                    }
                }
                upgradePending.append(UpgradeRequest {name, points});
            });
            QObject::connect(&logic, &Logic::roundOver, &logic, [room, &note]() {
                if (room->alivePlayersCount() > 1)
                    note(u"the round was reported over with %1 players still alive"_s.arg(room->alivePlayersCount()));
            });
            QObject::connect(&logic, &Logic::gameOver, &logic, [room, &note, &gameOverSeen, &reportedWinners](const QStringList &winners) {
                if (gameOverSeen)
                    note(u"the game was reported over twice"_s);
                gameOverSeen = true;
                reportedWinners = winners;
                if (winners.isEmpty())
                    note(u"the game was reported over without a winner"_s);
                for (const QString &winner : winners) {
                    if (room->player(winner) == nullptr)
                        note(u"the game named %1, which is not a player of this room"_s.arg(winner));
                }
            });

            const auto checkInvariants = [&]() {
                for (const Player *p : room->players()) {
                    // 1. no blow lands on a player who is already dead (see the note above)
                    if (p->hp() < minimumHp)
                        note(u"%1 is at %2 HP, below the one-blow floor of %3"_s.arg(p->objectName()).arg(p->hp()).arg(minimumHp));

                    // HP never sits above the maximum it is read against while a game is
                    // running. The game-over path breaks that on purpose and is the one place
                    // it is skipped: Room::isGameOver() is read, then Player::resetUpgrades()
                    // lowers maxHp back to the initial value while the round-end HP stays put,
                    // so a finished room can read 17 HP against a maximum of 10. Nothing runs
                    // on from there but another round start, which refills HP.
                    if (!gameOverSeen && p->hp() > p->maxHp())
                        note(u"%1 is at %2 HP, above its %3 HP maximum"_s.arg(p->objectName()).arg(p->hp()).arg(p->maxHp()));

                    // 2. the upgrade point is capped against what was left to spend when it
                    //    was granted (see the note above)
                    const int remaining = p->upgradeKnifeRemainingTimes() + p->upgradeHorseRemainingTimes() + p->upgradeMaxHpRemainingTimes();
                    if (p->upgradePoint() > remaining + spent.value(p->objectName(), 0))
                        note(u"%1 holds %2 upgrade point(s) against %3 upgrade(s) and %4 spent"_s.arg(p->objectName())
                                 .arg(p->upgradePoint())
                                 .arg(remaining)
                                 .arg(spent.value(p->objectName(), 0)));

                    // 5. each stat stays inside the bounds its configuration gives it
                    if (p->knifeDamage() < config.initialKnifeDamage() || p->knifeDamage() > config.maximumKnifeDamage())
                        note(u"%1 has knife damage %2, outside [%3, %4]"_s.arg(p->objectName())
                                 .arg(p->knifeDamage())
                                 .arg(config.initialKnifeDamage())
                                 .arg(config.maximumKnifeDamage()));
                    if (p->horseDamage() < config.initialHorseDamage() || p->horseDamage() > config.maximumHorseDamage())
                        note(u"%1 has horse damage %2, outside [%3, %4]"_s.arg(p->objectName())
                                 .arg(p->horseDamage())
                                 .arg(config.initialHorseDamage())
                                 .arg(config.maximumHorseDamage()));
                    if (p->maxHp() < config.initialMaxHp() || p->maxHp() > config.maximumMaxHp())
                        note(u"%1 has %2 max HP, outside [%3, %4]"_s.arg(p->objectName()).arg(p->maxHp()).arg(config.initialMaxHp()).arg(config.maximumMaxHp()));
                }

                // 4. the round is over exactly when at most one player is alive: the logic
                //    only asks for an action while the round is still on, so it can never be
                //    sitting in the action state with the round already decided.
                if (logic.state() == Logic::Action && room->alivePlayersCount() <= 1)
                    note(u"the logic is still asking for an action with %1 player(s) alive"_s.arg(room->alivePlayersCount()));

                // 6. the game being over is only ever reported once, by the upgrade phase
                //    that ends it: isGameOver() left true means that report was skipped.
                if (rounds > 0 && !gameOverSeen && room->isGameOver())
                    note(u"the game is already over but Logic::gameOver was never reported"_s);
            };

            struct Decision
            {
                Data::Action action = Data::DoNothing;
                QString toPlayer;
                int toPlace = 0;
            };

            const auto decide = [&](const QString &name) {
                // Walk one step toward a place on this board: every city is adjacent to the
                // Village and to nothing else, so the Village is always the way through.
                const auto stepToward = [](const Player *me, int target) {
                    if (target < 0 || target == me->place())
                        return -1;
                    return me->place() == Data::Village ? target : static_cast<int>(Data::Village);
                };

                Decision decision;

                Player *me = room->player(name);
                if (me == nullptr || !me->alive())
                    return decision;

                if (!me->hasKnife()) {
                    if (me->canBuyKnife()) {
                        decision.action = Data::BuyKnife;
                        return decision;
                    }

                    // Unarmed and unable to buy here: walk toward a city that will sell us
                    // one. Under canBuyOnlyInInitialCity that is our own seat, otherwise any
                    // city will do and we are necessarily standing in the Village.
                    int target = me->initialPlace();
                    if (!config.canBuyOnlyInInitialCity()) {
                        target = -1;
                        for (const Player *p : room->players()) {
                            if (p != me && p->place() != Data::Village) {
                                target = p->place();
                                break;
                            }
                        }
                        if (target < 0)
                            target = 1;
                    }

                    const int step = stepToward(me, target);
                    if (step >= 0 && me->canMove(step)) {
                        decision.action = Data::Move;
                        decision.toPlace = step;
                    }
                    return decision;
                }

                // Armed: hit somebody standing here. The seed picks among the candidates, so
                // different seeds walk different lines.
                QStringList targets;
                for (const Player *p : room->players()) {
                    if (p != me && me->canSlash(p))
                        targets << p->objectName();
                }
                if (!targets.isEmpty()) {
                    targets.sort();
                    decision.action = Data::Slash;
                    decision.toPlayer = targets.at(rng.bounded(targets.size()));
                    return decision;
                }

                // Nobody here: close the distance.
                int target = -1;
                for (const Player *p : room->players()) {
                    if (p != me && p->alive() && p->place() != me->place()) {
                        target = p->place();
                        break;
                    }
                }
                const int step = stepToward(me, target);
                if (step >= 0 && me->canMove(step)) {
                    decision.action = Data::Move;
                    decision.toPlace = step;
                }
                return decision;
            };

            // The match only leaves this loop by reaching game over; every other way out
            // records a failure, so a sweep that quietly stopped early could not pass.
            const int stepLimit = 50000;
            QString failure;
            int steps = 0;

            while (failure.isEmpty() && !broken) {
                if (steps++ >= stepLimit) {
                    failure = u"no game over within %1 steps"_s.arg(stepLimit);
                    break;
                }

                const Logic::State state = logic.state();

                if (state == Logic::BeforeRoundStart) {
                    if (gameOverSeen)
                        break;
                    ++rounds;
                    if (!logic.roundStart()) {
                        failure = u"roundStart() was refused"_s;
                        break;
                    }
                    spent.clear();

                    // 3. the seats are handed out here, one City No. per player, every round
                    QList<int> seats;
                    for (const Player *p : room->players())
                        seats << p->place();
                    std::sort(seats.begin(), seats.end());
                    for (int i = 0; i < seats.size(); ++i) {
                        if (seats.at(i) != i + 1) {
                            QStringList seatText;
                            for (int seat : seats)
                                seatText << QString::number(seat);
                            note(u"round start seated the players at %1, not one City No. each"_s.arg(seatText.join(u","_s)));
                            break;
                        }
                    }
                } else if (state == Logic::RpsForAction || state == Logic::RpsForActionOrder) {
                    QStringList &pending = (state == Logic::RpsForAction) ? rpsPending : rpsOrderPending;
                    if (pending.isEmpty()) {
                        failure = u"%1 is waiting for a throw from nobody"_s.arg(state == Logic::RpsForAction ? u"RpsForAction"_s : u"RpsForActionOrder"_s);
                        break;
                    }
                    const QString name = pending.takeFirst();
                    if (!logic.rpsReply(name, throwFor())) {
                        failure = u"the throw of %1 was refused"_s.arg(name);
                        break;
                    }
                } else if (state == Logic::ActionOrder) {
                    if (orderPending.isEmpty()) {
                        failure = u"ActionOrder is waiting for a pick from nobody"_s;
                        break;
                    }
                    const OrderRequest request = orderPending.takeFirst();
                    if (!logic.actionOrderReply(request.name, request.available.mid(0, request.selections))) {
                        failure = u"the order pick of %1 was refused"_s.arg(request.name);
                        break;
                    }
                } else if (state == Logic::Action) {
                    if (actionPending.isEmpty()) {
                        failure = u"Action is waiting for an action from nobody"_s;
                        break;
                    }
                    const QString name = actionPending;
                    actionPending.clear();
                    const Decision decision = decide(name);
                    if (!logic.actionReply(name, decision.action, decision.toPlayer, decision.toPlace)) {
                        failure = u"the action of %1 was refused"_s.arg(name);
                        break;
                    }
                } else {
                    if (upgradePending.isEmpty()) {
                        failure = u"Upgrade is waiting for a list from nobody"_s;
                        break;
                    }
                    const UpgradeRequest request = upgradePending.takeFirst();
                    const Player *p = room->player(request.name);
                    if (p == nullptr) {
                        failure = u"Upgrade named %1, which is not a player of this room"_s.arg(request.name);
                        break;
                    }

                    QList<Data::UpgradeItem> items;
                    int left = request.points;
                    const auto take = [&items, &left](Data::UpgradeItem item, int remaining) {
                        const int n = std::min(left, remaining);
                        for (int i = 0; i < n; ++i)
                            items << item;
                        left -= n;
                    };
                    take(Data::UpgradeKnife, p->upgradeKnifeRemainingTimes());
                    take(Data::UpgradeHorse, p->upgradeHorseRemainingTimes());
                    take(Data::UpgradeMaxHp, p->upgradeMaxHpRemainingTimes());
                    if (left > 0) {
                        failure = u"%1 was asked to spend %2 point(s) with only %3 upgrade(s) left"_s.arg(request.name).arg(request.points).arg(request.points - left);
                        break;
                    }
                    if (!logic.upgradeReply(request.name, items)) {
                        failure = u"the upgrade list of %1 was refused"_s.arg(request.name);
                        break;
                    }
                    spent[request.name] += request.points;
                }

                checkInvariants();
            }

            if (!failure.isEmpty())
                note(failure);

            QStringList parts;
            for (const Player *p : room->players())
                parts << u"%1@%2 hp%3 k%4 h%5 m%6"_s.arg(p->objectName()).arg(p->place()).arg(p->hp()).arg(p->knifeDamage()).arg(p->horseDamage()).arg(p->maxHp());
            parts << u"rounds=%1"_s.arg(rounds);
            parts << u"winners=%1"_s.arg(reportedWinners.join(u","_s));
            parts << u"gameOver=%1"_s.arg(gameOverSeen ? 1 : 0);
            return parts.join(u" | "_s);
        };

        QStringList problems;
        const auto sweepOnce = [&](QHash<QString, QString> *fingerprints) {
            for (int ci = 0; ci < cases.size(); ++ci) {
                for (int game = 0; game < 20; ++game) {
                    const int playerCount = 2 + (game % 3);
                    const quint32 seed = static_cast<quint32>((ci + 1) * 1000 + game);
                    const QString fingerprint = playMatch(cases.at(ci).first, cases.at(ci).second, playerCount, seed, &problems);
                    if (fingerprints != nullptr)
                        fingerprints->insert(u"%1 / %2 players / seed %3"_s.arg(cases.at(ci).first).arg(playerCount).arg(seed), fingerprint);
                }
            }
        };

        QHash<QString, QString> firstPass;
        QHash<QString, QString> secondPass;
        sweepOnce(&firstPass);
        sweepOnce(&secondPass);

        QCOMPARE(firstPass.size(), 6 * 20);

        // Repeatability: the same seed has to walk the same line. This one is within a single
        // process, so it says nothing about the cross-process question #31-9 asks; it is here
        // to catch a driver that reaches for global state or the clock.
        for (auto it = firstPass.constBegin(); it != firstPass.constEnd(); ++it) {
            if (secondPass.value(it.key()) != it.value())
                problems.append(u"%1: the same seed played a different line (%2 vs %3)"_s.arg(it.key(), it.value(), secondPass.value(it.key())));
        }

        QStringList report = problems.mid(0, 20);
        if (problems.size() > report.size())
            report << u"... and %1 more"_s.arg(problems.size() - report.size());
        QVERIFY2(problems.isEmpty(), qPrintable(report.join(u"\n"_s)));
    }
};

namespace {
RegisterTestObject<tst_QMdmmLogic> _;
}
#include "tst_qmdmmlogic.moc"

// NOLINTEND
