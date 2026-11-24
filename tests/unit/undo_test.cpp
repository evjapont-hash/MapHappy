// The undo history on its own (no OpenGL).
#include "TestSupport.h"
#include "UndoHistory.h"

static UndoHistory::State st(const std::string& s) { return {s, "full:" + s}; }

int main() {
    UndoHistory h;
    expect("nothing to undo at first", !h.canUndo() && !h.canRedo() && !h.stepBack());
    expect("the first state is the starting point, not an edit", !h.commit(st("a")) && !h.canUndo());
    expect("the same state again is not an edit", !h.commit(st("a")) && !h.canUndo());
    expect("a change is", h.commit(st("b")) && h.canUndo());
    h.commit(st("c"));
    const UndoHistory::State* s = h.stepBack();
    expect("undo gives the state before", s && s->full == "full:b" && h.canRedo());
    s = h.stepBack();
    expect("...and the one before that", s && s->full == "full:a" && !h.canUndo());
    s = h.stepForward();
    expect("redo goes forward again", s && s->full == "full:b");
    h.restored("b-tidied");
    expect("what loading tidied up is not an edit", !h.commit(st("b-tidied")) && h.canRedo());
    h.commit(st("d"));
    expect("a new edit drops what could be redone", !h.canRedo() && h.canUndo());
    for (int i = 0; i < 300; i++) h.commit(st("n" + std::to_string(i)));
    size_t steps = 0;
    while (h.stepBack()) steps++;
    expect("the history is limited", steps == UndoHistory::kMaxSteps);
    h.reset(st("z"));
    expect("opening another project forgets it", !h.canUndo() && !h.canRedo());
    return finishTest();
}
