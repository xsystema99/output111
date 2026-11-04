#include <chordcore/Document.h>

#include <exception>

namespace chordcore {

Document::Document(Progression initial, std::size_t maxHistory)
    : current_(std::move(initial)), maxHistory_(maxHistory == 0 ? 1 : maxHistory) {}

Document::Result Document::edit(const std::string& label, const std::function<void(Progression&)>& mutate,
                                const std::string& coalesceKey) {
    Progression candidate = current_;
    try {
        mutate(candidate);
    } catch (const std::exception& e) {
        return {false, std::string("edit failed: ") + e.what()};
    }

    const auto problems = candidate.validate();
    if (!problems.empty()) return {false, "invalid edit: " + problems.front()};

    if (candidate == current_) return {true, {}};  // nothing changed: no history, no new revision

    const bool merge = !coalesceKey.empty() && coalesceKey == lastCoalesceKey_ && !undo_.empty();
    if (!merge) {
        undo_.push_back({current_, label});
        trim();
    }
    redo_.clear();
    current_ = std::move(candidate);
    lastCoalesceKey_ = coalesceKey;
    ++revision_;
    return {true, {}};
}

std::string Document::undoLabel() const { return undo_.empty() ? std::string() : undo_.back().label; }
std::string Document::redoLabel() const { return redo_.empty() ? std::string() : redo_.back().label; }

bool Document::undo() {
    if (undo_.empty()) return false;
    Entry entry = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({std::move(current_), entry.label});
    current_ = std::move(entry.state);
    lastCoalesceKey_.clear();
    ++revision_;
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    Entry entry = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({std::move(current_), entry.label});
    current_ = std::move(entry.state);
    lastCoalesceKey_.clear();
    ++revision_;
    return true;
}

Document::Result Document::replace(Progression progression) {
    const auto problems = progression.validate();
    if (!problems.empty()) return {false, "invalid progression: " + problems.front()};
    current_ = std::move(progression);
    undo_.clear();
    redo_.clear();
    lastCoalesceKey_.clear();
    ++revision_;
    return {true, {}};
}

void Document::trim() {
    if (undo_.size() > maxHistory_) {
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - maxHistory_));
    }
}

}  // namespace chordcore
