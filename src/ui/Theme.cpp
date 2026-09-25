#include "ui/Ui.h"

#include <windows.h>

namespace infclick::ui {

namespace {
bool g_icons = false;
}

Fonts& fonts()
{
    static Fonts f;
    return f;
}

ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 alpha(ImU32 c, float a)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

const char* ic(Icon i)
{
    // Codepoints are identical in Segoe Fluent Icons (Win11) and Segoe MDL2 Assets (Win10).
    struct E {
        const char* glyph;
        const char* fallback;
    };
    static const E t[] = {
        {"", "*"},  // Settings
        {"", "v"},  // ChevronDown
        {"", ">"},  // ChevronRight
        {"", "<"},  // Back
        {"", "~"},  // Edit
        {"", "+"},  // Add
        {"", "x"},  // Close
        {"", "[]"}, // Folder
        {"", "x"},  // Delete
        {"", "="},  // Copy
        {"", "i"},  // Log (page)
    };
    const E& e = t[int(i)];
    return g_icons ? e.glyph : e.fallback;
}

void loadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    auto path = [&](const char* file) {
        char buf[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, winDir, -1, buf, sizeof buf, nullptr, nullptr);
        return std::string(buf) + "\\Fonts\\" + file;
    };
    auto exists = [](const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; };

    std::string iconFile;
    for (const char* f : {"SegoeIcons.ttf", "segmdl2.ttf"}) {
        if (exists(path(f))) {
            iconFile = path(f);
            break;
        }
    }
    g_icons = !iconFile.empty();

    auto load = [&](const char* file, float size, bool withIcons) -> ImFont* {
        const std::string p = path(file);
        if (!exists(p)) return nullptr;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), size, &cfg);
        if (f && withIcons && g_icons) {
            ImFontConfig m;
            m.MergeMode = true;
            m.GlyphOffset = ImVec2(0, 1);
            m.GlyphMinAdvanceX = size;
            io.Fonts->AddFontFromFileTTF(iconFile.c_str(), size, &m);
        }
        return f;
    };
    // Glyphs are rasterized on demand (ImGui 1.92 dynamic atlas), so Cyrillic just works.
    Fonts& f = fonts();
    f.body = load("segoeui.ttf", 15.0f, true);
    if (!f.body) f.body = io.Fonts->AddFontDefault();
    f.semibold = load("seguisb.ttf", 15.0f, true);
    if (!f.semibold) f.semibold = f.body;
    f.mono = load("consola.ttf", 14.0f, false);
    if (!f.mono) f.mono = f.body;
    f.display = load("bahnschrift.ttf", 40.0f, false);
    if (!f.display) f.display = f.semibold;
    io.FontDefault = f.body;
}

void applyTheme(float dpi)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(20, 16);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.CellPadding = ImVec2(10, 5);
    s.ScrollbarSize = 8;
    s.GrabMinSize = 14;
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 8;
    s.PopupRounding = 10;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 8;
    s.TabRounding = 8;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 1;
    s.PopupBorderSize = 1;
    s.SeparatorTextBorderSize = 1;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = v4(col::text);
    c[ImGuiCol_TextDisabled] = v4(col::dim);
    c[ImGuiCol_WindowBg] = v4(col::bg);
    c[ImGuiCol_ChildBg] = v4(col::panel);
    c[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.09f, 0.15f, 0.99f);
    c[ImGuiCol_Border] = v4(col::border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = v4(col::panel2);
    c[ImGuiCol_FrameBgHovered] = v4(col::panelHover);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.20f, 0.33f, 1);
    c[ImGuiCol_TitleBg] = v4(col::bg);
    c[ImGuiCol_TitleBgActive] = v4(col::bg);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.27f, 0.40f, 1);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.36f, 0.52f, 1);
    c[ImGuiCol_ScrollbarGrabActive] = v4(col::violet);
    c[ImGuiCol_CheckMark] = v4(col::violetSoft);
    c[ImGuiCol_SliderGrab] = v4(col::violetSoft);
    c[ImGuiCol_SliderGrabActive] = v4(col::violet);
    c[ImGuiCol_Button] = v4(col::panel2);
    c[ImGuiCol_ButtonHovered] = v4(col::panelHover);
    c[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.22f, 0.58f, 1);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.16f, 0.40f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.20f, 0.50f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.24f, 0.62f, 1);
    c[ImGuiCol_Separator] = v4(col::border);
    c[ImGuiCol_PlotLines] = v4(col::cyan);
    c[ImGuiCol_PlotHistogram] = v4(col::violet);
    c[ImGuiCol_TableHeaderBg] = v4(col::panel2);
    c[ImGuiCol_TableBorderStrong] = v4(col::border);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.18f, 0.27f, 1);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.36f, 0.96f, 0.35f);
    c[ImGuiCol_NavCursor] = v4(col::violetSoft);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

    s.ScaleAllSizes(dpi);
    s.FontScaleDpi = dpi;
}

} // namespace infclick::ui
