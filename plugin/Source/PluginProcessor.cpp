#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <chordcore/MidiFile.h>

ChordSketchProcessor::ChordSketchProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      progression(chordcore::makeDemoProgression()) {}

std::vector<std::uint8_t> ChordSketchProcessor::exportMidiBytes() const {
    return chordcore::exportProgressionToMidi(progression);
}

void ChordSketchProcessor::prepareToPlay(double, int) {}
void ChordSketchProcessor::releaseResources() {}

bool ChordSketchProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChordSketchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    // M1: silent, and no MIDI output yet (milestone 3 adds transport-synchronised MIDI generation).
    buffer.clear();
    midi.clear();
}

juce::AudioProcessorEditor* ChordSketchProcessor::createEditor() { return new ChordSketchEditor(*this); }

void ChordSketchProcessor::getStateInformation(juce::MemoryBlock&) {}  // milestone 4
void ChordSketchProcessor::setStateInformation(const void*, int) {}    // milestone 4

// Entry point used by the JUCE plugin client wrapper.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordSketchProcessor(); }
