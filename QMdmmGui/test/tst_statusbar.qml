// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 2.15
import QtTest 1.2

// Guards the one place the client's own status and its error reports are shown, in
// both directions: the strip itself (a line shows up, a reason is marked as one, and
// the strip does not grow without end), and the wiring behind it (what the bridge
// announces has to reach it through RootItem). The wiring half is not optional -- a
// strip that renders perfectly and stays empty for the whole session is exactly the
// state this work started from: GameClient knew the status all along and nothing read
// it -- so the last two cases drive the bridge's own channels instead of the strip's
// properties.
//
// The cases run with a visible window and wait for a frame: what is being asserted is
// that the line is on screen, and an item below an invisible one is not visible either
// (with no window up, everything here would read as hidden and the cases would say
// nothing). The strip and the scenes are loaded from the source tree the way the other
// scene cases load theirs: the QMdmm.Gui module resource lives in the QMdmm6
// executable, which this test does not link. They read the client through the `game`
// context property that tst_qmdmmgui.cpp installs the same way MainWindow does.
//
// What is NOT covered: the retry chain itself. A refused connection is reported twice -- the
// transport's reason as soon as it is known, and the generic "Reconnect failed" once the
// automatic reconnect has spent its retries (~16 s, ClientP::scheduleReconnect). The cases below
// pin only that the reason gets through; how often the client retries in between, and when it
// gives up, are properties of the client rather than of this strip.
TestCase {
    id: testCase

    function cleanup() {
        // The bridge here is the engine's own client (the `game` context property),
        // shared with the other cases: drop whatever connection was attempted and leave
        // it idle behind us.
        game.disconnectAll();
    }

    // A reason is a Text with a colour of its own (see StatusBar.qml), and finding one that way
    // is what lets a case ask "has a reason arrived?" without knowing what the transport called
    // it: the wording comes from the platform, not from this code.
    function findErrorLine(root) {
        for (let i = 0; i < root.children.length; ++i) {
            const c = root.children[i];
            if (String(c.color) === "#ff6b6b")
                return c;
            const found = findErrorLine(c);
            if (found !== null)
                return found;
        }
        return null;
    }

    // Recursive search for a rendered Text, so a case can assert on what is on screen
    // rather than on the properties behind it.
    function findText(root, text) {
        for (let i = 0; i < root.children.length; ++i) {
            const c = root.children[i];
            if (c.text === text)
                return c;
            const found = findText(c, text);
            if (found !== null)
                return found;
        }
        return null;
    }

    function hasText(root, text) {
        return findText(root, text) !== null;
    }

    // Something for the object under test to live in that is on screen: a window of its
    // own, since the test case itself is not visible.
    function makeHost() {
        const win = createTemporaryObject(windowComponent, testCase);
        verify(win !== null, "the host window should be created");

        const host = createTemporaryObject(hostComponent, win.contentItem);
        verify(host !== null, "the host item should be created");
        return host;
    }

    function makeObject(url, parent, props) {
        const comp = Qt.createComponent(Qt.resolvedUrl(url));
        tryCompare(comp, "status", Component.Ready);
        verify(comp.status === Component.Ready, url + " should load");

        const obj = createTemporaryObject(comp, parent, props === undefined ? {} : props);
        verify(obj !== null, url + " should instantiate");
        return obj;
    }

    function makeStrip(host) {
        // A width of its own: the strip does not set one, its host does.
        return makeObject("../qml/StatusBar.qml", host, {
                              width: 400
                          });
    }

    function test_theErrorTheBridgeReportsReachesTheStrip() {
        const root = makeObject("../qml/RootItem.qml", makeHost());

        game.errorOccurred("the server process did not start");

        tryVerify(function () {
            return hasText(root, "the server process did not start");
        }, 5000);
    }

    function test_theReasonForARefusedConnectionReachesTheStrip() {
        const root = makeObject("../qml/RootItem.qml", makeHost());

        // A connect attempt against a port nothing listens on. The transport knows why it failed
        // as soon as it fails and the bridge passes that on; the give-up notice that follows on
        // the same channel takes the whole retry chain (~16 s) to arrive, so a reason on the
        // strip well inside that window can only be the real one. Without the bridge's second
        // channel the strip stays silent here and the user is told nothing until the generic
        // give-up lands.
        game.connectOnline("qmdmm://127.0.0.1:1", "Tester");

        tryVerify(function () {
            return findErrorLine(root) !== null;
        }, 5000);

        // And it is the reason itself, not the give-up notice that also travels the channel.
        verify(findErrorLine(root).text !== "Reconnect failed");
    }

    function test_theReasonIsShownAndMarkedAsAnError() {
        const strip = makeStrip(makeHost());

        // The reason has to be readable as a reason and not as just another state line:
        // which of the two it is is what tells the user whether to wait a little longer
        // or to go and look at the server.
        strip.append("Connecting to server...", false);
        strip.append("Connection refused", true);

        wait(50);
        const line = findText(strip, "Connection refused");
        verify(line !== null, "the reason should be on the strip");
        compare(line.color, "#ff6b6b");
        compare(findText(strip, "Connecting to server...").color, "#dddddd");
    }

    function test_theStatusTheBridgeAnnouncesReachesTheStrip() {
        const root = makeObject("../qml/RootItem.qml", makeHost());

        // A real connect attempt against a port nothing listens on: what the user sees
        // when the server is not there. The bridge announces the status it moves to and
        // the strip has to follow it -- without the strip the screen says nothing at all
        // while the connect is being attempted.
        game.connectOnline("qmdmm://127.0.0.1:1", "Tester");

        tryVerify(function () {
            return hasText(root, "Connecting to server...");
        }, 5000);

        // On screen, not merely in the object tree.
        verify(findText(root, "Connecting to server...").visible);
    }

    function test_theStripDoesNotGrowWithoutEnd() {
        const strip = makeStrip(makeHost());

        for (let i = 0; i < 3; ++i)
            strip.append("line " + i, false);
        wait(50);
        const boxWithThree = strip.height;

        for (let i = 3; i < 20; ++i)
            strip.append("line " + i, false);
        wait(50);

        compare(strip.lines.length, 3);
        compare(strip.height, boxWithThree);
        verify(hasText(strip, "line 19"));

        // The oldest lines go first: the newest reason is the one that still matters.
        verify(!hasText(strip, "line 16"));
    }

    function test_theStripShowsNothingUntilThereIsSomethingToSay() {
        const strip = makeStrip(makeHost());

        wait(50);
        compare(strip.visible, false);

        strip.append("Connecting to server...", false);

        wait(50);
        compare(strip.visible, true);
        verify(hasText(strip, "Connecting to server..."));
    }

    name: "StatusBar"

    // See the header: the assertions are about being on screen.
    when: windowShown

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
