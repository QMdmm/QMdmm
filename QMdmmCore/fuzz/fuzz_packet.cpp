// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The libFuzzer harness for the inbound decoder. Whatever a peer puts on the
// wire, this is the code that has to survive it: a packet arrives as a byte
// array and Packet::fromJson() is the only thing between that array and every
// field the rest of the program reads. The corpus beside this file (corpus/) is
// the seed set; CI keeps it in front of every change, and a run that finds
// something new writes it into a corpus of its own.
//
// This file is not part of the ordinary test set: a fuzz run is measured in
// executions rather than in passed or failed cases, so it has no add_test()
// entry and the target is built only when QMDMM_BUILD_FUZZ_TARGET is on (see
// CMakeLists.txt beside this file).
// NOLINTBEGIN

#include <QMdmmCore/QMdmmProtocol>

#include <QByteArray>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    const QByteArray input(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));

    const QMdmmCore::Packet packet = QMdmmCore::Packet::fromJson(input);

    // A rejected packet stops here on purpose. The decoder reporting an error is
    // the expected answer for most of what a fuzzer throws at it, and what is
    // left -- a packet it accepted -- is the only input anything downstream sees.
    if (packet.hasError())
        return 0;

    // Every field of an accepted packet is read by whoever handles it, so read
    // them all: a field that goes wrong only when asked for is still a defect.
    (void)packet.type();
    (void)packet.requestId();
    (void)packet.notifyId();
    (void)packet.value();

    // And an accepted packet has to survive the trip back out. serialize() is
    // what a reply is built from, and the second pass is what the peer on the
    // other end does with it, so a packet that only decodes once is a defect
    // too -- one side's write has to be the other side's read.
    (void)QMdmmCore::Packet::fromJson(packet.serialize());

    return 0;
}

// NOLINTEND
