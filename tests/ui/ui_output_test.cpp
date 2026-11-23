// The projector output driven through the real editor: toolbar, Output menu and submenus, shortcuts,
// remembered settings, resolution changes and an unplugged display.
#include "UiTest.h"
#include "ProjectIO.h"

int main() {
    UiTest ui; if (!ui.ok()) return skipTest("no OpenGL 3.3 context available");
    const auto real = queryMonitors();
    if (real.empty() || real[0].w < 3840) return skipTest("needs a desktop at least 3840 px wide for the two made-up displays (CI uses a virtual 3840x1080 one)");
    Project& p = *ui.project;
    const std::vector<MonitorInfo> two = {{"Laptop", 0, 0, 1920, 1080, true, true}, {"Projector", 1920, 0, 1920, 1080, false, false}};
    bool plugged = true;
    const std::function<std::vector<MonitorInfo>()> provider = [&] { return plugged ? two : std::vector<MonitorInfo>{two[0]}; };
    ui.enableOutput(provider);
    OutputState& st = ui.outState;
    p.addSurface(Shape::Quad); ui.editor->markClean();
    ui.warmup();

    const float kShow = 0, kDisplay = 45, kMode = 64, kScaling = 83, kRes = 102;      // rows of the Output menu
    auto openMenu = [&] { ui.click(ui.uiCenter("menu:Output")); ui.wait(2); };

    ui.check("output starts off, with both displays known", [&] { return !st.isOpen && st.monitors.size() == 2 && !st.blackout; });
    ui.click(ui.uiCenter("button:Output")); ui.wait(3);
    ui.check("the toolbar Output button opens the projector window, fullscreen on the projector", [&] { return st.isOpen && st.isFullscreen && st.activeMonitor == 1; });
    ui.click(ui.uiCenter("button:Blackout")); ui.wait(2);
    ui.check("the toolbar Blackout button blacks out the output", [&] { return st.blackout; });
    ui.key(ImGuiKey_B); ui.wait(2);
    ui.check("the B key lifts the blackout", [&] { return !st.blackout; });
    ui.key(ImGuiKey_B); ui.wait(2);
    ui.check("...and B again brings it back", [&] { return st.blackout; });
    ui.key(ImGuiKey_B); ui.wait(2);
    ui.key(ImGuiKey_F11); ui.wait(3);
    ui.check("F11 closes the output", [&] { return !st.isOpen; });
    ui.key(ImGuiKey_F11); ui.wait(3);
    ui.check("F11 opens it again", [&] { return st.isOpen; });
    ui.key(ImGuiKey_F11); ui.wait(3);

    openMenu(); ui.click(UiTest::menuItem(0, kShow)); ui.wait(3);
    ui.check("Output > Show output window opens it", [&] { return st.isOpen; });
    openMenu(); ui.click(UiTest::menuItem(0, kShow)); ui.wait(3);
    ui.check("...and toggles it closed again", [&] { return !st.isOpen; });

    openMenu(); ui.mouseTo(UiTest::menuItem(0, kDisplay)); ui.wait(3); ui.click(UiTest::menuItem(1, 0, 60)); ui.wait(3);        // 1: Laptop
    ui.check("Output > Display chooses a display and remembers it in the settings", [&] { return st.monitorName == "Laptop" && loadSettings().outputMonitor == "Laptop"; });
    ui.click(ui.uiCenter("button:Output")); ui.wait(3);
    ui.check("auto mode on the editor's own display opens a preview window, never fullscreen over the editor", [&] { return st.isOpen && !st.isFullscreen && st.activeMonitor == 0; });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kDisplay)); ui.wait(3); ui.click(UiTest::menuItem(1, 19, 60)); ui.wait(3);      // 2: Projector
    ui.check("choosing the projector moves the window there, fullscreen", [&] { return st.monitorName == "Projector" && st.isFullscreen && st.activeMonitor == 1; });

    openMenu(); ui.mouseTo(UiTest::menuItem(0, kMode)); ui.wait(3); ui.click(UiTest::menuItem(1, 38, 60)); ui.wait(3);           // Preview window
    ui.check("Mode > Preview window forces a window, and is remembered", [&] { return st.mode == OutputMode::Windowed && !st.isFullscreen && loadSettings().outputMode == "windowed"; });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kMode)); ui.wait(3); ui.click(UiTest::menuItem(1, 19, 60)); ui.wait(3);           // Always fullscreen
    ui.check("Always fullscreen goes fullscreen", [&] { return st.mode == OutputMode::Fullscreen && st.isFullscreen && loadSettings().outputMode == "fullscreen"; });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kMode)); ui.wait(3); ui.click(UiTest::menuItem(1, 0, 60)); ui.wait(3);            // Automatic
    ui.check("Automatic restores the smart default", [&] { return st.mode == OutputMode::Auto && st.isFullscreen && loadSettings().outputMode == "auto"; });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kScaling)); ui.wait(3); ui.click(UiTest::menuItem(1, 19, 60)); ui.wait(3);       // Stretch
    ui.check("Scaling > Stretch is applied and remembered", [&] { return st.scale == OutputScale::Stretch && loadSettings().outputScale == "stretch"; });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kScaling)); ui.wait(3); ui.click(UiTest::menuItem(1, 0, 60)); ui.wait(3);        // Fit
    ui.check("...and Fit goes back", [&] { return st.scale == OutputScale::Fit; });

    openMenu(); ui.mouseTo(UiTest::menuItem(0, kRes)); ui.wait(3); ui.click(UiTest::menuItem(1, 25, 60)); ui.wait(4);           // 1280 x 720
    ui.check("Resolution > 1280 x 720 resizes the stage", [&] { return p.outW == 1280 && p.outH == 720 && ui.stage.w == 1280 && ui.stage.h == 720; });
    ui.check("a resolution change is an unsaved project change", [&] { ui.editor->refreshDirtyNow(); return ui.editor->dirty(); });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kRes)); ui.wait(3); ui.click(UiTest::menuItem(1, 0, 60)); ui.wait(4);             // Match the output display
    ui.check("Resolution > Match the output display uses the projector's own pixels", [&] { return p.outW == 1920 && p.outH == 1080 && ui.stage.w == 1920; });
    openMenu(); ui.mouseTo(UiTest::menuItem(0, kRes)); ui.wait(3); ui.click(UiTest::menuItem(1, 107, 60)); ui.wait(3);          // Custom...
    ui.check("Resolution > Custom... opens the size dialog", [] { return UiTest::windowOpen("Output size"); });
    ui.click(UiTest::modalButton("Output size", 1)); ui.wait(3);                                                                // Cancel
    ui.check("Cancel closes it without changing anything", [] { return !UiTest::windowOpen("Output size"); });
    ui.check("the projector shows the stage at the new size", [&] { return st.fbW == 1920 && st.fbH == 1080; });

    ui.act([&] {                                                              // "restarting the app": a fresh editor reads the remembered choices
        OutputState st2; ui.restart(); ui.editor->attachOutput(&st2);
        expect("after a restart the chosen display, mode and scaling are remembered", st2.monitorName == "Projector" && st2.mode == OutputMode::Auto && st2.scale == OutputScale::Fit);
        ui.enableOutput(provider);                                            // reconnect the output controller (a restart discards it)
    });

    ui.act([&] { plugged = false; });                                         // the projector is unplugged during the show
    ui.waitUntil("the output window closes when the display is unplugged", [&] { return !st.isOpen; }, 600);
    ui.check("...and the editor says why instead of crashing", [&] { return !st.wanted && st.message.find("disconnected") != std::string::npos; });
    ui.act([&] { plugged = true; }); ui.wait(90);
    ui.click(ui.uiCenter("button:Output")); ui.wait(3);
    ui.check("plugging it back in and pressing Output works again", [&] { return st.isOpen && st.isFullscreen && st.message.empty(); });
    ui.run();
    return finishTest();
}
