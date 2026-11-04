// Document: edits, undo/redo, coalescing, validation, and history limits.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Document.h>
#include <chordcore/Progression.h>

#include <random>
#include <stdexcept>
#include <vector>

using namespace chordcore;

namespace {

Document::Result setTempo(Document& d, double bpm, const std::string& key = {}) {
    return d.edit("Set tempo", [bpm](Progression& p) { p.tempoBpm = bpm; }, key);
}

Document::Result setKey(Document& d, int tonic) {
    return d.edit("Change key", [tonic](Progression& p) { p.key.tonic = tonic; });
}

}  // namespace

TEST_CASE("Document: starts as the demo with no history", "[document]") {
    Document d;
    CHECK(d.progression() == makeDemoProgression());
    CHECK_FALSE(d.canUndo());
    CHECK_FALSE(d.canRedo());
    CHECK(d.undoLabel().empty());
    CHECK(d.redoLabel().empty());
    CHECK(d.revision() == 0);
    CHECK_FALSE(d.undo());
    CHECK_FALSE(d.redo());
    CHECK(d.revision() == 0);
}

TEST_CASE("Document: edit, undo, redo", "[document]") {
    Document d;
    const auto original = d.progression();

    REQUIRE(setTempo(d, 90).ok);
    CHECK(d.progression().tempoBpm == 90.0);
    CHECK(d.canUndo());
    CHECK(d.undoLabel() == "Set tempo");

    REQUIRE(setKey(d, 7).ok);
    CHECK(d.progression().key.tonic == 7);
    CHECK(d.undoLabel() == "Change key");

    REQUIRE(d.undo());
    CHECK(d.progression().key.tonic == 0);
    CHECK(d.progression().tempoBpm == 90.0);
    CHECK(d.canRedo());
    CHECK(d.redoLabel() == "Change key");
    CHECK(d.undoLabel() == "Set tempo");

    REQUIRE(d.undo());
    CHECK(d.progression() == original);
    CHECK_FALSE(d.canUndo());

    REQUIRE(d.redo());
    CHECK(d.progression().tempoBpm == 90.0);
    REQUIRE(d.redo());
    CHECK(d.progression().key.tonic == 7);
    CHECK_FALSE(d.canRedo());
    CHECK_FALSE(d.redo());
}

TEST_CASE("Document: a new edit discards the redo stack", "[document]") {
    Document d;
    setTempo(d, 90);
    setKey(d, 7);
    d.undo();
    REQUIRE(d.canRedo());
    setTempo(d, 100);
    CHECK_FALSE(d.canRedo());
    CHECK(d.progression().key.tonic == 0);
    CHECK(d.progression().tempoBpm == 100.0);
}

TEST_CASE("Document: rejected edits change nothing", "[document]") {
    Document d;
    setTempo(d, 90);
    const auto before = d.progression();
    const auto revision = d.revision();

    SECTION("invalid result") {
        const auto r = d.edit("Overlap", [](Progression& p) { p.blocks[1].startTick = 10; });
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
    }
    SECTION("invalid tempo") { CHECK_FALSE(setTempo(d, 5000).ok); }
    SECTION("mutator throws") {
        const auto r = d.edit("Boom", [](Progression& p) {
            p.tempoBpm = 50;  // partial change must not leak
            throw std::runtime_error("boom");
        });
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("boom") != std::string::npos);
    }

    CHECK(d.progression() == before);
    CHECK(d.revision() == revision);
    CHECK(d.undoLabel() == "Set tempo");  // history intact: exactly one entry
    d.undo();
    CHECK_FALSE(d.canUndo());
}

TEST_CASE("Document: an edit that changes nothing records nothing", "[document]") {
    Document d;
    const auto revision = d.revision();
    CHECK(setTempo(d, d.progression().tempoBpm).ok);
    CHECK_FALSE(d.canUndo());
    CHECK(d.revision() == revision);
}

TEST_CASE("Document: coalescing merges a run of same-key edits into one undo step", "[document][coalesce]") {
    Document d;
    const auto original = d.progression();

    for (double bpm : {100.0, 101.0, 102.0, 103.0}) REQUIRE(setTempo(d, bpm, "tempo").ok);
    CHECK(d.progression().tempoBpm == 103.0);
    REQUIRE(d.undo());
    CHECK(d.progression() == original);  // one step back to before the whole drag
    CHECK_FALSE(d.canUndo());
    REQUIRE(d.redo());
    CHECK(d.progression().tempoBpm == 103.0);

    SECTION("a different key starts a new step") {
        setKey(d, 5);
        setTempo(d, 110, "tempo");
        setTempo(d, 111, "tempo");
        d.undo();
        CHECK(d.progression().tempoBpm == 103.0);
        CHECK(d.progression().key.tonic == 5);
    }
    SECTION("an undo ends the run") {
        d.undo();
        setTempo(d, 110, "tempo");
        setTempo(d, 111, "tempo");  // merges with the 110 edit, not with anything before the undo
        d.undo();
        CHECK(d.progression() == original);
    }
    SECTION("an empty key never merges") {
        setTempo(d, 120);
        setTempo(d, 121);
        d.undo();
        CHECK(d.progression().tempoBpm == 120.0);
    }
}

TEST_CASE("Document: coalesced edits do not merge into an earlier unrelated edit", "[document][coalesce]") {
    Document d;
    setKey(d, 7);                      // different key (none)
    setTempo(d, 100, "tempo");         // first "tempo" edit starts its own step
    setTempo(d, 101, "tempo");
    d.undo();
    CHECK(d.progression().key.tonic == 7);
    CHECK(d.progression().tempoBpm == 120.0);
}

TEST_CASE("Document: replace loads a state and clears history", "[document]") {
    Document d;
    setTempo(d, 90);
    setTempo(d, 91);
    d.undo();
    REQUIRE(d.canUndo());
    REQUIRE(d.canRedo());
    const auto revision = d.revision();

    auto loaded = makeDemoProgression();
    loaded.key = Key{2, Mode::Minor};
    REQUIRE(d.replace(loaded).ok);
    CHECK(d.progression() == loaded);
    CHECK_FALSE(d.canUndo());
    CHECK_FALSE(d.canRedo());
    CHECK(d.revision() == revision + 1);

    auto bad = loaded;
    bad.tempoBpm = -1;
    CHECK_FALSE(d.replace(bad).ok);
    CHECK(d.progression() == loaded);
}

TEST_CASE("Document: revision counts changes only", "[document]") {
    Document d;
    setTempo(d, 90);
    CHECK(d.revision() == 1);
    setTempo(d, 90);        // no change
    setTempo(d, 5000);      // rejected
    CHECK(d.revision() == 1);
    d.undo();
    CHECK(d.revision() == 2);
    d.redo();
    CHECK(d.revision() == 3);
    d.redo();               // nothing to redo
    CHECK(d.revision() == 3);
}

TEST_CASE("Document: history is bounded", "[document]") {
    Document d(makeDemoProgression(), 5);
    for (int i = 0; i < 20; ++i) setTempo(d, 100.0 + i);
    int undone = 0;
    while (d.undo()) ++undone;
    CHECK(undone == 5);
    CHECK(d.progression().tempoBpm == 114.0);  // the oldest 15 steps were dropped; 5 remain
}

TEST_CASE("property: undoing everything restores the start, redoing everything restores the end", "[document][property]") {
    std::mt19937 rng(2024);
    for (int trial = 0; trial < 50; ++trial) {
        Document d(makeDemoProgression(), 1000);
        std::vector<Progression> states{d.progression()};

        for (int step = 0; step < 40; ++step) {
            switch (rng() % 4) {
                case 0: setTempo(d, 20.0 + static_cast<double>(rng() % 900)); break;
                case 1: setKey(d, static_cast<int>(rng() % 12)); break;
                case 2:
                    d.edit("Add chord", [&](Progression& p) {
                        ChordSpec c{1 + static_cast<int>(rng() % 7), 0, Quality::Major, Seventh::None, 0, Suspension::None};
                        p.blocks.push_back(ChordBlock{c, p.endTick(), 960});
                    });
                    break;
                default:
                    d.edit("Remove chord", [](Progression& p) { if (!p.blocks.empty()) p.blocks.pop_back(); });
                    break;
            }
            if (d.progression() != states.back()) states.push_back(d.progression());
        }

        const auto finalState = d.progression();
        for (std::size_t i = states.size() - 1; i > 0; --i) {
            REQUIRE(d.progression() == states[i]);
            REQUIRE(d.undo());
        }
        REQUIRE(d.progression() == states[0]);
        REQUIRE_FALSE(d.undo());
        for (std::size_t i = 1; i < states.size(); ++i) {
            REQUIRE(d.redo());
            REQUIRE(d.progression() == states[i]);
        }
        REQUIRE(d.progression() == finalState);
        REQUIRE_FALSE(d.redo());
    }
}
