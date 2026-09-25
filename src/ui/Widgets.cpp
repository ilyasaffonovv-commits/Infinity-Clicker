#include "ui/Ui.h"

#include "core/I18n.h"
#include "core/Str.h"

#include <imgui_internal.h>

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>

namespace infclick::ui {

// ------------------------------------------------------------------ formatting

std::string fmtRate(double cps)
{
    if (cps >= 10000) return strFormat("%.0f", cps);
    if (cps >= 100) return strFormat("%.1f", cps);
    return strFormat("%.2f", cps);
}

std::string fmtUs(double us)
{
    if (us >= 1e6) return strFormat(tr("%.3f s"), us / 1e6);
    if (us >= 1000) return strFormat(tr("%.3f ms"), us / 1000);
    return strFormat(tr("%.1f us"), us);
}

std::string fmtInterval(double cps)
{
    if (cps <= 0) return "-";
    const double us = 1e6 / cps;
    if (us >= 1e6) return strFormat(tr("%.3g s"), us / 1e6);
    if (us >= 1000) return strFormat(tr("%.3g ms"), us / 1000);
    return strFormat(tr("%.3g us"), us);
}

std::string u64str(uint64_t v) { return strFormat("%llu", (unsigned long long)v); }

void openPath(const std::wstring& p) { ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

// ------------------------------------------------------------------ containers

bool beginCard(const char* id, ImVec2 size)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * S(), 14 * S()));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12 * S());
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0) flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void endCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void fieldLabel(const char* text)
{
    ImGui::PushFont(fonts().semibold, 11.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4 * S());
}

void sectionLabel(const char* text) { fieldLabel(text); }

// ------------------------------------------------------------------ simple controls

int segmentedClick(const char* id, const char* const* labels, int count, int selected, float width)
{
    ImGui::PushID(id);
    const float avail = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const float gap = 2.0f * S();
    const float w = (avail - gap * float(count - 1)) / float(count);
    const float h = 34.0f * S();
    int clicked = -1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(start, ImVec2(start.x + avail, start.y + h), col::panel2, 8 * S());
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, gap);
        const bool sel = selected == i;
        ImGui::PushID(i);
        ImGui::InvisibleButton("##seg", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) clicked = i;
        ImGui::PopID();
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        if (sel) {
            dl->AddRectFilled(ImVec2(mn.x + 2, mn.y + 2), ImVec2(mx.x - 2, mx.y - 2), alpha(col::violet, 0.85f), 7 * S());
        } else if (hov) {
            dl->AddRectFilled(ImVec2(mn.x + 2, mn.y + 2), ImVec2(mx.x - 2, mx.y - 2), col::panelHover, 7 * S());
        }
        // Long labels (Russian) shrink instead of spilling into the neighbouring segment.
        float fs = 13.5f;
        ImGui::PushFont(fonts().semibold, fs);
        ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        if (ts.x > w - 12 * S()) {
            ImGui::PopFont();
            fs = std::max(10.5f, fs * (w - 12 * S()) / ts.x);
            ImGui::PushFont(fonts().semibold, fs);
            ts = ImGui::CalcTextSize(labels[i]);
        }
        dl->AddText(ImVec2(mn.x + (w - ts.x) * 0.5f, mn.y + (h - ts.y) * 0.5f),
                    sel ? IM_COL32(255, 255, 255, 255) : (hov ? col::text : col::dim), labels[i]);
        ImGui::PopFont();
    }
    ImGui::PopID();
    return clicked;
}

bool segmented(const char* id, const char* const* labels, int count, int* value, float width)
{
    const int c = segmentedClick(id, labels, count, *value, width);
    if (c >= 0 && c != *value) {
        *value = c;
        return true;
    }
    return false;
}

bool bindField(const char* id, const KeyChord& chord, bool capturing, double secsLeft, float width)
{
    ImGui::PushID(id);
    const float h = 36.0f * S();
    const bool clicked = ImGui::InvisibleButton("##bind", ImVec2(width, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    const float r = 9 * S();
    if (capturing) {
        const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 6.0f);
        dl->AddRectFilled(p0, p1, alpha(col::violet, 0.16f + 0.10f * pulse), r);
        dl->AddRect(p0, p1, alpha(col::violetSoft, 0.7f + 0.3f * pulse), r, 1.5f * S());
    } else {
        dl->AddRectFilled(p0, p1, hov ? col::panelHover : col::panel2, r);
        dl->AddRect(p0, p1, hov ? alpha(col::violetSoft, 0.75f) : col::border, r, 1.0f * S());
    }
    ImGui::PushFont(fonts().semibold, 15.0f);
    const std::string label = capturing ? std::string(tr("Listening...")) : chord.name();
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    dl->AddText(ImVec2(p0.x + 14 * S(), p0.y + (h - ts.y) * 0.5f), capturing ? col::violetSoft : (chord.valid() ? col::text : col::dim),
                label.c_str());
    ImGui::PopFont();
    if (capturing) {
        const std::string t = strFormat(tr("%.0f s"), std::ceil(secsLeft));
        ImGui::PushFont(fonts().body, 13.0f);
        const ImVec2 tz = ImGui::CalcTextSize(t.c_str());
        dl->AddText(ImVec2(p1.x - 14 * S() - tz.x, p0.y + (h - tz.y) * 0.5f), col::dim, t.c_str());
        ImGui::PopFont();
    }
    if (hov && !capturing)
        ImGui::SetTooltip("%s", tr("Click, then press the key or mouse button you want to use.\nLeft clicks on this window "
                                   "are ignored while listening; click elsewhere to bind Left Mouse."));
    ImGui::PopID();
    return clicked;
}

bool iconButton(const char* id, Icon icon, const char* tooltip, float size)
{
    if (size <= 0) size = 34.0f * S();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##ib", ImVec2(size, size));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    if (hov || act) dl->AddRectFilled(p0, p1, act ? alpha(col::violet, 0.35f) : col::panelHover, 8 * S());
    ImGui::PushFont(fonts().body, 17.0f);
    const ImVec2 ts = ImGui::CalcTextSize(ic(icon));
    dl->AddText(ImVec2(p0.x + (size - ts.x) * 0.5f, p0.y + (size - ts.y) * 0.5f), hov ? col::text : col::dim, ic(icon));
    ImGui::PopFont();
    if (hov && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

bool keyPicker(const char* id, KeyChord& chord, bool allowMouse, bool allowWheel)
{
    bool changed = false;
    ImGui::PushID(id);
    const float sz = 36.0f * S();
    ImGui::InvisibleButton("##pick", ImVec2(sz, sz));
    const bool hov = ImGui::IsItemHovered();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        dl->AddRectFilled(p0, p1, hov ? col::panelHover : col::panel2, 9 * S());
        dl->AddRect(p0, p1, hov ? alpha(col::violetSoft, 0.75f) : col::border, 9 * S(), 1.0f * S());
        ImGui::PushFont(fonts().body, 15.0f);
        const ImVec2 ts = ImGui::CalcTextSize(ic(Icon::ChevronDown));
        dl->AddText(ImVec2(p0.x + (sz - ts.x) * 0.5f, p0.y + (sz - ts.y) * 0.5f), col::dim, ic(Icon::ChevronDown));
        ImGui::PopFont();
    }
    if (ImGui::IsItemClicked()) ImGui::OpenPopup("picker");
    if (hov) ImGui::SetTooltip("%s", tr("Pick from the full key list"));
    ImGui::SetNextWindowSizeConstraints(ImVec2(260 * S(), 0), ImVec2(FLT_MAX, 420 * S()));
    if (ImGui::BeginPopup("picker")) {
        bool ctrl = chord.mods & ModCtrl, shift = chord.mods & ModShift, alt = chord.mods & ModAlt, win = chord.mods & ModWin;
        ImGui::TextDisabled("%s", tr("Modifiers"));
        ImGui::SameLine();
        bool mc = ImGui::Checkbox("Ctrl", &ctrl);
        ImGui::SameLine();
        mc |= ImGui::Checkbox("Shift", &shift);
        ImGui::SameLine();
        mc |= ImGui::Checkbox("Alt", &alt);
        ImGui::SameLine();
        mc |= ImGui::Checkbox("Win", &win);
        if (mc) {
            chord.mods = uint8_t((ctrl ? ModCtrl : 0) | (shift ? ModShift : 0) | (alt ? ModAlt : 0) | (win ? ModWin : 0));
            changed = true;
        }
        ImGui::Separator();
        const uint8_t mods = chord.mods;
        if (allowMouse && ImGui::BeginMenu(tr("Mouse"))) {
            for (int b = 0; b < int(MouseButton::Count); ++b) {
                const MouseButton mb = MouseButton(b);
                if (!allowWheel && b >= int(MouseButton::WheelUp)) break;
                if (ImGui::MenuItem(mouseButtonName(mb), nullptr, chord.isMouse() && chord.code == b)) {
                    chord = KeyChord::mouse(mb, mods);
                    changed = true;
                }
            }
            ImGui::EndMenu();
        }
        const char* group = nullptr;
        bool open = false;
        for (const KeyListEntry& k : keyList()) {
            if (!group || std::strcmp(group, k.group) != 0) {
                if (open) ImGui::EndMenu();
                group = k.group;
                open = ImGui::BeginMenu(tr(group));
            }
            if (open) {
                const bool sel = chord.isKeyboard() && chord.code == k.vk && chord.extended == k.extended;
                if (ImGui::MenuItem(tr(k.name), nullptr, sel)) {
                    KeyChord c = KeyChord::key(k.vk, mods);
                    c.extended = k.extended;
                    chord = c;
                    changed = true;
                }
            }
        }
        if (open) ImGui::EndMenu();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool chip(const char* label, bool active)
{
    ImGui::PushID(label);
    ImGui::PushFont(fonts().semibold, 13.0f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 size(ts.x + 22 * S(), 28 * S());
    const bool clicked = ImGui::InvisibleButton("##chip", size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    dl->AddRectFilled(p0, p1, active ? alpha(col::violet, 0.30f) : (hov ? col::panelHover : col::panel2), size.y * 0.5f);
    dl->AddRect(p0, p1, active ? alpha(col::violetSoft, 0.85f) : (hov ? alpha(col::violetSoft, 0.4f) : col::border), size.y * 0.5f, 1.0f * S());
    dl->AddText(ImVec2(p0.x + (size.x - ts.x) * 0.5f, p0.y + (size.y - ts.y) * 0.5f), active ? col::text : (hov ? col::text : col::dim),
                label);
    ImGui::PopFont();
    ImGui::PopID();
    return clicked;
}

bool bigButton(const char* title, const char* subtitle, BigButton style, float height)
{
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(title);
    const bool clicked = ImGui::InvisibleButton("##big", ImVec2(w, height));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    const float r = 12 * S();
    ImU32 fill, edge, titleCol = IM_COL32(255, 255, 255, 255);
    switch (style) {
    case BigButton::Arm:
        fill = act ? IM_COL32(109, 70, 214, 255) : hov ? IM_COL32(155, 110, 250, 255) : col::violet;
        edge = alpha(IM_COL32(255, 255, 255, 255), 0.18f);
        break;
    case BigButton::Armed:
        fill = hov ? IM_COL32(43, 34, 92, 255) : IM_COL32(33, 26, 74, 255);
        edge = alpha(col::violetSoft, hov ? 0.95f : 0.65f);
        break;
    case BigButton::Stop:
        fill = hov ? IM_COL32(88, 34, 50, 255) : IM_COL32(68, 27, 40, 255);
        edge = alpha(col::red, 0.55f);
        break;
    default: // Paused
        fill = hov ? IM_COL32(88, 68, 22, 255) : IM_COL32(70, 54, 18, 255);
        edge = alpha(col::amber, 0.75f);
        break;
    }
    if (style == BigButton::Armed) // soft outer glow
        for (int i = 3; i >= 1; --i)
            dl->AddRect(ImVec2(p0.x - i * S(), p0.y - i * S()), ImVec2(p1.x + i * S(), p1.y + i * S()), alpha(col::violet, 0.07f * float(4 - i)),
                        r + i * S(), 1.0f * S());
    dl->AddRectFilled(p0, p1, fill, r);
    if (style == BigButton::Arm) // faint sheen on the upper half
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + height * 0.5f), alpha(IM_COL32(255, 255, 255, 255), 0.07f), r, ImDrawFlags_RoundCornersTop);
    dl->AddRect(p0, p1, edge, r, 1.5f * S());

    ImGui::PushFont(fonts().semibold, 17.0f);
    const ImVec2 tt = ImGui::CalcTextSize(title);
    ImGui::PopFont();
    const bool sub = subtitle && *subtitle;
    ImGui::PushFont(fonts().body, 12.5f);
    const ImVec2 st = sub ? ImGui::CalcTextSize(subtitle) : ImVec2(0, 0);
    ImGui::PopFont();
    const float total = tt.y + (sub ? st.y + 1 * S() : 0);
    float y = p0.y + (height - total) * 0.5f;
    ImGui::PushFont(fonts().semibold, 17.0f);
    if (style == BigButton::Armed) { // status dot before the title
        const float dr = 4.5f * S();
        const float pulse = 0.6f + 0.4f * std::sin(float(ImGui::GetTime()) * 3.0f);
        dl->AddCircleFilled(ImVec2(p0.x + (w - tt.x) * 0.5f - 14 * S(), y + tt.y * 0.5f), dr, alpha(col::green, pulse));
    }
    dl->AddText(ImVec2(p0.x + (w - tt.x) * 0.5f, y), titleCol, title);
    ImGui::PopFont();
    if (sub) {
        ImGui::PushFont(fonts().body, 12.5f);
        dl->AddText(ImVec2(p0.x + (w - st.x) * 0.5f, y + tt.y + 1 * S()), alpha(IM_COL32(255, 255, 255, 255), 0.66f), subtitle);
        ImGui::PopFont();
    }
    ImGui::PopID();
    return clicked;
}

bool linkButton(const char* label, bool chevron)
{
    ImGui::PushID(label);
    ImGui::PushFont(fonts().semibold, 13.5f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    ImGui::PushFont(fonts().body, 13.5f);
    const float cw = chevron ? ImGui::CalcTextSize(ic(Icon::ChevronRight)).x + 4 * S() : 0;
    ImGui::PopFont();
    const bool clicked = ImGui::InvisibleButton("##link", ImVec2(ts.x + cw + 4 * S(), ts.y + 8 * S()));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImU32 c = hov ? col::violetSoft : col::dim;
    dl->AddText(ImVec2(p0.x, p0.y + 4 * S()), c, label);
    if (hov) dl->AddLine(ImVec2(p0.x, p0.y + 4 * S() + ts.y), ImVec2(p0.x + ts.x, p0.y + 4 * S() + ts.y), alpha(c, 0.6f), 1.0f);
    ImGui::PopFont();
    if (chevron) {
        ImGui::PushFont(fonts().body, 13.5f);
        dl->AddText(ImVec2(p0.x + ts.x + 4 * S(), p0.y + 4 * S()), c, ic(Icon::ChevronRight));
        ImGui::PopFont();
    }
    ImGui::PopID();
    return clicked;
}

bool toggleSwitch(const char* id, bool* v)
{
    ImGui::PushID(id);
    const ImVec2 size(40 * S(), 22 * S());
    const bool clicked = ImGui::InvisibleButton("##sw", size);
    if (clicked) *v = !*v;
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->AddRectFilled(p0, p1, *v ? (hov ? col::violetSoft : col::violet) : (hov ? IM_COL32(58, 68, 100, 255) : IM_COL32(46, 55, 84, 255)),
                      size.y * 0.5f);
    const float kr = size.y * 0.5f - 3.5f * S();
    const float cx = *v ? p1.x - size.y * 0.5f : p0.x + size.y * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, p0.y + size.y * 0.5f), kr, IM_COL32(255, 255, 255, 255));
    ImGui::PopID();
    return clicked;
}

static const char* statusInfo(EngineState s, ImU32& c)
{
    c = IM_COL32(148, 156, 180, 255);
    switch (s) {
    case EngineState::Armed: c = col::cyan; return tr("READY");
    case EngineState::Running: c = col::green; return tr("ACTIVE");
    case EngineState::Paused: c = col::amber; return tr("PAUSED");
    default: return tr("STOPPED");
    }
}

float statusIndicatorWidth(EngineState s)
{
    ImU32 c;
    const char* text = statusInfo(s, c);
    ImGui::PushFont(fonts().semibold, 14.0f);
    const float w = 4.5f * S() * 2 + 8 * S() + ImGui::CalcTextSize(text).x;
    ImGui::PopFont();
    return w;
}

float statusIndicator(EngineState s)
{
    ImU32 c;
    const char* text = statusInfo(s, c);
    ImGui::PushFont(fonts().semibold, 14.0f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float dr = 4.5f * S();
    const float total = dr * 2 + 8 * S() + ts.x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float a = 1.0f;
    if (s == EngineState::Running) a = 0.55f + 0.45f * std::sin(float(ImGui::GetTime()) * 7.0f);
    dl->AddCircleFilled(ImVec2(p.x + dr, p.y + ts.y * 0.5f), dr + 3 * S(), alpha(c, 0.16f * a));
    dl->AddCircleFilled(ImVec2(p.x + dr, p.y + ts.y * 0.5f), dr, alpha(c, a));
    dl->AddText(ImVec2(p.x + dr * 2 + 8 * S(), p.y), c, text);
    ImGui::Dummy(ImVec2(total, ts.y));
    ImGui::PopFont();
    return total;
}

void sparkline(const float* values, int count, float target, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(9, 12, 20, 255), 8.0f * S());
    if (count < 2) {
        dl->AddText(ImVec2(p.x + 10 * S(), p.y + size.y * 0.5f - 8 * S()), col::dim, tr("CPS history (10 s)"));
        return;
    }
    float mx = target > 0 ? target * 1.25f : 1.0f;
    for (int i = 0; i < count; ++i) mx = std::max(mx, values[i] * 1.1f);
    const float pad = 6.0f * S();
    const float span = size.x - 2 * pad;
    auto pt = [&](int i) {
        return ImVec2(p.x + pad + span * float(kCpsHistory - count + i) / float(kCpsHistory - 1),
                      p.y + size.y - pad - (size.y - 2 * pad) * (values[i] / mx));
    };
    if (target > 0) {
        const float ty = p.y + size.y - pad - (size.y - 2 * pad) * (target / mx);
        dl->AddLine(ImVec2(p.x + pad, ty), ImVec2(p.x + size.x - pad, ty), alpha(col::violet, 0.5f), 1.0f);
    }
    for (int i = 1; i < count; ++i) {
        const ImVec2 a = pt(i - 1), b = pt(i);
        dl->AddQuadFilled(a, b, ImVec2(b.x, p.y + size.y - pad), ImVec2(a.x, p.y + size.y - pad), alpha(col::cyan, 0.11f));
        dl->AddLine(a, b, col::cyan, 1.6f * S());
    }
}

void help(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool accentButton(const char* label, ImVec2 size, ImU32 color)
{
    ImVec4 c = v4(color);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x * 0.55f, c.y * 0.55f, c.z * 0.55f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x * 0.70f, c.y * 0.70f, c.z * 0.70f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
    ImGui::PushStyleColor(ImGuiCol_Border, c);
    ImGui::PushFont(fonts().semibold, 15.0f);
    const bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return r;
}

void metricRow(const char* name, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const std::string v = strFormatV(fmt, ap);
    va_end(ap);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    ImGui::PushFont(fonts().mono, 0.0f);
    ImGui::TextUnformatted(v.c_str());
    ImGui::PopFont();
}

bool pageHeader(const char* title)
{
    bool back = iconButton("##back", Icon::Back, tr("Back"), 34 * S());
    ImGui::SameLine(0, 6 * S());
    ImGui::PushFont(fonts().semibold, 21.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        back = true;
    return back;
}

// ------------------------------------------------------------------ setting rows

namespace {
struct RowState {
    ImVec2 origin{};
    float height = 0;
    float width = 0;
} g_row;
} // namespace

void rowBegin(const char* title, const char* desc, float controlW, float controlH)
{
    if (controlH <= 0) controlH = ImGui::GetFrameHeight();
    g_row.width = ImGui::GetContentRegionAvail().x;
    g_row.origin = ImGui::GetCursorPos();
    const float textW = std::max(60.0f * S(), g_row.width - controlW - 20 * S());
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(g_row.origin.x + textW);
    ImGui::TextUnformatted(title);
    if (desc && *desc) {
        ImGui::PushFont(fonts().body, 12.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::TextUnformatted(desc);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    g_row.height = std::max(ImGui::GetItemRectSize().y, controlH);
    ImGui::SetCursorPos(ImVec2(g_row.origin.x + g_row.width - controlW, g_row.origin.y + (g_row.height - controlH) * 0.5f));
}

void rowEnd(bool separator)
{
    ImGui::SetCursorPos(ImVec2(g_row.origin.x, g_row.origin.y + g_row.height));
    if (separator) {
        ImGui::Dummy(ImVec2(0, 6 * S()));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + g_row.width, p.y), alpha(col::border, 0.7f), 1.0f);
        ImGui::Dummy(ImVec2(0, 6 * S()));
    } else {
        ImGui::Dummy(ImVec2(0, 6 * S()));
    }
}

void stackedBegin(const char* title, const char* desc)
{
    ImGui::TextUnformatted(title);
    if (desc && *desc) {
        ImGui::PushFont(fonts().body, 12.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, 2 * S()));
}

void stackedEnd(bool separator)
{
    ImGui::Dummy(ImVec2(0, 4 * S()));
    if (separator) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), alpha(col::border, 0.7f), 1.0f);
        ImGui::Dummy(ImVec2(0, 6 * S()));
    }
}

bool toggleRow(const char* title, const char* desc, bool* v)
{
    rowBegin(title, desc, 40 * S(), 22 * S());
    const bool changed = toggleSwitch(title, v);
    rowEnd();
    return changed;
}

} // namespace infclick::ui
