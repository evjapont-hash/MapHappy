// The Conductor timeline: tracks, clips, keyframes, ghost clips, snapping, the playhead, play/pause, mute and delete.
#include "UiTest.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    Project& p = *ui.project; Timeline& tl = p.timeline;
    p.addSurface(Shape::Quad); p.addSurface(Shape::Circle);              // ids 1 and 2; the newest (Circle-1) is listed first
    ui.warmup();

    auto geom = [&] { return ui.editor->conductor().geom(); };
    auto X = [&](double t) { return [=, &ui] { auto g = ui.editor->conductor().geom(); return (float)(g.canvasX0 + (t - g.scrollT) * g.pps); }; };
    auto laneTop = [&](int lane) { return [=, &ui] { return ui.editor->conductor().geom().laneTop0 + lane * 64.0f; }; };
    auto laneY = [&](int lane, float value) { return [=, &ui] { float top = ui.editor->conductor().geom().laneTop0 + lane * 64.0f + 26, bot = top + 30; return bot - value * (bot - top); }; };
    auto pt = [&](std::function<float()> x, std::function<float()> y) { return [=] { return ImVec2(x(), y()); }; };
    auto rulerY = [&] { return [=, &ui] { return ui.editor->conductor().geom().rulerTop + 22.0f; }; };
    auto track = [&]() { return tl.findTrackByPath("surface/2/opacity"); };
    auto clips = [&]() -> std::vector<Clip>* { Track* t = track(); return t ? &t->clips : nullptr; };

    // ---- Add Track: surface > category > parameter
    ui.click(ui.uiCenter("button:+ Track"));
    ui.mouseTo(UiTest::popupItem("##Popup_", 0)); ui.wait(3);            // hover the first surface
    ui.check("the Add Track popup lists surfaces as menus", [] { return UiTest::menuWindow(0) != nullptr; });
    ui.mouseTo(UiTest::menuItem(0, 0)); ui.wait(3);                       // "Appearance"
    ui.check("each surface lists its categories, each category its parameters", [] { return UiTest::menuWindow(1) != nullptr; });
    ui.click(UiTest::menuItem(1, 0)); ui.wait(2);                         // "Opacity"
    ui.check("choosing a parameter adds its track", [&] { return track() != nullptr && tl.tracks.size() == 1; });

    // ---- clips and keyframes
    ui.rclick(pt(X(1.0), [&] { return laneTop(0)() + 32; }));
    ui.click(UiTest::popupItem("##Popup_", 0, 22)); ui.wait(2);
    ui.check("right-click an empty lane > Create clip here makes a 2 s clip", [&] { auto* c = clips(); return c && c->size() == 1 && nearf((*c)[0].start, 1.0, 0.02) && nearf((*c)[0].duration, 2.0, 0.02); });
    ui.dclick(pt(X(2.0), laneY(0, 0.5f)));
    ui.check("double-clicking inside a clip adds a keyframe where you clicked", [&] { auto* c = clips(); if (!c || c->empty()) return false; auto& k = (*c)[0].data->keys; return k.size() == 3 && nearf(k[1].t, 1.0, 0.03); });
    ui.drag(pt(X(2.0), laneY(0, 0.5f)), pt(X(2.5), laneY(0, 1.0f)));
    ui.check("dragging a keyframe changes its time and value", [&] { auto* c = clips(); if (!c || c->empty()) return false; auto& k = (*c)[0].data->keys; return k.size() == 3 && nearf(k[1].t, 1.5, 0.03) && nearf(k[1].v, 1.0, 0.06); });
    ui.rclick(pt(X(2.5), laneY(0, 1.0f)));
    ui.click(UiTest::popupItem("##Popup_", 0, 22)); ui.wait(2);           // "Linear"
    ui.check("right-click a keyframe > Linear sets its curve", [&] { auto* c = clips(); return c && !c->empty() && (*c)[0].data->keys.size() == 3 && (*c)[0].data->keys[1].interp == Interp::Linear; });

    ui.drag(pt(X(1.5), [&] { return laneTop(0)() + 10; }), pt(X(2.5), [&] { return laneTop(0)() + 10; }));
    ui.check("dragging a clip's header moves it by a second", [&] { auto* c = clips(); return c && !c->empty() && nearf((*c)[0].start, 2.0, 0.03); });
    ui.drag(pt([&] { return X(4.0)() - 3; }, [&] { return laneTop(0)() + 40; }), pt(X(6.0), [&] { return laneTop(0)() + 40; }));
    ui.check("dragging the right edge lengthens it, so it now loops", [&] { auto* c = clips(); return c && !c->empty() && nearf((*c)[0].duration, 4.0, 0.05); });
    ui.check("the lengthened clip repeats its keyframes", [&] { Track* t = track(); float a, b; return t && t->evaluate(2.5, a) && t->evaluate(4.5, b) && nearf(a, b, 1e-3); });

    ui.rclick(pt(X(3.0), [&] { return laneTop(0)() + 44; }));
    ui.click(UiTest::popupItem("##Popup_", 19, 30)); ui.wait(2);          // "Ghost copy"
    ui.check("right-click a clip > Ghost copy adds a ghost that shares keyframes", [&] { auto* c = clips(); return c && c->size() == 2 && (*c)[1].ghost && (*c)[1].data == (*c)[0].data; });

    // ---- playhead and transport
    ui.click(pt(X(3.0), rulerY()));
    ui.check("clicking the ruler moves the playhead, snapped to a frame", [&] { return nearf(p.transport.time, 3.0, 0.03); });
    ui.check("...and the timeline is applied to the surface at that time", [&] { float v; return track()->evaluate(p.transport.time, v) && nearf(p.find(2)->opacity, v, 1e-3); });
    ui.key(ImGuiKey_Space);
    ui.check("Space starts playback", [&] { return p.transport.playing; });
    ui.wait(4);
    ui.check("the playhead advances while playing", [&] { return p.transport.time > 3.0; });
    ui.key(ImGuiKey_Space);
    ui.check("Space pauses it", [&] { return !p.transport.playing; });
    ui.click(ui.uiCenter("button:Stop"));
    ui.check("the Stop button returns the playhead to zero", [&] { return nearf(p.transport.time, 0.0, 1e-6) && !p.transport.playing; });

    // ---- deleting, muting
    ui.click(pt(X(5.0), [&] { return laneTop(0)() + 44; }));
    ui.key(ImGuiKey_Delete);
    ui.check("the Delete key removes the selected clip", [&] { auto* c = clips(); return c && c->size() == 1; });
    ui.click(pt([&] { return geom().canvasX0 - 24; }, [&] { return laneTop(0)() + 16; }));
    ui.check("the M button mutes a track", [&] { return track()->muted; });
    ui.click(pt([&] { return geom().canvasX0 - 24; }, [&] { return laneTop(0)() + 16; }));
    ui.check("...and unmutes it", [&] { return !track()->muted; });
    ui.act([&] { p.removeSurface(2); });
    ui.check("deleting a surface removes its tracks", [&] { return tl.tracks.empty(); });
    ui.run();
    return finishTest();
}
