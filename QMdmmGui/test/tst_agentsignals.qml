// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick 2.15
import QtTest 1.2

// Guards the acceptance criterion of the 0.0.2 version -- every request and every notification
// the agent can raise has to reach the screen. The reading itself is the `agentInventory`
// context property, whose table and reasoning live in tst_qmdmmgui.cpp next to the signals it
// holds the class to; this case is only the reading.
//
// The check is a declaration by design: Qt does not expose a QObject's incoming connections, so
// no test can ask the bridge what it connected. What it can ask is whether somebody made the
// decision -- which is what `operateNotified` went without while it sat unconnected.
TestCase {
    function test_everyRequestAndNotificationHasADisposition() {
        // Both directions in one reading: a signal the table does not mention, and a row whose
        // signal is gone (a rename counts as gone). Either one means the reading it stands for
        // is not happening any more, so the message names them rather than just failing.
        compare(agentInventory.problems().join("; "), "", "the agent's request / notification signals are not all accounted for");
    }

    name: "Agent signals"
}
