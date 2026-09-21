// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 2.15
import QtTest 1.2

// Guards the player card: the agent-state line, which turns the state mask (Data::AgentState:
// online / bot / managed) into words -- the card is the only place in the GUI where it does, and
// the managed flag in particular has nothing else on screen -- and the one control the card
// carries, the managed switch on your own card.
//
// Like tst_scene.qml, the card is loaded from the source tree (the QMdmm.Gui module resource
// lives in the QMdmm6 executable, which this test does not link) and reads the client through
// the `game` context property that tst_qmdmmgui.cpp installs the same way MainWindow does. The
// player is a plain object: the card only ever reads properties off it.
//
// The switch cases need the card on screen -- a hidden toggle is not a control -- so those run
// in a window of their own with the window shown, the way tst_statusbar.qml does it.
TestCase {
    id: testCase

    // The card the switch case drives. GameScene binds its cards to the bridge's map, so this one
    // is bound the same way: the card reads whatever the server last broadcast rather than the
    // value it was born with, and the switch is what shows the difference.
    property var cardUnderTest: null

    function cleanup() {
        // The switch case drives the engine's own client (the `game` context property), shared
        // with the other cases: drop the connection and leave it idle behind us.
        cardUnderTest = null;
        game.disconnectAll();
    }

    function expectStateText(agentState, expected) {
        const card = makeCard(agentState);
        verify(hasText(card, expected), "state " + agentState + " should read as '" + expected + "'");
    }

    // The switch stays in the object tree on a card it is not for, so what is looked for is a
    // switch that is on screen.
    function findToggle(root) {
        for (let i = 0; i < root.children.length; ++i) {
            const c = root.children[i];
            if (c.objectName === "managedToggle" && c.visible)
                return c;
            const found = findToggle(c);
            if (found !== null)
                return found;
        }
        return null;
    }

    function hasText(root, text) {
        for (let i = 0; i < root.children.length; ++i) {
            const c = root.children[i];
            if (c.text === text)
                return true;
            if (hasText(c, text))
                return true;
        }
        return false;
    }

    function makeCard(agentState, you, host) {
        const comp = Qt.createComponent(Qt.resolvedUrl("../qml/PlayerCard.qml"));
        tryCompare(comp, "status", Component.Ready);
        verify(comp.status === Component.Ready, "PlayerCard should load");

        const card = createTemporaryObject(comp, host === undefined ? testCase : host, {
                                               agentState: agentState,
                                               displayName: "P1",
                                               player: {
                                                   "dead": false,
                                                   "hasHorse": false,
                                                   "hasKnife": false,
                                                   "hp": 10,
                                                   "maxHp": 20,
                                                   "place": 0,
                                                   "upgradePoint": 0
                                               },
                                               you: you === true
                                           });
        verify(card !== null, "PlayerCard should instantiate");
        return card;
    }

    // Something for the card to live in that is on screen: a window of its own, since the test
    // case itself is not visible.
    function makeHost() {
        const win = createTemporaryObject(windowComponent, testCase);
        verify(win !== null, "the host window should be created");

        const host = createTemporaryObject(hostComponent, win.contentItem);
        verify(host !== null, "the host item should be created");
        return host;
    }

    function test_eachStateBitIsSpelledOut() {
        // One case per bit, with both readings of the online bit: a bit the wire sets has to
        // show up, and a bit it does not set must not be invented. The state is read off the
        // rendered card, not off the mapping, so the binding is on trial too.
        expectStateText(0x00, "Offline");
        expectStateText(0x10, "Online");
        expectStateText(0x08, "Offline, Managed");
        expectStateText(0x11, "Online, Bot");
        expectStateText(0x18, "Online, Managed");
        expectStateText(0x19, "Online, Bot, Managed");
    }

    function test_theManagedSwitchIsOnlyOnYourOwnOnlineCard() {
        // Two gates on the switch, one reading each: only your own card carries it -- nobody
        // hands another player over -- and only while that player is online, since the
        // declaration a click makes has nowhere to travel from an offline card. The last two
        // readings are the flag itself: the fill follows the state, not the click.
        const host = makeHost();

        const mine = makeCard(0x10, true, host);
        verify(findToggle(mine) !== null, "your own online card carries the switch");
        verify(!mine.managed, "an unmanaged player's switch reads as off");

        const shut = makeCard(0x18, true, host);
        verify(findToggle(shut) !== null, "the switch stays on a managed card");
        verify(shut.managed, "a managed player's switch reads as on");
        verify(hasText(shut, "Online, Managed"));

        const notMine = makeCard(0x10, false, host);
        verify(findToggle(notMine) === null, "another player's card does not carry it");

        const offline = makeCard(0x00, true, host);
        verify(findToggle(offline) === null, "an offline card does not carry it");
    }

    function test_theSwitchDeclaresTheFlagToTheServer() {
        // The whole path, from the click on: the card asks the bridge to declare the flag, the
        // server applies it and broadcasts the new state back, and both the bridge's map and the
        // card follow -- which is what is waited for. A click that only changed something inside
        // the card would leave the map where it was, and the card would go on asking for the flag
        // it already has.
        game.playerCount = 2;
        game.startLocalGame("Tester");
        tryVerify(function () {
            return game.agentStates[game.localName] === 0x10;
        }, 15000);

        cardUnderTest = makeCard(0x10, true, makeHost());
        wait(50);
        verify(findToggle(cardUnderTest) !== null, "your own online card carries the switch");
        verify(!cardUnderTest.managed, "the switch starts off");

        mouseClick(findToggle(cardUnderTest));
        tryVerify(function () {
            return game.agentStates[game.localName] === 0x18;
        }, 15000);
        tryVerify(function () {
            return cardUnderTest.managed;
        }, 5000);

        // And back, on a card that by now reads the flag as on: the same switch has to ask for it
        // off. The wait keeps the second click from being read as a double click on the first one.
        wait(700);
        mouseClick(findToggle(cardUnderTest));
        tryVerify(function () {
            return game.agentStates[game.localName] === 0x10;
        }, 15000);
        tryVerify(function () {
            return !cardUnderTest.managed;
        }, 5000);
    }

    name: "PlayerCard"

    // See the header: the switch cases assert on what is on screen.
    when: windowShown

    Binding {
        property: "agentState"
        target: cardUnderTest
        value: game.agentStates[game.localName]
        when: cardUnderTest !== null
    }

    Component {
        id: windowComponent

        Window {
            height: 1024
            visible: true
            width: 1024
        }
    }

    Component {
        id: hostComponent

        Item {
            anchors.fill: parent
        }
    }
}
