// SPDX-License-Identifier: AGPL-3.0-or-later

// A stand-in for a bot, for the GUI case that watches the seats a local game fills.
//
// A real bot's run is one match (see QMdmmBot/src/main.cpp): it leaves with code 0 when the
// match it was started for ends, and the bridge has to read that as a seat that played its game
// out and went home rather than as a seat that failed. Staging a real match all the way to its
// end inside the GUI test would be a whole game played out, so this program takes the other half
// apart from it: it starts, ignores the arguments a bot is handed, and leaves with code 0 the way
// a bot does once its match is over.
//
// Nothing else -- no output, no seat taken, no connection. What the case is about is the bridge's
// reading of that exit, so the program on the other side of it has to be as plain as this.

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

int main()
{
    return 0;
}

// NOLINTEND
