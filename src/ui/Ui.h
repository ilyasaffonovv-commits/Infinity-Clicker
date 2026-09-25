#pragma once
// Dear ImGui (DX11) front-end. Rendering is event-driven: the UI thread sleeps
// in MsgWaitForMultipleObjects and only draws on input, engine events or a slow
// refresh tick, so an idle, visible window costs ~0% CPU.
//
//   Main screen      Views.cpp         trigger / action / mode / speed / ARM
//   Advanced         AdvancedView.cpp  Engine | Diagnostics | Input Lab
//   Settings         SettingsView.cpp  General | Hotkeys | Behavior | Appearance | Diagnostics
//   Widgets, theme   Widgets.cpp, Theme.cpp
#include "app/App.h"

#include <imgui.h>

#include <functional>
#include <string>

namespace infclick::ui {

int run(App& app, bool startHidden, std::function<void(HWND)> onReady = {});

// ---- navigation
enum class Page : int { Main = 0, Advanced, Settings };
enum class AdvTab : int { Engine = 0, Diagnostics, Lab };
void setPage(Page p, int subTab = 0);
Page currentPage();
int currentSubTab();
void setSubTab(int tab);
void openProfileManager();

// ---- theme / fonts (Theme.cpp)
struct Fonts {
    ImFont* body = nullptr;
    ImFont* semibold = nullptr;
    ImFont* mono = nullptr;
    ImFont* display = nullptr;
};
Fonts& fonts();
void loadFonts();
void applyTheme(float dpiScale);
inline float S() { return ImGui::GetStyle().FontScaleDpi; } // device pixels per logical pixel

namespace col {
constexpr ImU32 bg = IM_COL32(10, 12, 22, 255);
constexpr ImU32 panel = IM_COL32(19, 24, 39, 255);
constexpr ImU32 panel2 = IM_COL32(27, 33, 53, 255);
constexpr ImU32 panelHover = IM_COL32(35, 43, 69, 255);
constexpr ImU32 border = IM_COL32(40, 48, 74, 255);
constexpr ImU32 text = IM_COL32(232, 236, 247, 255);
constexpr ImU32 dim = IM_COL32(140, 149, 176, 255);
constexpr ImU32 violet = IM_COL32(139, 92, 246, 255);
constexpr ImU32 violetSoft = IM_COL32(167, 139, 250, 255);
constexpr ImU32 cyan = IM_COL32(34, 211, 238, 255);
constexpr ImU32 green = IM_COL32(52, 211, 153, 255);
constexpr ImU32 amber = IM_COL32(251, 191, 36, 255);
constexpr ImU32 red = IM_COL32(248, 113, 113, 255);
} // namespace col
ImVec4 v4(ImU32 c);
ImU32 alpha(ImU32 c, float a); // same colour, alpha 0..1

// Segoe Fluent Icons / Segoe MDL2 Assets glyphs (merged into every UI font). Text fallback if the font is missing.
enum class Icon { Settings, ChevronDown, ChevronRight, Back, Edit, Add, Close, Folder, Delete, Copy, Log };
const char* ic(Icon i);

// ---- widgets (Widgets.cpp)
bool beginCard(const char* id, ImVec2 size);   // size.x == 0: full width; size.y == 0: auto height
void endCard();
void fieldLabel(const char* text);             // small caps caption above a control
void sectionLabel(const char* text);
bool segmented(const char* id, const char* const* labels, int count, int* value, float width = 0.0f);
int segmentedClick(const char* id, const char* const* labels, int count, int selected, float width = 0.0f); // clicked index or -1
bool bindField(const char* id, const KeyChord& chord, bool capturing, double secsLeft, float width);
bool keyPicker(const char* id, KeyChord& chord, bool allowMouse, bool allowWheel);
bool chip(const char* label, bool active);
enum class BigButton { Arm, Armed, Stop, Paused };
bool bigButton(const char* title, const char* subtitle, BigButton style, float height);
bool linkButton(const char* label, bool chevron);
bool iconButton(const char* id, Icon icon, const char* tooltip, float size = 0.0f);
bool toggleSwitch(const char* id, bool* v);
float statusIndicator(EngineState s);          // dot + "READY"; returns the drawn width
float statusIndicatorWidth(EngineState s);
void sparkline(const float* values, int count, float target, ImVec2 size);
void help(const char* text);
bool accentButton(const char* label, ImVec2 size, ImU32 color);
void metricRow(const char* name, const char* fmt, ...);
bool pageHeader(const char* title);            // "< title"; returns true when Back was pressed
// Settings-style rows: title (+ description) on the left, control right-aligned on the same row.
void rowBegin(const char* title, const char* desc, float controlW, float controlH = 0.0f);
void rowEnd(bool separator = true);
bool toggleRow(const char* title, const char* desc, bool* v);
// Same, but title and description use the full width and the control goes on the next line (long descriptions).
void stackedBegin(const char* title, const char* desc);
void stackedEnd(bool separator = true);

std::string fmtRate(double cps);
std::string fmtUs(double us);
std::string fmtInterval(double cps); // "50 ms", "800 us"
std::string u64str(uint64_t v);
void openPath(const std::wstring& path); // ShellExecute "open"

// ---- views
void drawRoot(App& app);
void drawAdvanced(App& app);   // AdvancedView.cpp
void drawLabPage(App& app);    // AdvancedView.cpp
void drawSettings(App& app);   // SettingsView.cpp
void drawLogModal(App& app);   // SettingsView.cpp (popup "Log")
void openLog();

// ---- tray (Tray.cpp)
class Tray {
public:
    bool add(HWND owner, HICON normal, HICON active);
    void remove();
    void update(EngineState s, const std::string& tip);
    void showMenu(App& app, HWND owner);
    void readd();
    bool present() const { return added_; }

private:
    HWND owner_ = nullptr;
    HICON normal_ = nullptr, active_ = nullptr;
    bool added_ = false;
    bool activeShown_ = false;
    std::wstring tip_;
};

} // namespace infclick::ui
