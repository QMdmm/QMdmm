#include "test.h"

#include <QMdmmCore/QMdmmLogic>
#include <QMdmmCore/QMdmmLogicConfiguration>
#include <QMdmmPlayer>

#include "qmdmmlogic_p.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

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
};

namespace {
RegisterTestObject<tst_QMdmmLogic> _;
}
#include "tst_qmdmmlogic.moc"

// NOLINTEND
