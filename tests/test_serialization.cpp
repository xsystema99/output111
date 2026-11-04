// State persistence: versioned JSON round trip, a golden document, and hostile input.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>

#include <random>
#include <string>

using namespace chordcore;

namespace {

std::string replaceFirst(std::string text, const std::string& from, const std::string& to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

void expectRejected(const std::string& json) {
    INFO("input: " << json.substr(0, 120));
    const auto r = deserializeProgression(json);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(r.progression == Progression{});  // a failed read hands back nothing half-built
}

const std::string kDemoJson =
    "{\"format\":\"chordsketch-state\",\"version\":1,\"key\":{\"tonic\":0,\"mode\":\"major\"},\"tempoBpm\":120,"
    "\"timeSignature\":{\"numerator\":4,\"denominator\":4},\"blocks\":["
    "{\"degree\":1,\"accidental\":0,\"quality\":\"major\",\"seventh\":\"none\",\"inversion\":0,\"suspension\":\"none\",\"startTick\":0,\"durationTicks\":3840},"
    "{\"degree\":5,\"accidental\":0,\"quality\":\"major\",\"seventh\":\"none\",\"inversion\":0,\"suspension\":\"none\",\"startTick\":3840,\"durationTicks\":3840},"
    "{\"degree\":6,\"accidental\":0,\"quality\":\"minor\",\"seventh\":\"none\",\"inversion\":0,\"suspension\":\"none\",\"startTick\":7680,\"durationTicks\":3840},"
    "{\"degree\":4,\"accidental\":0,\"quality\":\"major\",\"seventh\":\"none\",\"inversion\":0,\"suspension\":\"none\",\"startTick\":11520,\"durationTicks\":3840}"
    "]}";

}  // namespace

TEST_CASE("serializeProgression: golden document for the demo progression", "[state][golden]") {
    // Pins the on-disk format: changing it requires bumping kStateVersion and writing a migration.
    CHECK(serializeProgression(makeDemoProgression()) == kDemoJson);
    CHECK(kStateVersion == 1);
}

TEST_CASE("deserializeProgression: reads the golden document", "[state]") {
    const auto r = deserializeProgression(kDemoJson);
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.progression == makeDemoProgression());
}

TEST_CASE("round trip: demo, empty, and unusual progressions are reproduced exactly", "[state]") {
    auto roundTrip = [](const Progression& p) {
        const auto text = serializeProgression(p);
        const auto r = deserializeProgression(text);
        REQUIRE(r.ok);
        CHECK(r.progression == p);
        CHECK(serializeProgression(r.progression) == text);  // and re-saving is byte-identical
    };

    roundTrip(makeDemoProgression());
    roundTrip(Progression{});  // no blocks

    Progression odd;
    odd.key = Key{3, Mode::Minor};
    odd.tempoBpm = 133.3;
    odd.timeSignature = TimeSignature{7, 8};
    odd.blocks = {
        ChordBlock{ChordSpec{5, 0, Quality::Major, Seventh::Minor, 2, Suspension::Sus4}, 0, 1920},
        ChordBlock{ChordSpec{7, -1, Quality::Diminished, Seventh::Diminished, 3, Suspension::None}, 3360, 1},
        ChordBlock{ChordSpec{3, 1, Quality::Augmented, Seventh::Major, 0, Suspension::Sus2}, 5000, 2'000'000'000},
    };
    roundTrip(odd);
}

TEST_CASE("tempo survives exactly, including values with no short decimal form", "[state]") {
    for (double bpm : {120.0, 120.5, 133.3, 97.123456789, 20.0, 999.0, 0.1 + 0.2 + 100.0, 1.0 / 3.0 * 300.0}) {
        INFO("tempo " << bpm);
        auto p = makeDemoProgression();
        p.tempoBpm = bpm;
        const auto r = deserializeProgression(serializeProgression(p));
        REQUIRE(r.ok);
        CHECK(r.progression.tempoBpm == bpm);  // bit-exact
    }
    auto p = makeDemoProgression();
    p.tempoBpm = 120.5;
    CHECK(serializeProgression(p).find("\"tempoBpm\":120.5,") != std::string::npos);
}

TEST_CASE("property: random progressions round-trip", "[state][property]") {
    std::mt19937 rng(0x5EED);
    auto pick = [&](unsigned n) { return static_cast<int>(rng() % n); };
    const int denominators[] = {1, 2, 4, 8, 16, 32, 64};

    for (int iteration = 0; iteration < 300; ++iteration) {
        Progression p;
        p.key = Key{pick(12), pick(2) ? Mode::Major : Mode::Minor};
        p.tempoBpm = 20.0 + (rng() % 979000) / 1000.0;
        p.timeSignature = TimeSignature{1 + pick(32), denominators[pick(7)]};
        Tick cursor = pick(3) == 0 ? 60 * pick(100) : 0;
        for (int b = 0, n = pick(12); b < n; ++b) {
            ChordSpec c;
            c.degree = 1 + pick(7);
            c.accidental = pick(3) - 1;
            c.quality = static_cast<Quality>(pick(4));
            c.seventh = static_cast<Seventh>(pick(4));
            c.inversion = pick(c.seventh == Seventh::None ? 3 : 4);
            c.suspension = static_cast<Suspension>(pick(3));
            const Tick duration = 1 + pick(100000);
            p.blocks.push_back(ChordBlock{c, cursor, duration});
            cursor += duration + pick(5);
        }
        INFO("iteration " << iteration);
        REQUIRE(p.validate().empty());
        const auto r = deserializeProgression(serializeProgression(p));
        REQUIRE(r.ok);
        REQUIRE(r.progression == p);
    }
}

TEST_CASE("serializeProgression refuses an invalid progression", "[state]") {
    auto p = makeDemoProgression();
    p.blocks[1].startTick = 100;  // overlaps the first block
    CHECK_THROWS_AS(serializeProgression(p), std::invalid_argument);
}

TEST_CASE("deserialize: tolerant of whitespace, unknown fields and a missing suspension", "[state][compat]") {
    SECTION("pretty-printed with unknown fields (forward compatibility)") {
        const std::string text = R"({
            "format": "chordsketch-state",
            "version": 1,
            "futureFeature": {"anything": [1, 2, {"x": null}], "s": "café \"quoted\""},
            "key": {"tonic": 7, "mode": "major", "note": "ignored"},
            "tempoBpm": 90,
            "timeSignature": {"numerator": 3, "denominator": 4},
            "blocks": [
                {"degree": 1, "accidental": 0, "quality": "major", "seventh": "none",
                 "inversion": 0, "startTick": 0, "durationTicks": 2880, "color": "red"}
            ]
        })";
        const auto r = deserializeProgression(text);
        REQUIRE(r.ok);
        CHECK(r.progression.key == Key{7, Mode::Major});
        CHECK(r.progression.tempoBpm == 90.0);
        CHECK(r.progression.timeSignature == TimeSignature{3, 4});
        REQUIRE(r.progression.blocks.size() == 1);
        CHECK(r.progression.blocks[0].chord.suspension == Suspension::None);  // optional field defaulted
        CHECK(r.progression.blocks[0].durationTicks == 2880);
    }
    SECTION("exponent notation for integers is accepted") {
        auto text = replaceFirst(kDemoJson, "\"durationTicks\":3840", "\"durationTicks\":3.84e3");
        CHECK(deserializeProgression(text).ok);
    }
}

TEST_CASE("deserialize: rejects wrong format and versions", "[state][errors]") {
    expectRejected(replaceFirst(kDemoJson, "chordsketch-state", "something-else"));
    expectRejected(replaceFirst(kDemoJson, "\"format\":\"chordsketch-state\",", ""));
    expectRejected(replaceFirst(kDemoJson, "\"version\":1", "\"version\":0"));
    expectRejected(replaceFirst(kDemoJson, "\"version\":1", "\"version\":-3"));
    expectRejected(replaceFirst(kDemoJson, "\"version\":1", "\"version\":\"1\""));
    expectRejected(replaceFirst(kDemoJson, "\"version\":1,", ""));

    const auto newer = deserializeProgression(replaceFirst(kDemoJson, "\"version\":1", "\"version\":2"));
    CHECK_FALSE(newer.ok);
    CHECK(newer.error.find("newer version") != std::string::npos);
}

TEST_CASE("deserialize: rejects missing, mistyped and out-of-range fields", "[state][errors]") {
    expectRejected(replaceFirst(kDemoJson, "\"key\":{\"tonic\":0,\"mode\":\"major\"},", ""));
    expectRejected(replaceFirst(kDemoJson, "\"mode\":\"major\"", "\"mode\":\"dorian\""));
    expectRejected(replaceFirst(kDemoJson, "\"mode\":\"major\"", "\"mode\":1"));
    expectRejected(replaceFirst(kDemoJson, "\"tonic\":0", "\"tonic\":12"));
    expectRejected(replaceFirst(kDemoJson, "\"tonic\":0", "\"tonic\":0.5"));
    expectRejected(replaceFirst(kDemoJson, "\"tonic\":0", "\"tonic\":\"C\""));
    expectRejected(replaceFirst(kDemoJson, "\"tempoBpm\":120", "\"tempoBpm\":5"));
    expectRejected(replaceFirst(kDemoJson, "\"tempoBpm\":120", "\"tempoBpm\":\"fast\""));
    expectRejected(replaceFirst(kDemoJson, "\"tempoBpm\":120,", ""));
    expectRejected(replaceFirst(kDemoJson, "\"denominator\":4", "\"denominator\":3"));
    expectRejected(replaceFirst(kDemoJson, "\"timeSignature\":{\"numerator\":4,\"denominator\":4},", ""));
    expectRejected(replaceFirst(kDemoJson, "\"quality\":\"major\"", "\"quality\":\"sus\""));
    expectRejected(replaceFirst(kDemoJson, "\"seventh\":\"none\"", "\"seventh\":null"));
    expectRejected(replaceFirst(kDemoJson, "\"suspension\":\"none\"", "\"suspension\":\"sus3\""));
    expectRejected(replaceFirst(kDemoJson, "\"degree\":1", "\"degree\":8"));
    expectRejected(replaceFirst(kDemoJson, "\"degree\":1,", ""));
    expectRejected(replaceFirst(kDemoJson, "\"inversion\":0", "\"inversion\":3"));  // triad: max inversion 2
    expectRejected(replaceFirst(kDemoJson, "\"startTick\":0", "\"startTick\":-1"));
    expectRejected(replaceFirst(kDemoJson, "\"startTick\":3840", "\"startTick\":3839"));   // overlaps
    expectRejected(replaceFirst(kDemoJson, "\"durationTicks\":3840", "\"durationTicks\":0"));
    expectRejected(replaceFirst(kDemoJson, "\"durationTicks\":3840", "\"durationTicks\":1.5"));
    expectRejected(replaceFirst(kDemoJson, "\"durationTicks\":3840", "\"durationTicks\":1e30"));
    expectRejected(replaceFirst(kDemoJson, "\"blocks\":[", "\"blocks\":[7,"));
    expectRejected(replaceFirst(kDemoJson, "\"blocks\":[", "\"blocks\":{\"x\":[") );
}

TEST_CASE("deserialize: rejects malformed JSON without crashing", "[state][errors]") {
    expectRejected("");
    expectRejected("   ");
    expectRejected("null");
    expectRejected("[]");
    expectRejected("42");
    expectRejected("\"chordsketch-state\"");
    expectRejected("{}");
    expectRejected("{");
    expectRejected("not json at all");
    expectRejected(kDemoJson + " trailing");
    expectRejected(kDemoJson + kDemoJson);
    expectRejected(kDemoJson.substr(0, kDemoJson.size() / 2));       // truncated
    expectRejected(kDemoJson.substr(0, kDemoJson.size() - 1));       // missing final brace
    expectRejected(replaceFirst(kDemoJson, "\"version\":1", "\"version\":1,\"version\":1"));  // duplicate key
    expectRejected(replaceFirst(kDemoJson, "\"tempoBpm\":120", "\"tempoBpm\":1e999"));
    expectRejected(replaceFirst(kDemoJson, "\"tempoBpm\":120", "\"tempoBpm\":--1"));
    expectRejected(replaceFirst(kDemoJson, "\"tempoBpm\":120", "\"tempoBpm\":NaN"));
    expectRejected(replaceFirst(kDemoJson, "\"major\"", "\"maj\\qor\""));     // bad escape
    expectRejected(replaceFirst(kDemoJson, "\"major\"", "\"maj\\u12\""));     // truncated \u
    expectRejected(replaceFirst(kDemoJson, "\"major\"", "\"maj\nor\""));      // raw control char
    expectRejected(replaceFirst(kDemoJson, ",\"key\"", ",,\"key\""));
    expectRejected(replaceFirst(kDemoJson, "\"tonic\":0", "tonic:0"));
}

TEST_CASE("deserialize: absurd nesting and binary junk fail cleanly", "[state][errors]") {
    expectRejected(std::string(100000, '['));
    expectRejected(std::string(100000, '{'));
    std::string deep;
    for (int i = 0; i < 5000; ++i) deep += "{\"a\":[";
    expectRejected(deep);
    expectRejected(std::string("\x00\x01\x02\xFF\xFE", 5));
    std::string junk(4096, '\0');
    std::mt19937 rng(7);
    for (auto& c : junk) c = static_cast<char>(rng());
    expectRejected(junk);
}

TEST_CASE("property: single-byte corruption never crashes and is either rejected or valid", "[state][errors][property]") {
    std::mt19937 rng(99);
    for (int i = 0; i < 2000; ++i) {
        std::string mutated = kDemoJson;
        mutated[rng() % mutated.size()] = static_cast<char>(rng());
        const auto r = deserializeProgression(mutated);
        if (r.ok) {
            CHECK(r.progression.validate().empty());  // anything accepted is a valid progression
        } else {
            CHECK_FALSE(r.error.empty());
        }
    }
}
