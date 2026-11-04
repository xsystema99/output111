#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <chordcore/Progression.h>

namespace chordcore {

/// The editable project: the current progression plus its undo/redo history.
///
/// All edits go through edit(), which works on a copy, validates the result and only then commits, so
/// the current progression is always valid and a rejected edit changes nothing (history included).
/// History is snapshot based (the progression is a small value type), which keeps every operation
/// trivially correct. It is not persisted: a reloaded project starts with an empty history.
///
/// Not thread-safe: callers serialise access (the plugin does so with a lock).
class Document {
public:
    struct Result {
        bool ok = false;
        std::string error;
    };

    explicit Document(Progression initial = makeDemoProgression(), std::size_t maxHistory = 200);

    const Progression& progression() const noexcept { return current_; }

    /// Applies `mutate` to a copy of the progression. Rejected (nothing changes, nothing is recorded) if the
    /// result is invalid or `mutate` throws. An edit that leaves the progression unchanged is accepted but
    /// records no history.
    ///
    /// `coalesceKey`: consecutive edits with the same non-empty key (e.g. "tempo" while dragging a slider)
    /// share one undo step. Any undo, redo, load or edit with a different key ends the run.
    Result edit(const std::string& label, const std::function<void(Progression&)>& mutate,
                const std::string& coalesceKey = {});

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    /// Labels of the edit that undo()/redo() would revert/reapply; empty when unavailable.
    std::string undoLabel() const;
    std::string redoLabel() const;

    bool undo();  ///< false if there is nothing to undo
    bool redo();  ///< false if there is nothing to redo

    /// Replaces the whole project (loading a saved state) and clears the history. Rejects an invalid progression.
    Result replace(Progression progression);

    /// Increases by one on every change of the progression (edit, undo, redo, replace), never otherwise.
    std::uint64_t revision() const noexcept { return revision_; }

private:
    struct Entry {
        Progression state;  ///< undo stack: state before the edit; redo stack: state after the edit
        std::string label;
    };

    void trim();

    Progression current_;
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    std::string lastCoalesceKey_;
    std::size_t maxHistory_;
    std::uint64_t revision_ = 0;
};

}  // namespace chordcore
