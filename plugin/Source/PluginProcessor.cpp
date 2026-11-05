#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Serialization.h>

#include <exception>
#include <vector>

ChordSketchProcessor::ChordSketchProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    const juce::CriticalSection::ScopedLockType lock(documentLock);
    rebuildPlaybackLocked();  // the demo progression plays until a project is loaded or edited
}

void ChordSketchProcessor::rebuildPlaybackLocked() {
    const auto& current = document.progression();
    fallbackTempo.store(current.tempoBpm);

    std::vector<chordcore::NoteEvent> notes;
    try {
        notes = chordcore::generateNotes(current);  // the same notes the file export uses
    } catch (const std::exception&) {
        // An invalid progression plays nothing (Document never holds one, so this is defensive).
    }

    // setNotes() allocates; it runs here on the editing thread, with the audio thread locked out only for
    // the duration of the swap (the audio thread try-locks and skips the block).
    const juce::SpinLock::ScopedLockType lock(schedulerLock);
    scheduler.setNotes(std::move(notes));
}

ChordSketchProcessor::Playhead ChordSketchProcessor::getPlayhead() const noexcept {
    Playhead head;
    head.valid = playheadValid.load(std::memory_order_relaxed);
    head.playing = playheadPlaying.load(std::memory_order_relaxed);
    head.ppq = playheadPpq.load(std::memory_order_relaxed);
    return head;
}

chordcore::Progression ChordSketchProcessor::getProgression() const {
    const juce::CriticalSection::ScopedLockType lock(documentLock);
    return document.progression();
}

ChordSketchProcessor::HistoryInfo ChordSketchProcessor::getHistoryInfo() const {
    const juce::CriticalSection::ScopedLockType lock(documentLock);
    HistoryInfo info;
    info.canUndo = document.canUndo();
    info.canRedo = document.canRedo();
    info.undoLabel = document.undoLabel();
    info.redoLabel = document.redoLabel();
    return info;
}

chordcore::Document::Result ChordSketchProcessor::editProgression(
    const std::string& label, const std::function<void(chordcore::Progression&)>& mutate,
    const std::string& coalesceKey) {
    chordcore::Document::Result result;
    bool changed = false;
    {
        const juce::CriticalSection::ScopedLockType lock(documentLock);
        const auto before = document.revision();
        result = document.edit(label, mutate, coalesceKey);
        changed = document.revision() != before;
        if (changed) rebuildPlaybackLocked();
    }
    if (changed) broadcaster.sendChangeMessage();
    return result;
}

bool ChordSketchProcessor::undo() {
    bool done = false;
    {
        const juce::CriticalSection::ScopedLockType lock(documentLock);
        done = document.undo();
        if (done) rebuildPlaybackLocked();
    }
    if (done) broadcaster.sendChangeMessage();
    return done;
}

bool ChordSketchProcessor::redo() {
    bool done = false;
    {
        const juce::CriticalSection::ScopedLockType lock(documentLock);
        done = document.redo();
        if (done) rebuildPlaybackLocked();
    }
    if (done) broadcaster.sendChangeMessage();
    return done;
}

std::string ChordSketchProcessor::getLastLoadError() const {
    const juce::CriticalSection::ScopedLockType lock(documentLock);
    return lastLoadError;
}

std::vector<std::uint8_t> ChordSketchProcessor::exportMidiBytes() const {
    return chordcore::exportProgressionToMidi(getProgression());
}

void ChordSketchProcessor::prepareToPlay(double sampleRate, int) {
    currentSampleRate = sampleRate;
    const juce::SpinLock::ScopedLockType lock(schedulerLock);
    scheduler.reset();
}

void ChordSketchProcessor::releaseResources() {
    const juce::SpinLock::ScopedLockType lock(schedulerLock);
    scheduler.reset();
}

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
    block.bpm = fallbackTempo.load();  // fallback only: the host tempo is used whenever it is provided

    // Without a host position there is nothing to synchronise to, so the block is treated as stopped.
    bool havePosition = false;
    if (auto* hostPlayHead = getPlayHead()) {
        if (const auto position = hostPlayHead->getPosition()) {
            const auto ppq = position->getPpqPosition();
            if (ppq.hasValue()) {
                block.playing = position->getIsPlaying();
                block.ppqStart = *ppq;
                havePosition = true;
                const auto bpm = position->getBpm();
                if (bpm.hasValue()) block.bpm = *bpm;
            }
        }
    }

    playheadPpq.store(block.ppqStart, std::memory_order_relaxed);
    playheadPlaying.store(block.playing, std::memory_order_relaxed);
    playheadValid.store(havePosition, std::memory_order_relaxed);

    // Never block the audio thread: if an edit is swapping the note list, skip this block.
    const juce::SpinLock::ScopedTryLockType lock(schedulerLock);
    if (!lock.isLocked()) return;

    for (const auto& e : scheduler.process(block)) {
        midi.addEvent(e.noteOn ? juce::MidiMessage::noteOn(e.channel + 1, e.pitch, (juce::uint8) e.velocity)
                               : juce::MidiMessage::noteOff(e.channel + 1, e.pitch, (juce::uint8) 0),
                      e.sampleOffset);
    }
}

juce::AudioProcessorEditor* ChordSketchProcessor::createEditor() { return new ChordSketchEditor(*this); }

void ChordSketchProcessor::getStateInformation(juce::MemoryBlock& destData) {
    try {
        const auto text = chordcore::serializeProgression(getProgression());
        destData.append(text.data(), text.size());
    } catch (const std::exception&) {
        // Unreachable for a valid document; saving nothing is safer than saving garbage.
    }
}

void ChordSketchProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0) return;  // a fresh instance: keep the default project

    const std::string text(static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes));
    const auto loaded = chordcore::deserializeProgression(text);
    {
        const juce::CriticalSection::ScopedLockType lock(documentLock);
        if (loaded.ok) {
            const auto applied = document.replace(loaded.progression);
            lastLoadError = applied.ok ? std::string() : applied.error;
            if (applied.ok) rebuildPlaybackLocked();
        } else {
            lastLoadError = loaded.error;  // keep the current project; the editor shows the reason
        }
    }
    broadcaster.sendChangeMessage();
}

// Entry point used by the JUCE plugin client wrapper.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordSketchProcessor(); }
