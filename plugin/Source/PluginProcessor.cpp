#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>

#include <exception>

ChordSketchProcessor::ChordSketchProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      progression(chordcore::makeDemoProgression()) {
    try {
        scheduler.setNotes(chordcore::generateNotes(progression));  // the same notes the file export uses
    } catch (const std::exception&) {
        // An invalid progression plays nothing; the editor reports it.
    }
}

std::vector<std::uint8_t> ChordSketchProcessor::exportMidiBytes() const {
    return chordcore::exportProgressionToMidi(progression);
}

void ChordSketchProcessor::prepareToPlay(double sampleRate, int) {
    currentSampleRate = sampleRate;
    scheduler.reset();
}

void ChordSketchProcessor::releaseResources() { scheduler.reset(); }

bool ChordSketchProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChordSketchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();  // this plugin makes no sound; it only produces MIDI
    midi.clear();    // incoming MIDI is ignored (the input bus exists because Live requires one)

    chordcore::TransportBlock block;
    block.sampleRate = currentSampleRate;
    block.numSamples = buffer.getNumSamples();
    block.bpm = progression.tempoBpm;  // fallback only: the host tempo is used whenever it is provided

    // Without a host position there is nothing to synchronise to, so the block is treated as stopped.
    if (auto* hostPlayHead = getPlayHead()) {
        if (const auto position = hostPlayHead->getPosition()) {
            const auto ppq = position->getPpqPosition();
            if (ppq.hasValue()) {
                block.playing = position->getIsPlaying();
                block.ppqStart = *ppq;
                const auto bpm = position->getBpm();
                if (bpm.hasValue()) block.bpm = *bpm;
            }
        }
    }

    for (const auto& e : scheduler.process(block)) {
        midi.addEvent(e.noteOn ? juce::MidiMessage::noteOn(e.channel + 1, e.pitch, (juce::uint8) e.velocity)
                               : juce::MidiMessage::noteOff(e.channel + 1, e.pitch, (juce::uint8) 0),
                      e.sampleOffset);
    }
}

juce::AudioProcessorEditor* ChordSketchProcessor::createEditor() { return new ChordSketchEditor(*this); }

void ChordSketchProcessor::getStateInformation(juce::MemoryBlock&) {}  // milestone 4
void ChordSketchProcessor::setStateInformation(const void*, int) {}    // milestone 4

// Entry point used by the JUCE plugin client wrapper.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordSketchProcessor(); }
