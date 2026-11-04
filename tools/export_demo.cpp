// Writes the demo progression (C major: I - V - vi - IV) to a .mid file using the same
// pipeline as the plugin. Lets the export be checked with no plugin or DAW involved.
//
// usage: export_demo [output.mid]

#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>

#include <cstdio>
#include <exception>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "demo.mid";

    try {
        const auto progression = chordcore::makeDemoProgression();
        const auto notes = chordcore::generateNotes(progression);
        const auto bytes = chordcore::exportProgressionToMidi(progression);

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "cannot open '%s' for writing\n", path.c_str());
            return 1;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::fprintf(stderr, "write to '%s' failed\n", path.c_str());
            return 1;
        }

        std::printf("wrote %s (%zu bytes, %zu notes)\n", path.c_str(), bytes.size(), notes.size());
        for (const auto& n : notes) {
            std::printf("  pitch %3d  start %6lld  length %5lld  vel %d  ch %d\n", n.pitch,
                        static_cast<long long>(n.startTick), static_cast<long long>(n.lengthTicks), n.velocity,
                        n.channel + 1);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
