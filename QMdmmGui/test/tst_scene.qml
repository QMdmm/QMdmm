// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 2.15
import QtTest 1.2

// Interaction smoke test for the QML scenes and the button they are built from: loads the real
// StartScene from the source tree (exercising its `import "."` Button import, which the app
// relies on) and verifies the scene's button wiring -- triggering the "Start game" button must
// emit the scene's startGameClicked signal that RootItem connects to the connect scene.
//
// The button cases need the button on screen, so those run in a window of their own, the way
// tst_playercard.qml does it.
TestCase {
    id: testCase

    function findButtonByText(root, text) {
        for (var i = 0; i < root.children.length; ++i) {
            var c = root.children[i];
            if (c.text === text)
                return c;
            var found = findButtonByText(c, text);
            if (found)
                return found;
        }
        return null;
    }

    function makeButton(host) {
        var comp = Qt.createComponent(Qt.resolvedUrl("../qml/Button.qml"));
        tryCompare(comp, "status", Component.Ready);
        verify(comp.status === Component.Ready, "Button should load");

        var button = createTemporaryObject(comp, host, {
                                               height: 80,
                                               text: "Do it",
                                               width: 200,
                                               x: 200,
                                               y: 200
                                           });
        verify(button !== null, "Button should instantiate");
        return button;
    }

    function makeHost() {
        var win = createTemporaryObject(windowComponent, testCase);
        verify(win !== null, "the host window should be created");

        var host = createTemporaryObject(hostComponent, win.contentItem);
        verify(host !== null, "the host item should be created");
        return host;
    }

    // `enabled` is Item's own -- the button does not carry a flag of its own name -- and a
    // button that has nothing to offer is dimmed and takes no press.
    function test_aDisabledButtonTakesNoPress() {
        var button = makeButton(makeHost());

        var spy = createTemporaryObject(signalSpyComponent, testCase, {
                                            target: button,
                                            signalName: "clicked"
                                        });

        wait(50);
        verify(button.enabled, "a button starts out enabled");
        verify(button.opacity === 1, "an enabled button is not dimmed");
        mouseClick(button);
        compare(spy.count, 1, "an enabled button takes the press");

        button.enabled = false;
        verify(button.opacity < 1, "a disabled button is dimmed");

        wait(700);
        mouseClick(button);
        compare(spy.count, 1, "a disabled button takes no press");
    }

    function test_startSceneStartGameButton() {
        var comp = Qt.createComponent(Qt.resolvedUrl("../qml/StartScene.qml"));
        tryCompare(comp, "status", Component.Ready);
        verify(comp.status === Component.Ready, "StartScene should load");

        var scene = createTemporaryObject(comp, testCase);
        verify(scene !== null);

        var spy = createTemporaryObject(signalSpyComponent, testCase, {
                                            target: scene,
                                            signalName: "startGameClicked"
                                        });

        var button = findButtonByText(scene, "Start game");
        verify(button !== null, "Start game button should exist");

        // Emit the button's clicked signal (what its MouseArea does on a real
        // click) and assert the scene forwards it as startGameClicked.
        button.clicked();
        compare(spy.count, 1, "clicking Start game should emit startGameClicked");
    }

    // The button is drawn where it stands (see ../qml/Button.qml): the shape is the button, so
    // the rectangle that takes a click is the rectangle that is on screen. A press just past
    // an edge has to reach nothing -- the button this replaced was an image with rounded
    // corners inside a square hit area, and took presses over the corners.
    function test_theButtonTakesAPressOnlyInsideItsOwnOutline() {
        var button = makeButton(makeHost());

        var spy = createTemporaryObject(signalSpyComponent, testCase, {
                                            target: button,
                                            signalName: "clicked"
                                        });

        wait(50);
        mouseClick(button);
        compare(spy.count, 1, "a press on the button itself reaches it");

        // The waits keep the next press from being read as the second half of a double click.
        wait(700);
        mouseClick(button, button.width + 2, button.height / 2);
        compare(spy.count, 1, "a press past the right edge reaches nothing");

        wait(700);
        mouseClick(button, button.width / 2, button.height + 2);
        compare(spy.count, 1, "a press below the bottom edge reaches nothing");
    }

    name: "Scenes"
    when: windowShown

    Component {
        id: signalSpyComponent

        SignalSpy {
        }
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
