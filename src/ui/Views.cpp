#include "ui/Ui.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Str.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace infclick::ui {

namespace {

Page g_page = Page::Main;
int g_sub = 0;
bool g_openProfiles = false;

const char* pauseText(PauseReason r)
{
    switch (r) {
    case PauseReason::OwnWindow: return tr("Paused: Infinity Clicker's own window is in front");
    case PauseReason::TestGate: return tr("Paused: move the cursor over the Test Pad");
    case PauseReason::TargetNotForeground: return tr("Paused: the target application is not in front");
    default: return "";
    }
}

// ghost button with icon + label, used in the header
bool headerButton(Icon icon, const char* label)
{
    ImGui::PushID(label);
    ImGui::PushFont(fonts().semibold, 13.5f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    ImGui::PushFont(fonts().body, 16.0f);
    const ImVec2 is = ImGui::CalcTextSize(ic(icon));
    ImGui::PopFont();
    const ImVec2 size(is.x + ts.x + 30 * S(), 34 * S());
    const bool clicked = ImGui::InvisibleButton("##hb", size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    if (hov) dl->AddRectFilled(p0, p1, col::panelHover, 9 * S());
    const ImU32 c = hov ? col::text : col::dim;
    ImGui::PushFont(fonts().body, 16.0f);
    dl->AddText(ImVec2(p0.x + 10 * S(), p0.y + (size.y - is.y) * 0.5f), c, ic(icon));
    ImGui::PopFont();
    dl->AddText(ImVec2(p0.x + 20 * S() + is.x, p0.y + (size.y - ts.y) * 0.5f), c, label);
    ImGui::PopFont();
    ImGui::PopID();
    return clicked;
}

double toDisplay(const Profile& p)
{
    switch (p.rateUnit) {
    case RateUnit::IntervalMs: return 1000.0 / p.cps;
    case RateUnit::IntervalUs: return 1e6 / p.cps;
    default: return p.cps;
    }
}

double fromDisplay(const Profile& p, double v)
{
    double cps = v;
    if (p.rateUnit == RateUnit::IntervalMs) cps = 1000.0 / v;
    else if (p.rateUnit == RateUnit::IntervalUs) cps = 1e6 / v;
    return std::clamp(cps, 0.01, 1'000'000.0);
}

const char* unitLabel(RateUnit u)
{
    switch (u) {
    case RateUnit::IntervalMs: return tr("ms");
    case RateUnit::IntervalUs: return tr("us");
    default: return "CPS";
    }
}

// Mode-specific sentence shown under ARMED / STOP.
std::string armedHint(const Profile& p, bool running)
{
    const std::string trig = p.trigger.name();
    switch (p.mode.mode) {
    case TriggerMode::Hold: return strFormat(running ? tr("Release %s to stop") : tr("Hold %s"), trig.c_str());
    case TriggerMode::Toggle:
        return running ? strFormat(tr("Press %s to stop"), trig.c_str()) : strFormat(tr("Press %s to start / stop"), trig.c_str());
    case TriggerMode::Burst:
        return running ? std::string(tr("Running...")) : strFormat(tr("Press %s for %llu actions"), trig.c_str(), (unsigned long long)p.mode.burstCount);
    case TriggerMode::Count:
        return running ? std::string(tr("Running...")) : strFormat(tr("Press %s to run %llu actions"), trig.c_str(), (unsigned long long)p.mode.fixedCount);
    case TriggerMode::Duration:
        return running ? std::string(tr("Running...")) : strFormat(tr("Press %s to run for %s"), trig.c_str(), fmtUs(double(p.mode.durationMs) * 1000.0).c_str());
    }
    return {};
}

void centeredText(const char* text, ImU32 color)
{
    ImGui::PushFont(fonts().body, 13.5f);
    const float w = ImGui::CalcTextSize(text).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - w) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(color));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ------------------------------------------------------------------ main screen

void drawHeader(App& app)
{
    const float y0 = ImGui::GetCursorPosY();
    const float x0 = ImGui::GetCursorPosX();
    const float right = x0 + ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushFont(fonts().display, 25.0f);
    ImGui::TextUnformatted("Infinity Clicker");
    const ImVec2 tsz = ImGui::GetItemRectSize();
    ImGui::PopFont();
    dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + tsz.y + 1), ImVec2(p.x + tsz.x, p.y + tsz.y + 3 * S()), col::violet, col::cyan,
                                col::cyan, col::violet);

    // Settings button + status on the right
    ImGui::PushFont(fonts().semibold, 13.5f);
    const float sw = ImGui::CalcTextSize(tr("Settings")).x;
    ImGui::PopFont();
    ImGui::PushFont(fonts().body, 16.0f);
    const float iw = ImGui::CalcTextSize(ic(Icon::Settings)).x;
    ImGui::PopFont();
    const float btnW = iw + sw + 30 * S();
    ImGui::SameLine(right - btnW);
    ImGui::SetCursorPosY(y0 + 3 * S());
    if (headerButton(Icon::Settings, tr("Settings"))) setPage(Page::Settings);
    ImGui::SameLine(right - btnW - 18 * S() - statusIndicatorWidth(app.snap().state));
    ImGui::SetCursorPosY(y0 + 11 * S());
    statusIndicator(app.snap().state);
    ImGui::SetCursorPosY(y0 + tsz.y + 8 * S());
}

void drawProfileRow(App& app)
{
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
    ImGui::TextUnformatted(tr("Profile"));
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 10 * S());
    ImGui::SetNextItemWidth(220 * S());
    const std::string cur = app.profile().name;
    if (ImGui::BeginCombo("##profile", cur.c_str())) {
        for (const Profile& p2 : app.store().profiles())
            if (ImGui::Selectable(p2.name.c_str(), p2.name == cur)) app.selectProfile(p2.name);
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 6 * S());
    if (iconButton("##manage", Icon::Edit, tr("Manage profiles"), ImGui::GetFrameHeight())) openProfileManager();
}

bool drawInputCard(App& app, float w, float h)
{
    Profile& p = app.profile();
    bool changed = false;
    beginCard("##input", ImVec2(w, h));
    const float pickW = 36 * S(), gap = 8 * S();

    fieldLabel(tr("TRIGGER"));
    {
        const bool cap = app.capturing() == CaptureTarget::Trigger;
        if (bindField("trig", p.trigger, cap, app.captureSecondsLeft(), ImGui::GetContentRegionAvail().x - pickW - gap)) {
            if (cap) app.cancelCapture();
            else app.beginCapture(CaptureTarget::Trigger);
        }
        ImGui::SameLine(0, gap);
        if (keyPicker("trigpick", p.trigger, true, true)) changed = true;
    }
    ImGui::Dummy(ImVec2(0, 2 * S()));
    fieldLabel(tr("ACTION"));
    {
        const bool cap = app.capturing() == CaptureTarget::Action;
        if (bindField("act", p.action.chord, cap, app.captureSecondsLeft(), ImGui::GetContentRegionAvail().x - pickW - gap)) {
            if (cap) app.cancelCapture();
            else app.beginCapture(CaptureTarget::Action);
        }
        ImGui::SameLine(0, gap);
        if (keyPicker("actpick", p.action.chord, true, true)) changed = true;
    }
    ImGui::Dummy(ImVec2(0, 2 * S()));
    {
        const float labelY = ImGui::GetCursorPosY();
        fieldLabel(tr("MODE"));
        const float belowLabel = ImGui::GetCursorPosY();
        // The value of the selected mode (burst size / count / duration) shares the label line, right-aligned.
        if (p.mode.mode == TriggerMode::Burst || p.mode.mode == TriggerMode::Count || p.mode.mode == TriggerMode::Duration) {
            const bool dur = p.mode.mode == TriggerMode::Duration;
            const char* unit = dur ? tr("ms") : tr("actions");
            ImGui::PushFont(fonts().body, 13.0f);
            const float unitW = ImGui::CalcTextSize(unit).x + 8 * S();
            ImGui::PopFont();
            const float fieldW = 92 * S();
            const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPos(ImVec2(right - fieldW - unitW, labelY - 6 * S()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * S(), 3 * S()));
            ImGui::PushID("modeparam");
            ImGui::SetNextItemWidth(fieldW);
            uint64_t n = dur ? uint64_t(p.mode.durationMs) : (p.mode.mode == TriggerMode::Burst ? p.mode.burstCount : p.mode.fixedCount);
            static char buf[24]; // holds the edit in progress; refreshed from the profile whenever the field is not active
            if (ImGui::GetActiveID() != ImGui::GetID("##v")) std::snprintf(buf, sizeof buf, "%llu", (unsigned long long)n);
            ImGui::InputText("##v", buf, sizeof buf, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const uint64_t v = std::strtoull(buf, nullptr, 10);
                if (dur) p.mode.durationMs = uint32_t(std::clamp<uint64_t>(v, 1, 86'400'000));
                else if (p.mode.mode == TriggerMode::Burst) p.mode.burstCount = std::clamp<uint64_t>(v, 1, 1'000'000'000ull);
                else p.mode.fixedCount = std::clamp<uint64_t>(v, 1, 1'000'000'000'000ull);
                changed = true;
            }
            ImGui::PopID();
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 0);
            ImGui::PushFont(fonts().body, 13.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
            ImGui::SetCursorPosX(right - unitW + 8 * S());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(unit);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SetCursorPosY(std::max(belowLabel, ImGui::GetCursorPosY()));
        }
        const TriggerMode m = p.mode.mode;
        const bool hidden = m == TriggerMode::Count || m == TriggerMode::Duration;
        const char* moreLabel = m == TriggerMode::Count ? tr("Fixed count") : m == TriggerMode::Duration ? tr("Duration") : tr("More");
        const char* labels[] = {tr("Hold"), tr("Toggle"), tr("Burst"), moreLabel};
        const int sel = hidden ? 3 : int(m); // Hold 0, Toggle 1, Burst 2
        const float x = ImGui::GetCursorScreenPos().x, y = ImGui::GetCursorScreenPos().y;
        const int clicked = segmentedClick("mode", labels, 4, sel, ImGui::GetContentRegionAvail().x);
        if (clicked >= 0 && clicked <= 2) {
            p.mode.mode = TriggerMode(clicked); // enum order: Hold, Toggle, Burst
            changed = true;
        } else if (clicked == 3) {
            ImGui::OpenPopup("moremodes");
        }
        ImGui::SetNextWindowPos(ImVec2(x + ImGui::GetContentRegionAvail().x * 0.5f, y + 38 * S()));
        if (ImGui::BeginPopup("moremodes")) {
            if (ImGui::Selectable(tr("Fixed count"), m == TriggerMode::Count)) {
                p.mode.mode = TriggerMode::Count;
                changed = true;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
            ImGui::TextUnformatted(tr("Stop after a set number of actions"));
            ImGui::PopStyleColor();
            ImGui::Separator();
            if (ImGui::Selectable(tr("Duration"), m == TriggerMode::Duration)) {
                p.mode.mode = TriggerMode::Duration;
                changed = true;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
            ImGui::TextUnformatted(tr("Run for a set amount of time"));
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
    }
    endCard();
    if (changed) app.profileChanged();
    return changed;
}

void drawSpeedCard(App& app, float w, float h)
{
    Profile& p = app.profile();
    const EngineSnapshot& s = app.snap();
    const bool running = s.state == EngineState::Running || s.state == EngineState::Paused;
    bool changed = false;
    beginCard("##speed", ImVec2(w, h));
    fieldLabel(tr("SPEED"));

    if (p.maxMode) {
        ImGui::PushFont(fonts().display, 34.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::violetSoft));
        ImGui::TextUnformatted("MAX");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(tr("Experimental: as fast as Windows accepts input. Turn it off in Advanced settings."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else {
        // Big editable number. The text buffer is only applied when editing finishes, so typing "500"
        // never applies 5 and 50 on the way.
        static char buf[32];
        const ImGuiID id = ImGui::GetID("##speedvalue");
        if (ImGui::GetActiveID() != id) std::snprintf(buf, sizeof buf, "%.10g", toDisplay(p));
        ImGui::PushFont(fonts().display, 34.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * S(), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, v4(alpha(col::panel2, 0.9f)));
        const float numW = std::clamp(ImGui::CalcTextSize(buf).x + 26 * S(), 70.0f * S(), ImGui::GetContentRegionAvail().x * 0.7f);
        ImGui::SetNextItemWidth(numW);
        ImGui::InputText("##speedvalue", buf, sizeof buf, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
        const bool commit = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        if (commit) {
            std::string t = buf;
            std::replace(t.begin(), t.end(), ',', '.'); // decimal comma
            char* end = nullptr;
            const double v = std::strtod(t.c_str(), &end);
            if (end != t.c_str() && std::isfinite(v) && v > 0) {
                p.cps = fromDisplay(p, v);
                changed = true;
            }
        }
        ImGui::SameLine(0, 6 * S());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14 * S());
        ImGui::PushFont(fonts().semibold, 15.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::TextUnformatted(unitLabel(p.rateUnit));
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::PushFont(fonts().body, 13.0f);
        if (p.rateUnit == RateUnit::Cps) ImGui::Text(tr("= one click every %s"), fmtInterval(p.cps).c_str());
        else ImGui::Text(tr("= %s CPS"), fmtRate(p.cps).c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 2 * S()));
        double v = std::clamp(p.cps, 1.0, 1000.0);
        const double mn = 1.0, mx = 1000.0;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4 * S()));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 16 * S());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(col::panel2));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderScalar("##cpsslider", ImGuiDataType_Double, &v, &mn, &mx, "", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
            p.cps = v >= 100 ? std::round(v) : std::round(v * 10.0) / 10.0;
            changed = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        // Presets (always CPS values; labelled in the current unit).
        const double presets[] = {10, 15, 20, 50, 100, 500};
        float used = 0;
        const float availW = ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < std::size(presets); ++i) {
            const std::string label = p.rateUnit == RateUnit::Cps ? strFormat("%.0f", presets[i]) : fmtInterval(presets[i]);
            ImGui::PushFont(fonts().semibold, 13.0f);
            const float cw = ImGui::CalcTextSize(label.c_str()).x + 22 * S();
            ImGui::PopFont();
            if (i && used + cw + 6 * S() <= availW) ImGui::SameLine(0, 6 * S());
            else if (i) used = 0;
            if (chip(label.c_str(), std::fabs(p.cps - presets[i]) < 1e-9)) {
                p.cps = presets[i];
                changed = true;
            }
            used += cw + 6 * S();
        }
    }

    // "Actual" is anchored to the bottom of the card.
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 14 * S() - ImGui::GetTextLineHeight() - (running && s.cps1s < p.cps * 0.95 && s.sendShare > 0.5 && !p.maxMode ? 16 * S() : 0));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
    ImGui::TextUnformatted(tr("Actual"));
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 10 * S());
    const double shown = running ? s.cps1s : s.runAvgCps;
    ImGui::PushFont(fonts().semibold, 15.0f);
    if (s.runId || running) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(running ? col::green : col::text));
        ImGui::Text("%s CPS", fmtRate(shown).c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::TextUnformatted("-");
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    if (running && !p.maxMode && s.runElapsedSec > 1.5 && s.cps1s < p.cps * 0.95 && s.sendShare > 0.5) {
        ImGui::PushFont(fonts().body, 12.5f);
        ImGui::TextColored(v4(col::amber), "%s", tr("Windows is slowing input down right now"));
        ImGui::PopFont();
    }
    endCard();
    if (changed) app.profileChanged();
}

void drawArmButton(App& app)
{
    Profile& p = app.profile();
    const EngineSnapshot& s = app.snap();
    const bool armed = app.armed();
    const bool running = s.state == EngineState::Running;
    const bool paused = s.state == EngineState::Paused;
    const float h = 56 * S();
    bool clicked;
    if (paused) {
        clicked = bigButton(tr("PAUSED"), pauseText(s.pause), BigButton::Paused, h);
    } else if (running) {
        clicked = bigButton(tr("STOP"), armedHint(p, true).c_str(), BigButton::Stop, h);
    } else if (armed) {
        clicked = bigButton(tr("ARMED"), armedHint(p, false).c_str(), BigButton::Armed, h);
    } else {
        clicked = bigButton(tr("ARM"), tr("Enable the trigger"), BigButton::Arm, h);
    }
    if (clicked) {
        if (running || paused) app.stop();
        else app.arm(!armed);
    }
    if (ImGui::IsItemHovered()) {
        if (paused || running) ImGui::SetTooltip("%s", tr("Stop clicking"));
        else if (armed) ImGui::SetTooltip("%s", tr("Click to disarm"));
    }

    // One line of context under the button.
    const std::string filterInfo = strFormat(tr("Only while %s is in front"), p.targetProcess.c_str());
    ImGui::Dummy(ImVec2(0, 2 * S()));
    if (s.runFailedCalls >= 3) {
        centeredText(s.lastError == ERROR_ACCESS_DENIED ? tr("Windows rejected the input (screen locked or a UAC prompt is open)")
                                                        : tr("Windows rejected some input events"),
                     col::amber);
    } else if (!app.statusMessage().empty()) {
        centeredText(app.statusMessage().c_str(), col::cyan);
    } else if (p.filterEnabled && !p.targetProcess.empty()) {
        centeredText(filterInfo.c_str(), col::dim);
    } else {
        centeredText(strFormat(tr("Emergency stop: %s"), app.settings().emergency.name().c_str()).c_str(), col::dim);
    }
}

void drawMain(App& app)
{
    drawHeader(app);
    drawProfileRow(app);

    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = 12 * S();
    const float leftW = std::floor((W - gap) * 0.54f);
    const float cardH = 240 * S();
    drawInputCard(app, leftW, cardH);
    ImGui::SameLine(0, gap);
    drawSpeedCard(app, W - gap - leftW, cardH);

    ImGui::Dummy(ImVec2(0, 4 * S()));
    drawArmButton(app);

    // footer: pinned to the bottom of the window
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 6 * S(),
                                  ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - 30 * S()));
    if (linkButton(tr("Advanced settings"), true)) setPage(Page::Advanced);
}

// ------------------------------------------------------------------ profile manager

void drawProfileModal(App& app)
{
    if (g_openProfiles) {
        ImGui::OpenPopup("##profiles");
        g_openProfiles = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter()), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(600 * S(), vp->Size.x - 40 * S()), std::min(380 * S(), vp->Size.y - 40 * S())));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * S(), 16 * S()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * S());
    const bool open = ImGui::BeginPopupModal("##profiles", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    if (!open) return;

    auto& list = app.store().profiles();
    static char nameBuf[64] = "";
    static int selected = -1;
    if (ImGui::IsWindowAppearing()) selected = -1;
    if (selected < 0 || selected >= int(list.size())) {
        selected = app.store().activeIndex();
        if (selected >= 0) strncpy_s(nameBuf, list[size_t(selected)].name.c_str(), _TRUNCATE);
    }

    ImGui::PushFont(fonts().semibold, 20.0f);
    ImGui::TextUnformatted(tr("Profiles"));
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 20 * S() - 34 * S());
    if (iconButton("##close", Icon::Close, tr("Close"), 34 * S()) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::Dummy(ImVec2(0, 2 * S()));

    const float listW = 190 * S();
    ImGui::BeginChild("##plist", ImVec2(listW, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders);
    for (int i = 0; i < int(list.size()); ++i) {
        const bool active = list[size_t(i)].name == app.settings().activeProfile;
        std::string label = (active ? "\xE2\x97\x8F  " : "     ") + list[size_t(i)].name; // filled circle marks the active one
        if (ImGui::Selectable(label.c_str(), selected == i)) {
            selected = i;
            strncpy_s(nameBuf, list[size_t(i)].name.c_str(), _TRUNCATE);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) app.selectProfile(list[size_t(i)].name);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (selected >= 0 && selected < int(list.size())) {
        Profile& p = list[size_t(selected)];
        ImGui::PushFont(fonts().semibold, 17.0f);
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::PushTextWrapPos(0);
        ImGui::Text(tr("Trigger: %s (%s)"), p.trigger.name().c_str(), tr(triggerModeName(p.mode.mode)));
        ImGui::Text(tr("Action: %s, %s"), p.action.chord.name().c_str(), p.maxMode ? "MAX" : (fmtRate(p.cps) + " CPS").c_str());
        if (p.filterEnabled) ImGui::Text(tr("Only while %s is in front"), p.targetProcess.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4 * S()));

        const bool isActive = p.name == app.settings().activeProfile;
        ImGui::BeginDisabled(isActive);
        if (accentButton(isActive ? tr("Active") : tr("Activate"), ImVec2(-FLT_MIN, 36 * S()), col::violet)) app.selectProfile(p.name);
        ImGui::EndDisabled();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 96 * S() - 8 * S());
        ImGui::InputText("##rename", nameBuf, sizeof nameBuf);
        ImGui::SameLine(0, 8 * S());
        if (ImGui::Button(tr("Rename"), ImVec2(96 * S(), 0)) && nameBuf[0]) {
            std::string nn = trim(nameBuf);
            if (!nn.empty() && nn != p.name) {
                nn = app.store().uniqueName(nn);
                if (app.settings().activeProfile == p.name) app.settings().activeProfile = nn;
                p.name = nn;
                app.profileChanged();
            }
        }
        const float half = (ImGui::GetContentRegionAvail().x - 8 * S()) * 0.5f;
        if (ImGui::Button(tr("Duplicate"), ImVec2(half, 0))) {
            Profile c = p;
            c.name = app.store().uniqueName(p.name + tr(" copy"));
            list.push_back(c);
            selected = int(list.size()) - 1;
            strncpy_s(nameBuf, c.name.c_str(), _TRUNCATE);
            app.profileChanged();
        }
        ImGui::SameLine(0, 8 * S());
        ImGui::BeginDisabled(list.size() <= 1);
        if (ImGui::Button(tr("Delete"), ImVec2(half, 0))) ImGui::OpenPopup("confirmdel");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("confirmdel")) {
            ImGui::Text(tr("Delete profile '%s'?"), p.name.c_str());
            if (ImGui::Button(tr("Delete"))) {
                const bool wasActive = app.settings().activeProfile == p.name;
                list.erase(list.begin() + selected);
                selected = -1;
                if (wasActive) app.settings().activeProfile = list.front().name;
                app.profileChanged();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    const float half = (ImGui::GetContentRegionAvail().x - 8 * S()) * 0.5f;
    if (ImGui::Button(tr("New profile"), ImVec2(half, 0))) {
        Profile n;
        n.name = app.store().uniqueName(tr("New profile"));
        list.push_back(n);
        selected = int(list.size()) - 1;
        strncpy_s(nameBuf, n.name.c_str(), _TRUNCATE);
        app.profileChanged();
    }
    ImGui::SameLine(0, 8 * S());
    if (ImGui::Button(tr("Reset to defaults"), ImVec2(half, 0))) ImGui::OpenPopup("confirmreset");
    if (ImGui::BeginPopup("confirmreset")) {
        ImGui::TextUnformatted(tr("Replace all profiles with the built-in set?"));
        if (ImGui::Button(tr("Replace"))) {
            list = ProfileStore::defaults();
            app.settings().activeProfile = list.front().name;
            selected = -1;
            app.profileChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    ImGui::EndPopup();
}

} // namespace

void setPage(Page p, int subTab)
{
    g_page = p;
    g_sub = subTab;
}

Page currentPage() { return g_page; }
int currentSubTab() { return g_sub; }
void setSubTab(int t) { g_sub = t; }
void openProfileManager() { g_openProfiles = true; }

void drawRoot(App& app)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * S(), 16 * S()));
    ImGui::Begin("Infinity Clicker", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    switch (g_page) {
    case Page::Main: drawMain(app); break;
    case Page::Advanced: drawAdvanced(app); break;
    case Page::Settings: drawSettings(app); break;
    }
    drawProfileModal(app);
    drawLogModal(app);
    ImGui::End();
}

} // namespace infclick::ui
