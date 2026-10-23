// SPDX-License-Identifier: AGPL-3.0-or-later

#include "test.h"

#include <QTest>
#include <QThread>

#include <memory>

// NOLINTBEGIN
// Exempt from clang-tidy by policy; see AGENTS.md.

class tst_QMdmmRace : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE tst_QMdmmRace() = default;

private slots:
    void raceOnAPlainInt();
};

// The positive control of the ThreadSanitizer leg: two threads write one plain
// int with nothing to order the writes, which is a data race by construction.
//
// The leg exists to read what the runtime finds, and a reading of zero means
// one of two things -- nothing raced, or the runtime said nothing at all. Those
// are told apart only by a case that must produce a report; this is that case.
// The step that runs it passes options under which the runtime reports, so a
// zero there is a defect in the control rather than a fact about the tree.
//
// Every other cell skips the case: what it does is undefined behaviour, and the
// ordinary builds have no reason to run it. QMDMM_TSAN_CONTROL is set by the
// positive-control step in .github/workflows/ci.yml and nowhere else.
void tst_QMdmmRace::raceOnAPlainInt()
{
    if (qEnvironmentVariableIsEmpty("QMDMM_TSAN_CONTROL")) {
        QSKIP("only the ThreadSanitizer leg's positive control sets QMDMM_TSAN_CONTROL");
    }

    int shared = 0;
    const auto bump = [&shared] {
        for (int i = 0; i < 20000; ++i)
            shared = shared + 1;
    };

    std::unique_ptr<QThread> first {QThread::create(bump)};
    std::unique_ptr<QThread> second {QThread::create(bump)};

    first->start();
    second->start();

    QVERIFY(first->wait(60000));
    QVERIFY(second->wait(60000));

    // Read the int afterwards so neither store can be dropped, and deliberately
    // do not assert its value: which interleaving the two threads took is not
    // something this case is about, and a count here would be the flaky kind of
    // assertion the log records elsewhere. What the case is for is the report.
    QVERIFY(shared >= 0);
}

namespace {
RegisterTestObject<tst_QMdmmRace> _;
} // namespace
#include "tst_qmdmmrace.moc"

// NOLINTEND
