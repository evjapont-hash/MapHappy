#pragma once
#include <string>
#include <utility>
#include <vector>

// Undo and redo as whole-project snapshots (JSON text: a show is small, and the video and audio files aren't in it).
// No UI or OpenGL in here; the editor decides when an edit is finished and how to put a snapshot back.
class UndoHistory {
public:
    struct State {
        std::string check;       // decides whether anything was edited (leaves out what merely playing or selecting changes)
        std::string full;        // what is put back
    };
    static constexpr size_t kMaxSteps = 100;

    bool started() const { return started_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // Forgets everything: the project starts from this state (a new or just opened project).
    void reset(State s) { undo_.clear(); redo_.clear(); now_ = std::move(s); started_ = true; }
    void clear() { undo_.clear(); redo_.clear(); now_ = State(); started_ = false; }

    // The project as it is now. Returns true if that was an edit (a new undo step; what could be redone is gone).
    bool commit(State s) {
        if (!started_) { reset(std::move(s)); return false; }
        if (s.check == now_.check) return false;
        undo_.push_back(std::move(now_));
        if (undo_.size() > kMaxSteps) undo_.erase(undo_.begin());
        redo_.clear();
        now_ = std::move(s);
        return true;
    }
    // Step back / forward: the state to put back, or nullptr. After putting it back, call restored().
    const State* stepBack()    { return step(undo_, redo_); }
    const State* stepForward() { return step(redo_, undo_); }
    // Loading tidies some things up; what it really became is what the next edit is compared with.
    void restored(const std::string& check) { now_.check = check; }

private:
    const State* step(std::vector<State>& from, std::vector<State>& to) {
        if (from.empty()) return nullptr;
        to.push_back(std::move(now_));
        now_ = std::move(from.back());
        from.pop_back();
        return &now_;
    }
    std::vector<State> undo_, redo_;
    State now_;
    bool started_ = false;
};
