/**
 * DLCalcWin32.cpp
 * Native Windows C++17 (Win32 + GDI+) Rewrite of DL Calc
 *
 * Compile command (MinGW-w64 static standalone .exe):
 *   x86_64-w64-mingw32-windres cpp/resource.rc -O coff -o /tmp/resource.res
 *   x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -mwindows -municode -static -static-libgcc -static-libstdc++ \
 *       cpp/DLCalcWin32.cpp /tmp/resource.res -o DLCalc.exe -lgdiplus -ldwmapi -lcomctl32 -lgdi32 -luser32 -lshell32
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include "DLCalcCore.hpp"

#define IDI_APP_ICON 101

using namespace dlcalc;

// ============================================================================
// UTF-8 <-> UTF-16 Helpers
// ============================================================================
static std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &res[0], sz);
    return res;
}

static std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return "";
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &res[0], sz, nullptr, nullptr);
    return res;
}

// ============================================================================
// Application State & %APPDATA%\DLCalc\settings.ini Persistence Engine
// ============================================================================
struct AppState {
    std::string fileSize = "";
    FileSizeUnit fileSizeUnit = FileSizeUnit::GB;
    std::string speed = "";
    SpeedUnit speedUnit = SpeedUnit::MBPS;
    std::string days = "";
    std::string hours = "";
    std::string minutes = "";
    std::string seconds = "";
    std::string timeSeconds = "";
    CalcMode calcMode = CalcMode::NONE;
    bool isDarkMode = true;
};

class PersistenceManager {
public:
    static std::wstring getAppDataIniPath(bool createDir = false) {
        wchar_t appData[MAX_PATH] = {0};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            std::wstring dir = std::wstring(appData) + L"\\DLCalc";
            if (createDir) {
                CreateDirectoryW(dir.c_str(), nullptr);
            }
            return dir + L"\\settings.ini";
        }
        return L"";
    }

    static std::string serializeIni(const AppState& st) {
        std::ostringstream oss;
        oss << "[DLCalc]\r\n";
        oss << "fileSize=" << st.fileSize << "\r\n";
        oss << "fileSizeUnit=" << fileSizeUnitToLabel(st.fileSizeUnit) << "\r\n";
        oss << "speed=" << st.speed << "\r\n";
        oss << "speedUnit=" << speedUnitToLabel(st.speedUnit) << "\r\n";
        oss << "days=" << st.days << "\r\n";
        oss << "hours=" << st.hours << "\r\n";
        oss << "minutes=" << st.minutes << "\r\n";
        oss << "seconds=" << st.seconds << "\r\n";
        oss << "timeSeconds=" << st.timeSeconds << "\r\n";
        oss << "calcMode=" << calcModeToString(st.calcMode) << "\r\n";
        oss << "isDarkMode=" << (st.isDarkMode ? "1" : "0") << "\r\n";
        return oss.str();
    }

    static bool parseIniContent(const std::string& content, AppState& st) {
        if (content.find("[DLCalc]") == std::string::npos) return false;
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '[' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "fileSize") st.fileSize = val;
            else if (key == "fileSizeUnit") st.fileSizeUnit = fileSizeUnitFromLabel(val);
            else if (key == "speed") st.speed = val;
            else if (key == "speedUnit") st.speedUnit = speedUnitFromLabel(val);
            else if (key == "days") st.days = val;
            else if (key == "hours") st.hours = val;
            else if (key == "minutes") st.minutes = val;
            else if (key == "seconds") st.seconds = val;
            else if (key == "timeSeconds") st.timeSeconds = val;
            else if (key == "calcMode") st.calcMode = calcModeFromString(val);
            else if (key == "isDarkMode") st.isDarkMode = (val != "0" && val != "false");
        }
        return true;
    }

    static void loadState(AppState& st) {
        std::wstring iniPath = getAppDataIniPath(false);
        if (iniPath.empty()) return;

        HANDLE hIni = CreateFileW(
            iniPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hIni != INVALID_HANDLE_VALUE) {
            char buf[4096] = {0};
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(hIni, buf, sizeof(buf) - 1, &bytesRead, nullptr);
            CloseHandle(hIni);
            if (ok && bytesRead > 0) {
                std::string content(buf, bytesRead);
                parseIniContent(content, st);
            }
        }
    }

    static void saveState(const AppState& st) {
        std::wstring iniPath = getAppDataIniPath(true);
        if (iniPath.empty()) return;

        std::string payload = serializeIni(st);
        HANDLE hIni = CreateFileW(
            iniPath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hIni != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hIni, payload.data(), (DWORD)payload.size(), &written, nullptr);
            CloseHandle(hIni);
        }
    }
};

// ============================================================================
// GUI Control IDs & Layout Geometry (1:1 Match with Preview)
// ============================================================================
enum ControlId {
    ID_EDIT_DAYS = 101,
    ID_EDIT_HOURS = 102,
    ID_EDIT_MINUTES = 103,
    ID_EDIT_SECONDS = 104,
    ID_EDIT_TOTAL_SECONDS = 105,
    ID_EDIT_FILESIZE = 106,
    ID_EDIT_SPEED = 107,

    ID_MENU_SIZE_MB = 201,
    ID_MENU_SIZE_GB = 202,
    ID_MENU_SIZE_TB = 203,

    ID_MENU_SPEED_KBS = 211,
    ID_MENU_SPEED_MBS = 212,
    ID_MENU_SPEED_MBPS = 213,
    ID_MENU_SPEED_GBPS = 214,

    ID_TIMER_TOAST = 301
};

struct GlobalApp {
    HWND hwndMain = nullptr;
    HWND hEditDays = nullptr;
    HWND hEditHours = nullptr;
    HWND hEditMinutes = nullptr;
    HWND hEditSeconds = nullptr;
    HWND hEditTotalSeconds = nullptr;
    HWND hEditFileSize = nullptr;
    HWND hEditSpeed = nullptr;

    HFONT hFontRegular = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontSemiBold = nullptr;
    HFONT hFontButton = nullptr;
    HFONT hFontSmall = nullptr;
    HBRUSH hBgBrushDark = nullptr;
    HBRUSH hBgBrushLight = nullptr;

    AppState state;
    DownloadTimeResult calcResult;
    bool isUpdatingControls = false;
    bool isFileSizeFocused = false;
    bool showInfoTooltip = false;
    int openDropdown = 0; // 0 = none, 1 = fileSizeUnit, 2 = speedUnit
    int hoveredDropdownIndex = -1;
    std::wstring toastMessage = L"";

    // Exact 1:1 hit-test rectangles matching the preview layout (400x490 client area)
    RECT rcCopyBreakdown = {336, 26, 368, 58};
    RECT rcCopySeconds   = {336, 194, 368, 226};
    RECT rcSizeDropdown  = {256, 260, 330, 298};
    RECT rcInfoButton    = {336, 263, 368, 295};
    RECT rcSpeedDropdown = {256, 322, 330, 360};
    RECT rcModeButton    = {72, 396, 177, 442};
    RECT rcClearButton   = {193, 396, 328, 442};

    RECT rcSizePopup     = {256, 300, 348, 398}; // 3 items * 30px + 8px padding
    RECT rcSpeedPopup    = {256, 362, 348, 488}; // 4 items * 30px + 6px padding
};

static GlobalApp g_app;

// ============================================================================
// State & Calculation Synchronization
// ============================================================================
static bool hasAnyTimeInput(const AppState& s) {
    return !s.timeSeconds.empty() || !s.days.empty() || !s.hours.empty() || !s.minutes.empty() || !s.seconds.empty();
}

static void reconcileCalcMode(AppState& s) {
    bool hasSize = !s.fileSize.empty();
    bool hasSpeed = !s.speed.empty();
    bool hasTime = hasAnyTimeInput(s);

    if (s.calcMode == CalcMode::TIME && (!hasSize || !hasSpeed)) {
        s.calcMode = CalcMode::NONE;
    } else if (s.calcMode == CalcMode::SPEED && (!hasSize || !hasTime)) {
        s.calcMode = CalcMode::NONE;
    } else if (s.calcMode == CalcMode::SIZE && (!hasSpeed || !hasTime)) {
        s.calcMode = CalcMode::NONE;
    }

    if (s.calcMode == CalcMode::NONE) {
        if (hasSize && hasSpeed) s.calcMode = CalcMode::TIME;
        else if (hasSize && hasTime) s.calcMode = CalcMode::SPEED;
        else if (hasSpeed && hasTime) s.calcMode = CalcMode::SIZE;
    }
}

static std::string stripCommas(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != ',') out.push_back(c);
    }
    return out;
}

static double parseDoubleFast(const std::string& str) {
    if (str.empty()) return 0.0;
    double val = 0.0;
    if (DownloadCalculator::parseDoubleSafe(stripCommas(str), val)) return val;
    return 0.0;
}

static std::string recalculateTotalSeconds(
    const std::string& days,
    const std::string& hours,
    const std::string& minutes,
    const std::string& seconds
) {
    double d = parseDoubleFast(days);
    double h = parseDoubleFast(hours);
    double m = parseDoubleFast(minutes);
    double s = parseDoubleFast(seconds);
    double total = d * 86400.0 + h * 3600.0 + m * 60.0 + s;
    if (total > 0.0) {
        double rounded = std::round(total);
        if (std::abs(total - rounded) < 1e-9) {
            return std::to_string(static_cast<int64_t>(rounded));
        }
        std::ostringstream oss;
        oss << total;
        return oss.str();
    }
    return "";
}

static void applyWindowTheme() {
    BOOL dark = g_app.state.isDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(g_app.hwndMain, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(g_app.hwndMain, 19, &dark, sizeof(dark));
}

static void setEditFormattedText(HWND hEdit, const std::string& rawText, bool appendSuffix = false) {
    std::string display = DownloadCalculator::formatTextWithThousandsCommas(rawText);
    if (appendSuffix && rawText.find('+') != std::string::npos) {
        std::string suffix = DownloadCalculator::formatFileSizeResultSuffix(rawText);
        if (!suffix.empty()) {
            display += suffix;
        }
    }
    std::wstring wDisplay = utf8ToWide(display);

    wchar_t currentBuf[512] = {0};
    GetWindowTextW(hEdit, currentBuf, 511);
    if (wDisplay == currentBuf) return;

    DWORD selStart = 0, selEnd = 0;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    int oldLen = GetWindowTextLengthW(hEdit);
    int distFromRight = oldLen - (int)selEnd;

    bool prevUpdating = g_app.isUpdatingControls;
    g_app.isUpdatingControls = true;
    SetWindowTextW(hEdit, wDisplay.c_str());
    int newLen = (int)wDisplay.length();
    int newPos = std::max(0, newLen - distFromRight);
    SendMessageW(hEdit, EM_SETSEL, newPos, newPos);
    g_app.isUpdatingControls = prevUpdating;
}

static void refreshCalculatedUI() {
    g_app.calcResult = DownloadCalculator::calculate(
        g_app.state.fileSize,
        g_app.state.fileSizeUnit,
        g_app.state.speed,
        g_app.state.speedUnit,
        g_app.state.timeSeconds,
        g_app.state.calcMode
    );

    bool timeEditable = (g_app.state.calcMode != CalcMode::TIME);
    bool sizeEditable = (g_app.state.calcMode != CalcMode::SIZE);
    bool speedEditable = (g_app.state.calcMode != CalcMode::SPEED);

    SendMessageW(g_app.hEditDays, EM_SETREADONLY, !timeEditable, 0);
    SendMessageW(g_app.hEditHours, EM_SETREADONLY, !timeEditable, 0);
    SendMessageW(g_app.hEditMinutes, EM_SETREADONLY, !timeEditable, 0);
    SendMessageW(g_app.hEditSeconds, EM_SETREADONLY, !timeEditable, 0);
    SendMessageW(g_app.hEditTotalSeconds, EM_SETREADONLY, !timeEditable, 0);
    SendMessageW(g_app.hEditFileSize, EM_SETREADONLY, !sizeEditable, 0);
    SendMessageW(g_app.hEditSpeed, EM_SETREADONLY, !speedEditable, 0);

    g_app.isUpdatingControls = true;
    if (g_app.state.calcMode == CalcMode::TIME) {
        SetWindowTextW(g_app.hEditDays, utf8ToWide(g_app.calcResult.formattedDays).c_str());
        SetWindowTextW(g_app.hEditHours, utf8ToWide(g_app.calcResult.formattedHours).c_str());
        SetWindowTextW(g_app.hEditMinutes, utf8ToWide(g_app.calcResult.formattedMinutes).c_str());
        SetWindowTextW(g_app.hEditSeconds, utf8ToWide(g_app.calcResult.formattedSeconds).c_str());
        SetWindowTextW(g_app.hEditTotalSeconds, utf8ToWide(g_app.calcResult.hasResult ? g_app.calcResult.formattedTotalSeconds : "").c_str());
    } else {
        setEditFormattedText(g_app.hEditDays, g_app.state.days);
        setEditFormattedText(g_app.hEditHours, g_app.state.hours);
        setEditFormattedText(g_app.hEditMinutes, g_app.state.minutes);
        setEditFormattedText(g_app.hEditSeconds, g_app.state.seconds);
        setEditFormattedText(g_app.hEditTotalSeconds, g_app.state.timeSeconds);
    }

    if (g_app.state.calcMode == CalcMode::SIZE) {
        SetWindowTextW(g_app.hEditFileSize, utf8ToWide(g_app.calcResult.hasResult ? g_app.calcResult.calculatedSize : "").c_str());
    } else {
        setEditFormattedText(g_app.hEditFileSize, g_app.state.fileSize, !g_app.isFileSizeFocused);
    }

    if (g_app.state.calcMode == CalcMode::SPEED) {
        SetWindowTextW(g_app.hEditSpeed, utf8ToWide(g_app.calcResult.hasResult ? g_app.calcResult.calculatedSpeed : "").c_str());
    } else {
        setEditFormattedText(g_app.hEditSpeed, g_app.state.speed);
    }
    g_app.isUpdatingControls = false;

    RedrawWindow(g_app.hwndMain, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static std::string getCopyBreakdownText() {
    if (g_app.state.calcMode == CalcMode::TIME) {
        return g_app.calcResult.toTimeBreakdownString();
    }
    if (g_app.calcResult.hasResult) {
        std::string r = g_app.calcResult.toTimeBreakdownString();
        if (!r.empty()) return r;
    }
    std::string sb;
    if (!g_app.state.days.empty() && g_app.state.days != "0") {
        sb += g_app.state.days + (g_app.state.days == "1" ? " day" : " days");
    }
    if (!g_app.state.hours.empty() && g_app.state.hours != "0") {
        if (!sb.empty()) sb += ", ";
        sb += g_app.state.hours + (g_app.state.hours == "1" ? " hour" : " hours");
    }
    if (!g_app.state.minutes.empty() && g_app.state.minutes != "0") {
        if (!sb.empty()) sb += ", ";
        sb += g_app.state.minutes + (g_app.state.minutes == "1" ? " minute" : " minutes");
    }
    if (!g_app.state.seconds.empty() && g_app.state.seconds != "0") {
        if (!sb.empty()) sb += ", ";
        sb += g_app.state.seconds + (g_app.state.seconds == "1" ? " second" : " seconds");
    }
    return sb;
}

static std::string getCopySecondsText() {
    if (g_app.state.calcMode == CalcMode::TIME) {
        return g_app.calcResult.toSecondsRawString();
    }
    return g_app.state.timeSeconds;
}

static void copyToClipboardWithToast(const std::string& text) {
    if (text.empty()) return;
    std::wstring wtext = utf8ToWide(text);
    if (OpenClipboard(g_app.hwndMain)) {
        EmptyClipboard();
        size_t bytes = (wtext.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* ptr = GlobalLock(hMem);
            if (ptr) {
                std::memcpy(ptr, wtext.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
    g_app.toastMessage = L"Copied: " + wtext;
    SetTimer(g_app.hwndMain, ID_TIMER_TOAST, 2000, nullptr);
    InvalidateRect(g_app.hwndMain, nullptr, FALSE);
}

// ============================================================================
// Subclassed Edit Control Procedure
// ============================================================================
static WNDPROC g_origEditProc = nullptr;

static LRESULT CALLBACK SubclassedEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int id = GetDlgCtrlID(hwnd);
    if (msg == WM_SETFOCUS || msg == WM_LBUTTONDOWN) {
        if (g_app.openDropdown != 0 || g_app.showInfoTooltip) {
            g_app.openDropdown = 0;
            g_app.hoveredDropdownIndex = -1;
            g_app.showInfoTooltip = false;
            InvalidateRect(g_app.hwndMain, nullptr, FALSE);
        }
    }
    if (msg == WM_SETFOCUS && id == ID_EDIT_FILESIZE) {
        g_app.isFileSizeFocused = true;
        setEditFormattedText(hwnd, g_app.state.fileSize, false);
        int len = GetWindowTextLengthW(hwnd);
        SendMessageW(hwnd, EM_SETSEL, len, len);
    } else if (msg == WM_KILLFOCUS && id == ID_EDIT_FILESIZE) {
        g_app.isFileSizeFocused = false;
        setEditFormattedText(hwnd, g_app.state.fileSize, true);
    } else if (msg == WM_KEYDOWN && wParam == VK_TAB) {
        HWND order[] = {
            g_app.hEditDays,
            g_app.hEditHours,
            g_app.hEditMinutes,
            g_app.hEditSeconds,
            g_app.hEditTotalSeconds,
            g_app.hEditFileSize,
            g_app.hEditSpeed
        };
        int idx = -1;
        for (int i = 0; i < 7; ++i) {
            if (order[i] == hwnd) { idx = i; break; }
        }
        if (idx >= 0) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int next = shift ? (idx + 6) % 7 : (idx + 1) % 7;
            SetFocus(order[next]);
            return 0;
        }
    } else if (msg == WM_CHAR && wParam == VK_TAB) {
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

// ============================================================================
// Handle Edit Changes
// ============================================================================
static void onEditControlChanged(int id, HWND hEdit) {
    if (g_app.isUpdatingControls) return;

    wchar_t wbuf[512] = {0};
    GetWindowTextW(hEdit, wbuf, 511);
    std::string rawInput = wideToUtf8(wbuf);

    if (id == ID_EDIT_FILESIZE) {
        if (g_app.state.calcMode == CalcMode::SIZE) return;
        std::string clean;
        if (!DownloadCalculator::sanitizeFileSizeInput(rawInput, clean)) {
            setEditFormattedText(hEdit, g_app.state.fileSize, !g_app.isFileSizeFocused);
            return;
        }
        if (clean == g_app.state.fileSize) {
            setEditFormattedText(hEdit, g_app.state.fileSize, !g_app.isFileSizeFocused);
            return;
        }
        g_app.state.fileSize = clean;
        reconcileCalcMode(g_app.state);
        PersistenceManager::saveState(g_app.state);
        refreshCalculatedUI();
        return;
    }

    if (id == ID_EDIT_SPEED) {
        if (g_app.state.calcMode == CalcMode::SPEED) return;
        std::string clean = stripCommas(rawInput);
        if (!DownloadCalculator::isValidDecimalInput(clean)) {
            setEditFormattedText(hEdit, g_app.state.speed);
            return;
        }
        if (clean == g_app.state.speed) {
            setEditFormattedText(hEdit, g_app.state.speed);
            return;
        }
        g_app.state.speed = clean;
        reconcileCalcMode(g_app.state);
        PersistenceManager::saveState(g_app.state);
        refreshCalculatedUI();
        return;
    }

    if (id == ID_EDIT_DAYS || id == ID_EDIT_HOURS || id == ID_EDIT_MINUTES || id == ID_EDIT_SECONDS) {
        if (g_app.state.calcMode == CalcMode::TIME) return;
        std::string clean = stripCommas(rawInput);
        std::string* targetField = nullptr;
        if (id == ID_EDIT_DAYS) targetField = &g_app.state.days;
        else if (id == ID_EDIT_HOURS) targetField = &g_app.state.hours;
        else if (id == ID_EDIT_MINUTES) targetField = &g_app.state.minutes;
        else targetField = &g_app.state.seconds;

        if (!DownloadCalculator::isValidDecimalInput(clean)) {
            setEditFormattedText(hEdit, *targetField);
            return;
        }
        if (clean == *targetField) {
            setEditFormattedText(hEdit, *targetField);
            return;
        }
        *targetField = clean;
        g_app.state.timeSeconds = recalculateTotalSeconds(
            g_app.state.days, g_app.state.hours, g_app.state.minutes, g_app.state.seconds
        );
        reconcileCalcMode(g_app.state);
        PersistenceManager::saveState(g_app.state);
        refreshCalculatedUI();
        return;
    }

    if (id == ID_EDIT_TOTAL_SECONDS) {
        if (g_app.state.calcMode == CalcMode::TIME) return;
        std::string clean = stripCommas(rawInput);
        if (!DownloadCalculator::isValidDecimalInput(clean)) {
            setEditFormattedText(hEdit, g_app.state.timeSeconds);
            return;
        }
        if (clean == g_app.state.timeSeconds) {
            setEditFormattedText(hEdit, g_app.state.timeSeconds);
            return;
        }
        g_app.state.timeSeconds = clean;
        double secVal = 0.0;
        std::string dStr, hStr, mStr, sStr;
        if (DownloadCalculator::parseDoubleSafe(clean, secVal) && secVal > 0.0) {
            int64_t totalSec = static_cast<int64_t>(std::llround(secVal));
            int64_t d = totalSec / 86400;
            int64_t remD = totalSec % 86400;
            int64_t h = remD / 3600;
            int64_t remH = remD % 3600;
            int64_t m = remH / 60;
            int64_t s = remH % 60;
            if (d > 0) dStr = std::to_string(d);
            if (h > 0) hStr = std::to_string(h);
            if (m > 0) mStr = std::to_string(m);
            if (s > 0) sStr = std::to_string(s);
        }
        g_app.state.days = dStr;
        g_app.state.hours = hStr;
        g_app.state.minutes = mStr;
        g_app.state.seconds = sStr;
        reconcileCalcMode(g_app.state);

        PersistenceManager::saveState(g_app.state);
        refreshCalculatedUI();
    }
}

// ============================================================================
// High-Precision GDI+ Anti-Aliased Drawing Helpers (Matching Preview 1:1)
// ============================================================================
static void addRoundedRectPath(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
    Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void drawCopyIconGdiPlus(Gdiplus::Graphics& g, float x, float y, Gdiplus::Color color) {
    Gdiplus::Pen pen(color, 1.5f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    // Back sheet (rounded L shape matching Lucide Copy icon)
    Gdiplus::GraphicsPath backPath;
    backPath.AddLine(x + 3.5f, y + 10.5f, x + 2.5f, y + 10.5f);
    backPath.AddArc(x + 1.5f, y + 9.5f, 2.0f, 2.0f, 90.0f, 90.0f);
    backPath.AddLine(x + 1.5f, y + 9.5f, x + 1.5f, y + 2.5f);
    backPath.AddArc(x + 1.5f, y + 1.5f, 2.0f, 2.0f, 180.0f, 90.0f);
    backPath.AddLine(x + 2.5f, y + 1.5f, x + 9.5f, y + 1.5f);
    backPath.AddArc(x + 8.5f, y + 1.5f, 2.0f, 2.0f, 270.0f, 90.0f);
    backPath.AddLine(x + 10.5f, y + 2.5f, x + 10.5f, y + 3.5f);
    g.DrawPath(&pen, &backPath);

    // Front sheet (rounded rectangle)
    Gdiplus::GraphicsPath frontPath;
    addRoundedRectPath(frontPath, x + 5.5f, y + 5.5f, 9.0f, 9.0f, 1.8f);
    g.DrawPath(&pen, &frontPath);
}

static void drawInfoIconGdiPlus(Gdiplus::Graphics& g, float x, float y, Gdiplus::Color color) {
    Gdiplus::Pen pen(color, 1.5f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    g.DrawEllipse(&pen, x + 1.0f, y + 1.0f, 14.0f, 14.0f);
    g.DrawLine(&pen, x + 8.0f, y + 7.5f, x + 8.0f, y + 11.5f);

    Gdiplus::SolidBrush dotBrush(color);
    g.FillEllipse(&dotBrush, x + 7.1f, y + 4.2f, 1.8f, 1.8f);
}

static void drawChevronDownGdiPlus(Gdiplus::Graphics& g, float x, float y, Gdiplus::Color color) {
    Gdiplus::Pen pen(color, 1.75f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    Gdiplus::PointF pts[3] = {
        Gdiplus::PointF(x, y),
        Gdiplus::PointF(x + 4.5f, y + 4.5f),
        Gdiplus::PointF(x + 9.0f, y)
    };
    g.DrawLines(&pen, pts, 3);
}

static void drawUnderlineGdiPlus(Gdiplus::Graphics& g, float left, float right, float y, Gdiplus::Color color) {
    Gdiplus::Pen pen(color, 1.5f);
    g.DrawLine(&pen, left, y, right, y);
}

static void drawRoundedButtonGdiPlus(
    Gdiplus::Graphics& g,
    HDC hdc,
    const RECT& rc,
    Gdiplus::Color bgColor,
    COLORREF textColor,
    const wchar_t* label
) {
    Gdiplus::GraphicsPath path;
    addRoundedRectPath(
        path,
        (Gdiplus::REAL)rc.left,
        (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)(rc.right - rc.left),
        (Gdiplus::REAL)(rc.bottom - rc.top),
        12.0f
    );
    Gdiplus::SolidBrush brush(bgColor);
    g.FillPath(&brush, &path);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    SelectObject(hdc, g_app.hFontButton);
    RECT textRc = rc;
    DrawTextW(hdc, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void paintMainWindow(HWND hwnd, HDC hdc) {
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    bool dark = g_app.state.isDarkMode;
    COLORREF bgRef = dark ? RGB(18, 18, 18) : RGB(255, 255, 255);
    COLORREF textRef = dark ? RGB(248, 250, 252) : RGB(15, 23, 42);

    Gdiplus::Color underlineColor = dark
        ? Gdiplus::Color(255, 226, 232, 240)
        : Gdiplus::Color(255, 15, 23, 42);
    Gdiplus::Color subtleIconColor = dark
        ? Gdiplus::Color(110, 248, 250, 252)
        : Gdiplus::Color(110, 15, 23, 42);
    Gdiplus::Color chevronColor = dark
        ? Gdiplus::Color(235, 248, 250, 252)
        : Gdiplus::Color(235, 15, 23, 42);

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    HBRUSH bgBrush = CreateSolidBrush(bgRef);
    FillRect(memDC, &rcClient, bgBrush);
    DeleteObject(bgBrush);

    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, textRef);
    SelectObject(memDC, g_app.hFontRegular);

    bool showTimeUnderline = (g_app.state.calcMode != CalcMode::TIME);
    bool showSizeUnderline = (g_app.state.calcMode != CalcMode::SIZE);
    bool showSpeedUnderline = (g_app.state.calcMode != CalcMode::SPEED);

    // 1. Time Breakdown Rows (days, hours, minutes, seconds)
    // Centered 235px block starting at x = 82:
    //   Value box & Underline: x = 82..162 (width 80px)
    //   Spacer: 12px (162..174)
    //   Unit label: x = 174..310
    const wchar_t* timeUnits[4] = {L"days", L"hours", L"minutes", L"seconds"};
    int rowYs[4] = {28, 65, 102, 139};
    for (int i = 0; i < 4; ++i) {
        RECT rcUnit = {174, rowYs[i] - 1, 310, rowYs[i] + 25};
        DrawTextW(memDC, timeUnits[i], -1, &rcUnit, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (showTimeUnderline) {
            // Drawn at rowYs[i] + 27.5f (exact same +27.5f offset as File size and Speed)
            drawUnderlineGdiPlus(g, 82.0f, 162.0f, (float)rowYs[i] + 27.5f, underlineColor);
        }
    }

    // Copy breakdown icon button (top right, aligned with Copy Seconds and Info button)
    drawCopyIconGdiPlus(g, (float)(g_app.rcCopyBreakdown.left + 8), (float)(g_app.rcCopyBreakdown.top + 8), subtleIconColor);

    // 2. Total Seconds Row
    // Centered block: Input x = 98..208 (width 110px), Spacer 10px, "seconds" at x = 218
    RECT rcSecLabel = {218, 196, 320, 222};
    DrawTextW(memDC, L"seconds", -1, &rcSecLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (showTimeUnderline) {
        drawUnderlineGdiPlus(g, 98.0f, 208.0f, 224.5f, underlineColor);
    }
    drawCopyIconGdiPlus(g, (float)(g_app.rcCopySeconds.left + 8), (float)(g_app.rcCopySeconds.top + 8), subtleIconColor);

    // 3. File Size Row
    // Centered 260px block starting at x = 70:
    //   "File size:" x = 70..158 (88px)
    //   Input & Underline: x = 158..248 (90px)
    //   Spacer: 8px (248..256)
    //   Unit Dropdown: x = 256..330 (74px)
    RECT rcFileSizeLabel = {70, 265, 158, 291};
    DrawTextW(memDC, L"File size:", -1, &rcFileSizeLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (showSizeUnderline) {
        drawUnderlineGdiPlus(g, 158.0f, 248.0f, 293.5f, underlineColor);
    }

    FileSizeUnit dispSizeUnit = (g_app.state.calcMode == CalcMode::SIZE && g_app.calcResult.hasCalculatedSizeUnit)
        ? g_app.calcResult.calculatedSizeUnit
        : g_app.state.fileSizeUnit;
    std::wstring wSizeUnit = utf8ToWide(fileSizeUnitToLabel(dispSizeUnit));
    RECT rcSizeUnitText = {258, 265, 310, 291};
    DrawTextW(memDC, wSizeUnit.c_str(), -1, &rcSizeUnitText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawChevronDownGdiPlus(g, 312.0f, 276.5f, chevronColor);
    drawInfoIconGdiPlus(g, (float)(g_app.rcInfoButton.left + 8), (float)(g_app.rcInfoButton.top + 8), subtleIconColor);

    // 4. Speed Row
    // Perfectly aligned with File size row (x = 70..158 label, 158..248 input, 256..330 dropdown)
    RECT rcSpeedLabel = {70, 327, 158, 353};
    DrawTextW(memDC, L"Speed:", -1, &rcSpeedLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (showSpeedUnderline) {
        drawUnderlineGdiPlus(g, 158.0f, 248.0f, 355.5f, underlineColor);
    }

    SpeedUnit dispSpeedUnit = (g_app.state.calcMode == CalcMode::SPEED && g_app.calcResult.hasCalculatedSpeedUnit)
        ? g_app.calcResult.calculatedSpeedUnit
        : g_app.state.speedUnit;
    std::wstring wSpeedUnit = utf8ToWide(speedUnitToLabel(dispSpeedUnit));
    RECT rcSpeedUnitText = {258, 327, 310, 353};
    DrawTextW(memDC, wSpeedUnit.c_str(), -1, &rcSpeedUnitText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawChevronDownGdiPlus(g, 312.0f, 338.5f, chevronColor);

    // 5. Action Buttons: [ Dark / Light ] and [ Clear all ]
    if (dark) {
        drawRoundedButtonGdiPlus(g, memDC, g_app.rcModeButton, Gdiplus::Color(255, 255, 255, 255), RGB(0, 0, 0), L"Dark");
    } else {
        drawRoundedButtonGdiPlus(g, memDC, g_app.rcModeButton, Gdiplus::Color(255, 0, 0, 0), RGB(255, 255, 255), L"Light");
    }
    drawRoundedButtonGdiPlus(g, memDC, g_app.rcClearButton, Gdiplus::Color(255, 255, 26, 26), RGB(255, 255, 255), L"Clear all");

    // 6. Info Tooltip Popup (if active)
    if (g_app.showInfoTooltip) {
        RECT rcTip = {76, 229, 368, 259};
        Gdiplus::GraphicsPath tipPath;
        addRoundedRectPath(tipPath, (Gdiplus::REAL)rcTip.left, (Gdiplus::REAL)rcTip.top,
                           (Gdiplus::REAL)(rcTip.right - rcTip.left), (Gdiplus::REAL)(rcTip.bottom - rcTip.top), 8.0f);
        Gdiplus::SolidBrush tipBrush(dark ? Gdiplus::Color(255, 38, 38, 43) : Gdiplus::Color(255, 241, 245, 249));
        Gdiplus::Pen tipPen(dark ? Gdiplus::Color(255, 63, 63, 70) : Gdiplus::Color(255, 203, 213, 225), 1.0f);
        g.FillPath(&tipBrush, &tipPath);
        g.DrawPath(&tipPen, &tipPath);

        SelectObject(memDC, g_app.hFontSmall);
        SetTextColor(memDC, textRef);
        DrawTextW(memDC, L"you can use + (plus) for multiple file size", -1, &rcTip, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // 7. Custom Themed Dropdown Popup (if open)
    if (g_app.openDropdown == 1 || g_app.openDropdown == 2) {
        bool isSize = (g_app.openDropdown == 1);
        RECT rcPop = isSize ? g_app.rcSizePopup : g_app.rcSpeedPopup;
        int itemCount = isSize ? 3 : 4;
        const wchar_t* sizeItems[3] = {L"MB", L"GB", L"TB"};
        const wchar_t* speedItems[4] = {L"KB/s", L"MB/s", L"Mbps", L"Gbps"};

        Gdiplus::GraphicsPath popPath;
        addRoundedRectPath(popPath, (Gdiplus::REAL)rcPop.left, (Gdiplus::REAL)rcPop.top,
                           (Gdiplus::REAL)(rcPop.right - rcPop.left), (Gdiplus::REAL)(rcPop.bottom - rcPop.top), 8.0f);
        Gdiplus::SolidBrush popBg(dark ? Gdiplus::Color(255, 30, 30, 36) : Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Pen popBorder(dark ? Gdiplus::Color(255, 51, 65, 85) : Gdiplus::Color(255, 226, 232, 240), 1.0f);
        g.FillPath(&popBg, &popPath);
        g.DrawPath(&popBorder, &popPath);

        SelectObject(memDC, g_app.hFontSmall);
        SetTextColor(memDC, textRef);
        for (int i = 0; i < itemCount; ++i) {
            RECT rcItem = {rcPop.left + 4, rcPop.top + 4 + i * 30, rcPop.right - 4, rcPop.top + 4 + (i + 1) * 30};
            if (g_app.hoveredDropdownIndex == i) {
                Gdiplus::GraphicsPath hlPath;
                addRoundedRectPath(hlPath, (Gdiplus::REAL)rcItem.left, (Gdiplus::REAL)rcItem.top,
                                   (Gdiplus::REAL)(rcItem.right - rcItem.left), (Gdiplus::REAL)(rcItem.bottom - rcItem.top), 5.0f);
                Gdiplus::SolidBrush hlBrush(Gdiplus::Color(55, 59, 130, 246));
                g.FillPath(&hlBrush, &hlPath);
            }
            RECT rcText = {rcItem.left + 10, rcItem.top, rcItem.right - 6, rcItem.bottom};
            DrawTextW(memDC, isSize ? sizeItems[i] : speedItems[i], -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // 8. Toast Notification
    if (!g_app.toastMessage.empty()) {
        RECT rcToast = {40, 452, 360, 480};
        Gdiplus::GraphicsPath toastPath;
        addRoundedRectPath(toastPath, (Gdiplus::REAL)rcToast.left, (Gdiplus::REAL)rcToast.top,
                           (Gdiplus::REAL)(rcToast.right - rcToast.left), (Gdiplus::REAL)(rcToast.bottom - rcToast.top), 8.0f);
        Gdiplus::SolidBrush toastBrush(Gdiplus::Color(245, 30, 41, 59));
        g.FillPath(&toastBrush, &toastPath);

        SelectObject(memDC, g_app.hFontSmall);
        SetTextColor(memDC, RGB(248, 250, 252));
        DrawTextW(memDC, g_app.toastMessage.c_str(), -1, &rcToast, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

// ============================================================================
// Main Window Procedure
// ============================================================================
static HWND createInputControl(HWND hwndParent, int id, int x, int y, int w, int h, HFONT font) {
    HWND hEdit = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_AUTOHSCROLL,
        x, y, w, h,
        hwndParent,
        (HMENU)(INT_PTR)id,
        GetModuleHandleW(nullptr),
        nullptr
    );
    // Remove CS_PARENTDC so EDIT internal ExtTextOutW is strictly clipped to hEdit's 22px height
    // and can never overwrite the parent window's underline below the control.
    LONG_PTR clsStyle = GetClassLongPtrW(hEdit, GCL_STYLE);
    if (clsStyle & CS_PARENTDC) {
        SetClassLongPtrW(hEdit, GCL_STYLE, clsStyle & ~CS_PARENTDC);
    }
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)font, TRUE);
    g_origEditProc = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)SubclassedEditProc);
    return hEdit;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwndMain = hwnd;

            // Load embedded application icon for both title bar (small) and taskbar/Alt-Tab (big)
            HINSTANCE hInst = GetModuleHandleW(nullptr);
            HICON hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
            HICON hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
            if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
            if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

            // Use "Segoe UI Semibold" for crisp medium-weight labels matching the web/Compose preview
            g_app.hFontRegular = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI Semibold");
            g_app.hFontBold = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_app.hFontSemiBold = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI Semibold");
            g_app.hFontButton = CreateFontW(-19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_app.hFontSmall = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI Semibold");

            g_app.hBgBrushDark = CreateSolidBrush(RGB(18, 18, 18));
            g_app.hBgBrushLight = CreateSolidBrush(RGB(255, 255, 255));

            // Strip CS_PARENTDC from the global EDIT class BEFORE creating any input controls
            // so Windows clips every EDIT control strictly to its own 22px height.
            HWND hTmpEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD, 0, 0, 10, 10, hwnd, nullptr, hInst, nullptr);
            if (hTmpEdit) {
                LONG_PTR clsStyle = GetClassLongPtrW(hTmpEdit, GCL_STYLE);
                if (clsStyle & CS_PARENTDC) {
                    SetClassLongPtrW(hTmpEdit, GCL_STYLE, clsStyle & ~CS_PARENTDC);
                }
                DestroyWindow(hTmpEdit);
            }

            // Each EDIT control is 22px tall with -18px font, placed 4.5px above its +27.5px underline
            g_app.hEditDays         = createInputControl(hwnd, ID_EDIT_DAYS,          82,  27,  80, 22, g_app.hFontBold);
            g_app.hEditHours        = createInputControl(hwnd, ID_EDIT_HOURS,         82,  64,  80, 22, g_app.hFontBold);
            g_app.hEditMinutes      = createInputControl(hwnd, ID_EDIT_MINUTES,       82, 101,  80, 22, g_app.hFontBold);
            g_app.hEditSeconds      = createInputControl(hwnd, ID_EDIT_SECONDS,       82, 138,  80, 22, g_app.hFontBold);
            g_app.hEditTotalSeconds = createInputControl(hwnd, ID_EDIT_TOTAL_SECONDS, 98, 196, 110, 22, g_app.hFontBold);
            g_app.hEditFileSize     = createInputControl(hwnd, ID_EDIT_FILESIZE,     158, 265,  90, 22, g_app.hFontSemiBold);
            g_app.hEditSpeed        = createInputControl(hwnd, ID_EDIT_SPEED,        158, 327,  90, 22, g_app.hFontSemiBold);

            PersistenceManager::loadState(g_app.state);
            reconcileCalcMode(g_app.state);
            applyWindowTheme();
            refreshCalculatedUI();
            SetFocus(g_app.hEditFileSize);
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcEdit = (HDC)wParam;
            bool dark = g_app.state.isDarkMode;
            SetTextColor(hdcEdit, dark ? RGB(248, 250, 252) : RGB(15, 23, 42));
            SetBkColor(hdcEdit, dark ? RGB(18, 18, 18) : RGB(255, 255, 255));
            return (LRESULT)(dark ? g_app.hBgBrushDark : g_app.hBgBrushLight);
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == EN_CHANGE) {
                onEditControlChanged(id, (HWND)lParam);
                return 0;
            }
            switch (id) {
                case ID_MENU_SIZE_MB:
                    g_app.state.fileSizeUnit = FileSizeUnit::MB;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
                case ID_MENU_SIZE_GB:
                    g_app.state.fileSizeUnit = FileSizeUnit::GB;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
                case ID_MENU_SIZE_TB:
                    g_app.state.fileSizeUnit = FileSizeUnit::TB;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
                case ID_MENU_SPEED_KBS:
                    g_app.state.speedUnit = SpeedUnit::KB_S;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
                case ID_MENU_SPEED_MBS:
                    g_app.state.speedUnit = SpeedUnit::MB_S;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
                case ID_MENU_SPEED_MBPS:
                    g_app.state.speedUnit = SpeedUnit::MBPS;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
                case ID_MENU_SPEED_GBPS:
                    g_app.state.speedUnit = SpeedUnit::GBPS;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    break;
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_app.openDropdown != 0) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                RECT rcPop = (g_app.openDropdown == 1) ? g_app.rcSizePopup : g_app.rcSpeedPopup;
                int itemCount = (g_app.openDropdown == 1) ? 3 : 4;
                int newHover = -1;
                if (PtInRect(&rcPop, pt)) {
                    int relY = pt.y - (rcPop.top + 4);
                    if (relY >= 0 && relY < itemCount * 30) {
                        newHover = relY / 30;
                    }
                }
                if (newHover != g_app.hoveredDropdownIndex) {
                    g_app.hoveredDropdownIndex = newHover;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // Check if clicking inside an open custom dropdown popup first
            if (g_app.openDropdown == 1 && PtInRect(&g_app.rcSizePopup, pt)) {
                int idx = (pt.y - (g_app.rcSizePopup.top + 4)) / 30;
                if (idx >= 0 && idx < 3) {
                    FileSizeUnit units[3] = {FileSizeUnit::MB, FileSizeUnit::GB, FileSizeUnit::TB};
                    g_app.state.fileSizeUnit = units[idx];
                    g_app.openDropdown = 0;
                    g_app.hoveredDropdownIndex = -1;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    return 0;
                }
            }
            if (g_app.openDropdown == 2 && PtInRect(&g_app.rcSpeedPopup, pt)) {
                int idx = (pt.y - (g_app.rcSpeedPopup.top + 4)) / 30;
                if (idx >= 0 && idx < 4) {
                    SpeedUnit units[4] = {SpeedUnit::KB_S, SpeedUnit::MB_S, SpeedUnit::MBPS, SpeedUnit::GBPS};
                    g_app.state.speedUnit = units[idx];
                    g_app.openDropdown = 0;
                    g_app.hoveredDropdownIndex = -1;
                    PersistenceManager::saveState(g_app.state);
                    refreshCalculatedUI();
                    return 0;
                }
            }

            if (g_app.showInfoTooltip && !PtInRect(&g_app.rcInfoButton, pt)) {
                g_app.showInfoTooltip = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            if (PtInRect(&g_app.rcSizeDropdown, pt)) {
                g_app.openDropdown = (g_app.openDropdown == 1) ? 0 : 1;
                g_app.hoveredDropdownIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&g_app.rcSpeedDropdown, pt)) {
                g_app.openDropdown = (g_app.openDropdown == 2) ? 0 : 2;
                g_app.hoveredDropdownIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (g_app.openDropdown != 0) {
                g_app.openDropdown = 0;
                g_app.hoveredDropdownIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            if (PtInRect(&g_app.rcCopyBreakdown, pt)) {
                copyToClipboardWithToast(getCopyBreakdownText());
                return 0;
            }
            if (PtInRect(&g_app.rcCopySeconds, pt)) {
                copyToClipboardWithToast(getCopySecondsText());
                return 0;
            }
            if (PtInRect(&g_app.rcInfoButton, pt)) {
                g_app.showInfoTooltip = !g_app.showInfoTooltip;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&g_app.rcModeButton, pt)) {
                g_app.state.isDarkMode = !g_app.state.isDarkMode;
                applyWindowTheme();
                PersistenceManager::saveState(g_app.state);
                refreshCalculatedUI();
                return 0;
            }
            if (PtInRect(&g_app.rcClearButton, pt)) {
                g_app.state.fileSize = "";
                g_app.state.speed = "";
                g_app.state.days = "";
                g_app.state.hours = "";
                g_app.state.minutes = "";
                g_app.state.seconds = "";
                g_app.state.timeSeconds = "";
                g_app.state.calcMode = CalcMode::NONE;
                PersistenceManager::saveState(g_app.state);
                refreshCalculatedUI();
                return 0;
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rcBreakdownArea = {24, 20, 376, 176};
            if (PtInRect(&rcBreakdownArea, pt)) {
                copyToClipboardWithToast(getCopyBreakdownText());
                return 0;
            }
            break;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE && (g_app.openDropdown != 0 || g_app.showInfoTooltip)) {
                g_app.openDropdown = 0;
                g_app.hoveredDropdownIndex = -1;
                g_app.showInfoTooltip = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_SETCURSOR: {
            if ((HWND)wParam == hwnd && LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (PtInRect(&g_app.rcCopyBreakdown, pt) ||
                    PtInRect(&g_app.rcCopySeconds, pt) ||
                    PtInRect(&g_app.rcSizeDropdown, pt) ||
                    PtInRect(&g_app.rcInfoButton, pt) ||
                    PtInRect(&g_app.rcSpeedDropdown, pt) ||
                    PtInRect(&g_app.rcModeButton, pt) ||
                    PtInRect(&g_app.rcClearButton, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == ID_TIMER_TOAST) {
                KillTimer(hwnd, ID_TIMER_TOAST);
                g_app.toastMessage.clear();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paintMainWindow(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE: {
            PersistenceManager::saveState(g_app.state);
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            if (g_app.hFontRegular) DeleteObject(g_app.hFontRegular);
            if (g_app.hFontBold) DeleteObject(g_app.hFontBold);
            if (g_app.hFontSemiBold) DeleteObject(g_app.hFontSemiBold);
            if (g_app.hFontButton) DeleteObject(g_app.hFontButton);
            if (g_app.hFontSmall) DeleteObject(g_app.hFontSmall);
            if (g_app.hBgBrushDark) DeleteObject(g_app.hBgBrushDark);
            if (g_app.hBgBrushLight) DeleteObject(g_app.hBgBrushLight);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"DLCalcWin32Class";
    RegisterClassExW(&wc);

    RECT rc = {0, 0, 400, 490};
    // WS_CLIPCHILDREN prevents the parent window BitBlt and child EDIT controls from overwriting each other
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&rc, style, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"DL Calc",
        style,
        (scrW - winW) / 2,
        (scrH - winH) / 2,
        winW,
        winH,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
