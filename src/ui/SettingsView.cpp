#include "ui/Ui.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Str.h"
#include "platform/win/Autostart.h"
#include "platform/win/ProcessUtil.h"

#include <infclick/Version.h>

#include <algorithm>

namespace infclick::ui {

namespace {

bool g_openLog = false;

void setLanguage(App& app, const char* code)
{
    app.settings().language = code;
    i18n::setLang(i18n::resolve(code));
    app.settingsChanged();
}

void applyTopmost(App& app)
{
    if (HWND h = app.uiWindow())
        SetWindowPos(h, app.settings().alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool navItem(const char* label, bool selected)
{
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x, h = 38 * S();
    const bool clicked = ImGui::InvisibleButton("##nav", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    if (selected) dl->AddRectFilled(p0, p1, alpha(col::violet, 0.18f), 9 * S());
    else if (hov) dl->AddRectFilled(p0, p1, col::panelHover, 9 * S());
    if (selected) dl->AddRectFilled(ImVec2(p0.x + 3 * S(), p0.y + 9 * S()), ImVec2(p0.x + 6 * S(), p1.y - 9 * S()), col::violetSoft, 2 * S());
    ImGui::PushFont(fonts().semibold, 14.0f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p0.x + 16 * S(), p0.y + (h - ts.y) * 0.5f), selected ? col::text : (hov ? col::text : col::dim), label);
    ImGui::PopFont();
    ImGui::PopID();
    return clicked;
}

} // namespace

void openLog() { g_openLog = true; }

void drawSettings(App& app)
{
    AppSettings& st = app.settings();
    if (pageHeader(tr("Settings"))) {
        setPage(Page::Main);
        return;
    }
    ImGui::Dummy(ImVec2(0, 4 * S()));
    const char* names[] = {tr("General"), tr("Hotkeys"), tr("Behavior"), tr("Appearance"), tr("Diagnostics")};
    int tab = std::clamp(currentSubTab(), 0, 4);

    ImGui::BeginChild("##snav", ImVec2(160 * S(), 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    for (int i = 0; i < 5; ++i)
        if (navItem(names[i], tab == i)) setSubTab(tab = i);
    ImGui::EndChild();
    ImGui::SameLine(0, 12 * S());
    ImGui::BeginChild("##scontent", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    beginCard("##scard", ImVec2(0, 0));
    bool changed = false;
    switch (tab) {
    case 0: { // General
        const char* langs[] = {tr("Auto (system)"), "English", "Русский"};
        int cur = st.language == "en" ? 1 : st.language == "ru" ? 2 : 0;
        rowBegin(tr("Language"), tr("Auto follows the Windows display language."), 300 * S(), 34 * S());
        if (segmented("lang", langs, 3, &cur, 300 * S())) setLanguage(app, cur == 1 ? "en" : cur == 2 ? "ru" : "auto");
        rowEnd();

        static bool autoOn = false;
        static int64_t checkedAt = 0;
        if (clk::now() - checkedAt > clk::secToTicks(1)) {
            autoOn = autostart::isEnabled();
            checkedAt = clk::now();
        }
        bool want = autoOn;
        if (toggleRow(tr("Start with Windows"), tr("Starts Infinity Clicker minimized to the tray when you sign in."), &want)) {
            if (autostart::set(want)) autoOn = want;
            else app.setStatus(tr("Could not change the startup entry"));
        }
        changed |= toggleRow(tr("Minimize to tray"), nullptr, &st.minimizeToTray);
        changed |= toggleRow(tr("Close button hides to tray"), tr("Exit from the tray menu."), &st.closeToTray);
        changed |= toggleRow(tr("Start minimized to tray"), nullptr, &st.startMinimized);
        break;
    }
    case 1: { // Hotkeys
        stackedBegin(tr("Emergency stop"),
                     tr("Stops output and disarms regardless of mode, profile or window; releases every held key/button. Detected by "
                        "the keyboard hook, backed up by RegisterHotKey."));
        const bool cap = app.capturing() == CaptureTarget::Emergency;
        if (bindField("emg", st.emergency, cap, app.captureSecondsLeft(), std::min(300.0f * S(), ImGui::GetContentRegionAvail().x - 44 * S()))) {
            if (cap) app.cancelCapture();
            else app.beginCapture(CaptureTarget::Emergency);
        }
        ImGui::SameLine(0, 8 * S());
        KeyChord e = st.emergency;
        if (keyPicker("emgpick", e, true, false) && e.valid()) {
            st.emergency = e;
            changed = true;
        }
        stackedEnd(false);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(tr("The trigger and the action of each profile are set on the main screen."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        break;
    }
    case 2: { // Behavior
        changed |= toggleRow(tr("Arm automatically when Infinity Clicker starts"), nullptr, &st.armOnLaunch);
        changed |= toggleRow(tr("Pause output while Infinity Clicker's own window is in front"),
                             tr("Prevents clicks from landing on Infinity Clicker's own controls."), &st.pauseOnOwnWindow);
        changed |= toggleRow(tr("Accept input injected by other programs"),
                             tr("Treat keys sent by macro tools or remote desktop as real presses. Infinity Clicker's own clicks never count."),
                             &st.acceptInjectedTriggers);
        changed |= toggleRow(tr("Crash guardian process"),
                             tr("A tiny helper process that releases every held key and button if Infinity Clicker is killed or crashes. Takes "
                                "effect on the next start."),
                             &st.guardian);
        break;
    }
    case 3: { // Appearance
        if (toggleRow(tr("Always on top"), nullptr, &st.alwaysOnTop)) {
            applyTopmost(app);
            changed = true;
        }
        break;
    }
    default: { // Diagnostics
        changed |= toggleRow(tr("Write log file"), nullptr, &st.fileLogging);
        changed |= toggleRow(tr("Debug logging"), tr("Adds one-line engine summaries every second while clicking."), &st.debugLogging);
        ImGui::TextUnformatted(tr("Logs and data"));
        const float bw = (ImGui::GetContentRegionAvail().x - 16 * S()) / 3;
        if (ImGui::Button(tr("View log"), ImVec2(bw, 0))) openLog();
        ImGui::SameLine(0, 8 * S());
        if (ImGui::Button(tr("Open logs"), ImVec2(bw, 0))) openPath(paths::dataDir() + L"\\logs");
        ImGui::SameLine(0, 8 * S());
        if (ImGui::Button(tr("Data folder"), ImVec2(bw, 0))) openPath(paths::dataDir());
        ImGui::Dummy(ImVec2(0, 4 * S()));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4 * S()));
        if (app.elevated()) {
            rowBegin(tr("Privileges"), tr("Running as administrator - can send input to elevated windows too."), 10 * S());
            rowEnd();
        } else {
            stackedBegin(tr("Privileges"),
                         tr("Windows blocks input from a normal process into windows running as administrator (User Interface Privilege "
                            "Isolation). SendInput still reports success - the events are silently dropped. If your target app runs "
                            "elevated, restart Infinity Clicker elevated."));
            if (ImGui::Button(tr("Restart as administrator"), ImVec2(230 * S(), 0))) {
                app.saveNow();
                if (procutil::relaunchElevated(L"--restarted")) PostMessageW(app.uiWindow(), WM_CLOSE, 1, 0);
            }
            stackedEnd();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::PushTextWrapPos(0);
        ImGui::Text(tr("Infinity Clicker %s - native C++20 / Win32 / Direct3D 11 / Dear ImGui %s. No network, no telemetry."), INFCLICK_VERSION,
                    IMGUI_VERSION);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        break;
    }
    }
    endCard();
    ImGui::EndChild();
    if (changed) app.settingsChanged();
}

void drawLogModal(App&)
{
    if (g_openLog) {
        ImGui::OpenPopup("##log");
        g_openLog = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter()), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(680 * S(), vp->Size.x - 40 * S()), std::min(420 * S(), vp->Size.y - 40 * S())));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * S(), 16 * S()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * S());
    const bool open = ImGui::BeginPopupModal("##log", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    if (!open) return;
    ImGui::PushFont(fonts().semibold, 20.0f);
    ImGui::TextUnformatted(tr("Log"));
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 20 * S() - 34 * S());
    if (iconButton("##logclose", Icon::Close, tr("Close"), 34 * S()) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::BeginChild("##logtext", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    static uint64_t lastCount = 0;
    static std::vector<std::string> lines;
    if (log::lineCounter() != lastCount || ImGui::IsWindowAppearing()) {
        lastCount = log::lineCounter();
        lines = log::tail(200);
    }
    ImGui::PushFont(fonts().mono, 12.0f);
    for (const auto& l : lines) ImGui::TextUnformatted(l.c_str());
    if (ImGui::IsWindowAppearing() || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::EndPopup();
}

} // namespace infclick::ui
