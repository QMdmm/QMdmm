// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 6.5

// Where the client's own account of itself lands: the status it is in ("Connecting
// to server...", "Disconnected") and the reasons it reports when something failed --
// a connection that could not be made, a socket that dropped. Both exist on the
// bridge (GameClient::statusMessage and GameClient::errorOccurred) and neither had a
// reader: with nothing on screen saying why, a failed connect just looks like a GUI
// that stopped responding.
//
// RootItem hosts it above the three scenes, because no single scene owns the message:
// a connection is asked for from the connect scene, the outcome shows up in the game
// scene, and a drop can happen while either is on screen.
Item {
    id: statusBar

    // Oldest first, each entry { text, error }. The status line an error arrives with
    // is replaced by the next status, so the reason gets its own line -- it is what
    // has to stay readable.
    property var lines: []

    // Bounded like the match log: a long session must not grow the strip forever.
    property int maximumLines: 3

    function append(text, error) {
        if (!text)
            return;

        // Re-assign rather than mutate in place: a plain JS array carries no change
        // signal, so a Repeater bound to it would never see the line.
        const items = lines.slice();
        const item = {
            "text": text,
            "error": error
        };
        items.push(item);
        if (items.length > maximumLines)
            items.splice(0, items.length - maximumLines);
        lines = items;
    }

    // Only when there is something to say: an empty strip would sit on top of whatever
    // the current scene has in that corner for no reason.
    height: column.height + 12
    visible: lines.length > 0

    // Both are events, and both are announced by the bridge: every new status line, and
    // an error beside it when there is one.
    Connections {
        function onErrorOccurred(message) {
            statusBar.append(message, true);
        }

        function onStatusMessageChanged(message) {
            statusBar.append(message, false);
        }

        target: game
    }

    Rectangle {
        anchors.fill: parent
        color: "#cc000000"
        radius: 8
    }

    Column {
        id: column

        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 6
        spacing: 2

        Repeater {
            model: statusBar.lines

            Text {
                color: modelData.error ? "#ff6b6b" : "#dddddd"
                font.pixelSize: 18
                text: modelData.text
                width: column.width
                wrapMode: Text.Wrap
            }
        }
    }
}
