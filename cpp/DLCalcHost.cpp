/**
 * DLCalcHost.cpp
 * Pure C++17 native binary host for serving the compiled Win32 PE32+ executable (DLCalc.exe)
 * and executing DLCalcCore.hpp calculations over POSIX sockets on port 3000.
 */

#include "DLCalcCore.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace dlcalc;

static std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            char* end = nullptr;
            long val = std::strtol(hex, &end, 16);
            if (end == hex + 2) {
                out.push_back(static_cast<char>(val));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

static std::string getQueryParam(const std::string& query, const std::string& key, const std::string& defVal = "") {
    size_t start = 0;
    while (start < query.size()) {
        size_t amp = query.find('&', start);
        size_t end = (amp == std::string::npos) ? query.size() : amp;
        std::string pair = query.substr(start, end - start);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            if (pair.substr(0, eq) == key) {
                return urlDecode(pair.substr(eq + 1));
            }
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return defVal;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

static void sendAll(int clientFd, const void* data, size_t len) {
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t sent = ::send(clientFd, ptr, remaining, MSG_NOSIGNAL);
        if (sent <= 0) break;
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
}

static void sendHttpResponse(
    int clientFd,
    const std::string& status,
    const std::string& contentType,
    const std::string& body,
    const std::string& extraHeaders = ""
) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << extraHeaders
        << "\r\n";
    std::string header = oss.str();
    sendAll(clientFd, header.data(), header.size());
    if (!body.empty()) {
        sendAll(clientFd, body.data(), body.size());
    }
}

static std::string readBinaryFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

static std::string buildCalculationJson(const std::string& query) {
    std::string fileSize = getQueryParam(query, "fileSize", "");
    FileSizeUnit fileUnit = fileSizeUnitFromLabel(getQueryParam(query, "sizeUnit", "GB"));
    std::string speed = getQueryParam(query, "speed", "");
    SpeedUnit speedUnit = speedUnitFromLabel(getQueryParam(query, "speedUnit", "Mbps"));
    std::string timeSeconds = getQueryParam(query, "timeSeconds", "");
    CalcMode mode = calcModeFromString(getQueryParam(query, "mode", "NONE"));

    DownloadTimeResult res = DownloadCalculator::calculate(fileSize, fileUnit, speed, speedUnit, timeSeconds, mode);
    std::string suffix = DownloadCalculator::formatFileSizeResultSuffix(fileSize);

    std::ostringstream oss;
    oss << "{"
        << "\"hasResult\":" << (res.hasResult ? "true" : "false") << ","
        << "\"isBelowOneSecond\":" << (res.isBelowOneSecond ? "true" : "false") << ","
        << "\"formattedDays\":\"" << jsonEscape(res.formattedDays) << "\","
        << "\"formattedHours\":\"" << jsonEscape(res.formattedHours) << "\","
        << "\"formattedMinutes\":\"" << jsonEscape(res.formattedMinutes) << "\","
        << "\"formattedSeconds\":\"" << jsonEscape(res.formattedSeconds) << "\","
        << "\"formattedTotalSeconds\":\"" << jsonEscape(res.formattedTotalSeconds) << "\","
        << "\"calculatedSpeed\":\"" << jsonEscape(res.calculatedSpeed) << "\","
        << "\"calculatedSpeedUnit\":\"" << (res.hasCalculatedSpeedUnit ? speedUnitToLabel(res.calculatedSpeedUnit) : "") << "\","
        << "\"calculatedSize\":\"" << jsonEscape(res.calculatedSize) << "\","
        << "\"calculatedSizeUnit\":\"" << (res.hasCalculatedSizeUnit ? fileSizeUnitToLabel(res.calculatedSizeUnit) : "") << "\","
        << "\"breakdownString\":\"" << jsonEscape(res.toTimeBreakdownString()) << "\","
        << "\"secondsRawString\":\"" << jsonEscape(res.toSecondsRawString()) << "\","
        << "\"fileSizeSuffix\":\"" << jsonEscape(suffix) << "\""
        << "}";
    return oss.str();
}

static std::string buildWin32PreviewDocument() {
    return R"CPPHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>DL Calc — Pure Win32 C++17 Application</title>
<link rel="icon" type="image/x-icon" href="/dlcalc.ico" />
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    min-height: 100vh;
    background: #0b0f17;
    color: #f8fafc;
    font-family: "Segoe UI", -apple-system, BlinkMacSystemFont, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 20px;
    gap: 16px;
  }
  .win32-window {
    width: 400px;
    border-radius: 10px;
    overflow: hidden;
    border: 1px solid #334155;
    box-shadow: 0 24px 48px rgba(0, 0, 0, 0.65);
  }
  .titlebar {
    height: 32px;
    padding: 0 12px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 12px;
    user-select: none;
    border-bottom: 1px solid #27272a;
    background: #18181b;
    color: #e2e8f0;
  }
  .titlebar.light {
    background: #f1f5f9;
    color: #1e293b;
    border-bottom: 1px solid #e2e8f0;
  }
  .titlebar-left {
    display: flex;
    align-items: center;
    gap: 8px;
    font-weight: 600;
  }
  .titlebar-left img {
    width: 16px;
    height: 16px;
  }
  .titlebar-controls {
    display: flex;
    gap: 14px;
    font-family: monospace;
    opacity: 0.7;
  }
  .client-area {
    position: relative;
    width: 400px;
    height: 490px;
    background: #121212;
    color: #f8fafc;
    user-select: none;
  }
  .client-area.light {
    background: #ffffff;
    color: #0f172a;
  }
  .edit-box {
    position: absolute;
    background: transparent;
    border: none;
    outline: none;
    text-align: right;
    font-family: "Segoe UI", sans-serif;
    font-weight: 700;
    font-size: 18px;
    color: inherit;
    padding: 0 2px;
  }
  .underline {
    position: absolute;
    height: 1.5px;
    background: #e2e8f0;
    pointer-events: none;
  }
  .client-area.light .underline {
    background: #0f172a;
  }
  .unit-label {
    position: absolute;
    font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
    font-weight: 600;
    font-size: 20px;
    line-height: 26px;
    pointer-events: none;
  }
  .icon-btn {
    position: absolute;
    width: 32px;
    height: 32px;
    border: none;
    background: transparent;
    color: inherit;
    opacity: 0.42;
    cursor: pointer;
    border-radius: 6px;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .icon-btn:hover {
    opacity: 0.85;
    background: rgba(148, 163, 184, 0.12);
  }
  .dropdown-btn {
    position: absolute;
    height: 38px;
    border: none;
    background: transparent;
    color: inherit;
    font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
    font-weight: 600;
    font-size: 19px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 4px 0 2px;
    cursor: pointer;
  }
  .popup-menu {
    position: absolute;
    width: 92px;
    border-radius: 8px;
    padding: 4px;
    background: #1e1e24;
    border: 1px solid #334155;
    z-index: 30;
    box-shadow: 0 10px 25px rgba(0,0,0,0.45);
    display: none;
  }
  .client-area.light .popup-menu {
    background: #ffffff;
    border-color: #cbd5e1;
  }
  .popup-item {
    height: 30px;
    padding: 0 10px;
    display: flex;
    align-items: center;
    font-size: 13px;
    font-weight: 600;
    border-radius: 5px;
    cursor: pointer;
  }
  .popup-item:hover {
    background: rgba(59, 130, 246, 0.25);
  }
  .action-btn {
    position: absolute;
    height: 46px;
    border: none;
    border-radius: 12px;
    font-family: "Segoe UI", sans-serif;
    font-weight: 700;
    font-size: 19px;
    cursor: pointer;
  }
  .action-btn:active {
    transform: scale(0.97);
  }
  .tooltip {
    position: absolute;
    left: 76px;
    top: 225px;
    width: 292px;
    height: 38px;
    border-radius: 8px;
    background: #26262b;
    border: 1px solid #3f3f46;
    font-size: 12px;
    line-height: 15px;
    font-weight: 600;
    display: none;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    z-index: 25;
  }
  .client-area.light .tooltip {
    background: #f1f5f9;
    border-color: #cbd5e1;
  }
  .toast {
    position: absolute;
    left: 40px;
    top: 452px;
    width: 320px;
    height: 28px;
    border-radius: 8px;
    background: rgba(30, 41, 59, 0.96);
    color: #f8fafc;
    font-size: 12.5px;
    font-weight: 600;
    display: none;
    align-items: center;
    justify-content: center;
    z-index: 40;
  }
  .toolbar {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
    justify-content: center;
  }
  .dl-btn {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 9px 16px;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 600;
    text-decoration: none;
    color: #ffffff;
    background: #2563eb;
    border: 1px solid #3b82f6;
    cursor: pointer;
  }
  .dl-btn:hover { background: #1d4ed8; }
  .dl-btn.secondary {
    background: #1e293b;
    border-color: #334155;
    color: #e2e8f0;
  }
  .dl-btn.secondary:hover { background: #334155; }
  .meta-bar {
    font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
    font-size: 11.5px;
    color: #94a3b8;
    text-align: center;
  }
</style>
</head>
<body>
  <div class="win32-window">
    <div id="titlebar" class="titlebar">
      <div class="titlebar-left">
        <img src="/dlcalc.ico" alt="icon" />
        <span>DL Calc</span>
      </div>
      <div class="titlebar-controls">
        <span>&#9472;</span>
        <span>&#9633;</span>
        <span>&#10005;</span>
      </div>
    </div>

    <div id="clientArea" class="client-area">
      <!-- Days (y=27) -->
      <input id="editDays" class="edit-box" style="left:82px;top:27px;width:80px;height:22px;" inputmode="decimal" />
      <div id="ulDays" class="underline" style="left:82px;top:55px;width:80px;"></div>
      <div class="unit-label" style="left:174px;top:27px;">days</div>

      <!-- Hours (y=64) -->
      <input id="editHours" class="edit-box" style="left:82px;top:64px;width:80px;height:22px;" inputmode="decimal" />
      <div id="ulHours" class="underline" style="left:82px;top:92px;width:80px;"></div>
      <div class="unit-label" style="left:174px;top:64px;">hours</div>

      <!-- Minutes (y=101) -->
      <input id="editMinutes" class="edit-box" style="left:82px;top:101px;width:80px;height:22px;" inputmode="decimal" />
      <div id="ulMinutes" class="underline" style="left:82px;top:129px;width:80px;"></div>
      <div class="unit-label" style="left:174px;top:101px;">minutes</div>

      <!-- Seconds (y=138) -->
      <input id="editSeconds" class="edit-box" style="left:82px;top:138px;width:80px;height:22px;" inputmode="decimal" />
      <div id="ulSeconds" class="underline" style="left:82px;top:166px;width:80px;"></div>
      <div class="unit-label" style="left:174px;top:138px;">seconds</div>

      <!-- Copy Breakdown (336, 26) -->
      <button id="btnCopyBreakdown" class="icon-btn" style="left:336px;top:26px;" title="Copy time breakdown">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/></svg>
      </button>

      <!-- Total Seconds (98, 196) -->
      <input id="editTotalSeconds" class="edit-box" style="left:98px;top:196px;width:110px;height:22px;" inputmode="decimal" />
      <div id="ulTotalSeconds" class="underline" style="left:98px;top:224px;width:110px;"></div>
      <div class="unit-label" style="left:218px;top:196px;">seconds</div>

      <!-- Copy Seconds (336, 194) -->
      <button id="btnCopySeconds" class="icon-btn" style="left:336px;top:194px;" title="Copy raw seconds">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/></svg>
      </button>

      <!-- Info Tooltip -->
      <div id="infoTooltip" class="tooltip">
        <div>you can use + (plus) for multiple file size</div>
        <div>values are stored in %appdata%</div>
      </div>

      <!-- File Size (70, 265) -->
      <div class="unit-label" style="left:70px;top:265px;">File size:</div>
      <input id="editFileSize" class="edit-box" style="left:158px;top:265px;width:90px;height:22px;font-weight:600;" />
      <div id="ulFileSize" class="underline" style="left:158px;top:293px;width:90px;"></div>

      <button id="btnSizeDropdown" class="dropdown-btn" style="left:256px;top:260px;width:74px;">
        <span id="lblSizeUnit">GB</span>
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>
      </button>
      <div id="popupSize" class="popup-menu" style="left:256px;top:300px;">
        <div class="popup-item" data-size="MB">MB</div>
        <div class="popup-item" data-size="GB">GB</div>
        <div class="popup-item" data-size="TB">TB</div>
      </div>

      <!-- Info Button (336, 263) -->
      <button id="btnInfo" class="icon-btn" style="left:336px;top:263px;" title="you can use + (plus) for multiple file size">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4"/><path d="M12 8h.01"/></svg>
      </button>

      <!-- Speed (70, 327) -->
      <div class="unit-label" style="left:70px;top:327px;">Speed:</div>
      <input id="editSpeed" class="edit-box" style="left:158px;top:327px;width:90px;height:22px;font-weight:600;" inputmode="decimal" />
      <div id="ulSpeed" class="underline" style="left:158px;top:355px;width:90px;"></div>

      <button id="btnSpeedDropdown" class="dropdown-btn" style="left:256px;top:322px;width:74px;">
        <span id="lblSpeedUnit">Mbps</span>
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>
      </button>
      <div id="popupSpeed" class="popup-menu" style="left:256px;top:362px;">
        <div class="popup-item" data-speed="KB/s">KB/s</div>
        <div class="popup-item" data-speed="MB/s">MB/s</div>
        <div class="popup-item" data-speed="Mbps">Mbps</div>
        <div class="popup-item" data-speed="Gbps">Gbps</div>
      </div>

      <!-- Dark / Light Mode Button (72, 396, 105x46) -->
      <button id="btnMode" class="action-btn" style="left:72px;top:396px;width:105px;background:#ffffff;color:#000000;">Dark</button>

      <!-- Clear All Button (193, 396, 135x46) -->
      <button id="btnClear" class="action-btn" style="left:193px;top:396px;width:135px;background:#ff1a1a;color:#ffffff;">Clear all</button>

      <!-- Toast -->
      <div id="toast" class="toast"></div>
    </div>
  </div>

  <div class="toolbar">
    <a class="dl-btn" href="/DLCalc.exe" download="DLCalc.exe">Download Standalone DLCalc.exe v2.0 (Win32 PE32+)</a>
  </div>
  <div class="meta-bar">
    DL Calc v2.0 &middot; Pure C++17 Win32 API + GDI+ &middot; Default Persistence: %APPDATA%\DLCalc\settings.ini
  </div>

<script>
  const state = {
    fileSize: '',
    sizeUnit: 'GB',
    speed: '',
    speedUnit: 'Mbps',
    days: '',
    hours: '',
    minutes: '',
    seconds: '',
    timeSeconds: '',
    mode: 'NONE',
    dark: true,
    fileSizeFocused: false,
    lastResult: {}
  };

  const el = (id) => document.getElementById(id);
  const stripCommas = (s) => s.replace(/,/g, '');
  const formatCommas = (s) => {
    if (s.length <= 3) return s;
    let out = '', i = 0;
    const isD = (c) => c >= '0' && c <= '9';
    while (i < s.length) {
      if (isD(s[i]) && (i === 0 || s[i - 1] !== '.')) {
        let end = i + 1;
        while (end < s.length && isD(s[end])) end++;
        for (let k = i; k < end; k++) {
          out += s[k];
          if (k < end - 1 && (end - 1 - k) % 3 === 0) out += ',';
        }
        i = end;
      } else if (s[i] === '.') {
        out += s[i++];
        while (i < s.length && isD(s[i])) out += s[i++];
      } else {
        out += s[i++];
      }
    }
    return out;
  };

  function hasTime() {
    return Boolean(state.timeSeconds || state.days || state.hours || state.minutes || state.seconds);
  }

  function reconcileMode() {
    const hs = state.fileSize.length > 0;
    const hsp = state.speed.length > 0;
    const ht = hasTime();
    if (state.mode === 'TIME' && (!hs || !hsp)) state.mode = 'NONE';
    else if (state.mode === 'SPEED' && (!hs || !ht)) state.mode = 'NONE';
    else if (state.mode === 'SIZE' && (!hsp || !ht)) state.mode = 'NONE';
    if (state.mode === 'NONE') {
      if (hs && hsp) state.mode = 'TIME';
      else if (hs && ht) state.mode = 'SPEED';
      else if (hsp && ht) state.mode = 'SIZE';
    }
  }

  async function syncFromCppEngine() {
    reconcileMode();
    const q = new URLSearchParams({
      fileSize: state.fileSize,
      sizeUnit: state.sizeUnit,
      speed: state.speed,
      speedUnit: state.speedUnit,
      timeSeconds: state.timeSeconds,
      mode: state.mode
    });
    const res = await fetch('/api/calc?' + q.toString());
    const data = await res.json();
    state.lastResult = data;

    const timeEditable = state.mode !== 'TIME';
    const sizeEditable = state.mode !== 'SIZE';
    const speedEditable = state.mode !== 'SPEED';

    ['editDays','editHours','editMinutes','editSeconds','editTotalSeconds'].forEach(id => {
      el(id).readOnly = !timeEditable;
    });
    el('editFileSize').readOnly = !sizeEditable;
    el('editSpeed').readOnly = !speedEditable;

    ['ulDays','ulHours','ulMinutes','ulSeconds','ulTotalSeconds'].forEach(id => {
      el(id).style.display = timeEditable ? 'block' : 'none';
    });
    el('ulFileSize').style.display = sizeEditable ? 'block' : 'none';
    el('ulSpeed').style.display = speedEditable ? 'block' : 'none';

    if (state.mode === 'TIME') {
      el('editDays').value = data.formattedDays || '';
      el('editHours').value = data.formattedHours || '';
      el('editMinutes').value = data.formattedMinutes || '';
      el('editSeconds').value = data.formattedSeconds || '';
      el('editTotalSeconds').value = data.hasResult ? (data.formattedTotalSeconds || '') : '';
    } else {
      el('editDays').value = formatCommas(state.days);
      el('editHours').value = formatCommas(state.hours);
      el('editMinutes').value = formatCommas(state.minutes);
      el('editSeconds').value = formatCommas(state.seconds);
      el('editTotalSeconds').value = formatCommas(state.timeSeconds);
    }

    if (state.mode === 'SIZE') {
      el('editFileSize').value = data.hasResult ? (data.calculatedSize || '') : '';
      el('lblSizeUnit').textContent = data.calculatedSizeUnit || state.sizeUnit;
    } else {
      const base = formatCommas(state.fileSize);
      el('editFileSize').value = (!state.fileSizeFocused && data.fileSizeSuffix) ? (base + data.fileSizeSuffix) : base;
      el('lblSizeUnit').textContent = state.sizeUnit;
    }

    if (state.mode === 'SPEED') {
      el('editSpeed').value = data.hasResult ? (data.calculatedSpeed || '') : '';
      el('lblSpeedUnit').textContent = data.calculatedSpeedUnit || state.speedUnit;
    } else {
      el('editSpeed').value = formatCommas(state.speed);
      el('lblSpeedUnit').textContent = state.speedUnit;
    }
  }

  let toastTimer = null;
  function showToast(msg) {
    if (!msg) return;
    const t = el('toast');
    t.textContent = msg;
    t.style.display = 'flex';
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.style.display = 'none'; }, 2000);
  }

  function copyWithToast(txt) {
    if (!txt) return;
    if (navigator.clipboard) navigator.clipboard.writeText(txt).catch(() => {});
    showToast('Copied: ' + txt);
  }

  el('editFileSize').addEventListener('focus', () => {
    state.fileSizeFocused = true;
    el('editFileSize').value = formatCommas(state.fileSize);
  });
  el('editFileSize').addEventListener('blur', () => {
    state.fileSizeFocused = false;
    syncFromCppEngine();
  });
  el('editFileSize').addEventListener('input', (e) => {
    if (state.mode === 'SIZE') return;
    let raw = e.target.value;
    if (raw.includes('=')) raw = raw.slice(0, raw.indexOf('='));
    raw = raw.replace(/,/g, '').replace(/ /g, '');
    if (!/^\d*(\.\d*)?(\+\d*(\.\d*)?)*$/.test(raw)) {
      e.target.value = formatCommas(state.fileSize);
      return;
    }
    state.fileSize = raw;
    syncFromCppEngine();
  });

  el('editSpeed').addEventListener('input', (e) => {
    if (state.mode === 'SPEED') return;
    const clean = stripCommas(e.target.value);
    if (!/^\d*(\.\d*)?$/.test(clean)) {
      e.target.value = formatCommas(state.speed);
      return;
    }
    state.speed = clean;
    syncFromCppEngine();
  });

  function recalcTotalFromBreakdown() {
    const d = parseFloat(state.days) || 0;
    const h = parseFloat(state.hours) || 0;
    const m = parseFloat(state.minutes) || 0;
    const s = parseFloat(state.seconds) || 0;
    const tot = d * 86400 + h * 3600 + m * 60 + s;
    state.timeSeconds = tot > 0 ? (Number.isInteger(tot) ? String(Math.round(tot)) : String(tot)) : '';
  }

  [['editDays','days'],['editHours','hours'],['editMinutes','minutes'],['editSeconds','seconds']].forEach(([id, key]) => {
    el(id).addEventListener('input', (e) => {
      if (state.mode === 'TIME') return;
      const clean = stripCommas(e.target.value);
      if (!/^\d*(\.\d*)?$/.test(clean)) {
        e.target.value = formatCommas(state[key]);
        return;
      }
      state[key] = clean;
      recalcTotalFromBreakdown();
      syncFromCppEngine();
    });
  });

  el('editTotalSeconds').addEventListener('input', (e) => {
    if (state.mode === 'TIME') return;
    const clean = stripCommas(e.target.value);
    if (!/^\d*(\.\d*)?$/.test(clean)) {
      e.target.value = formatCommas(state.timeSeconds);
      return;
    }
    state.timeSeconds = clean;
    const secVal = parseFloat(clean);
    if (secVal > 0) {
      const totalSec = Math.round(secVal);
      const d = Math.floor(totalSec / 86400);
      const remD = totalSec % 86400;
      const h = Math.floor(remD / 3600);
      const remH = remD % 3600;
      const m = Math.floor(remH / 60);
      const s = remH % 60;
      state.days = d > 0 ? String(d) : '';
      state.hours = h > 0 ? String(h) : '';
      state.minutes = m > 0 ? String(m) : '';
      state.seconds = s > 0 ? String(s) : '';
    } else {
      state.days = state.hours = state.minutes = state.seconds = '';
    }
    syncFromCppEngine();
  });

  el('btnCopyBreakdown').addEventListener('click', () => {
    copyWithToast(state.lastResult.breakdownString || '');
  });
  el('btnCopySeconds').addEventListener('click', () => {
    const txt = state.mode === 'TIME' ? (state.lastResult.secondsRawString || '') : state.timeSeconds;
    copyWithToast(txt);
  });
  el('btnInfo').addEventListener('click', (e) => {
    e.stopPropagation();
    const tip = el('infoTooltip');
    tip.style.display = tip.style.display === 'flex' ? 'none' : 'flex';
  });

  el('btnSizeDropdown').addEventListener('click', (e) => {
    e.stopPropagation();
    const p = el('popupSize');
    el('popupSpeed').style.display = 'none';
    p.style.display = p.style.display === 'block' ? 'none' : 'block';
  });
  el('btnSpeedDropdown').addEventListener('click', (e) => {
    e.stopPropagation();
    const p = el('popupSpeed');
    el('popupSize').style.display = 'none';
    p.style.display = p.style.display === 'block' ? 'none' : 'block';
  });

  document.querySelectorAll('[data-size]').forEach(item => {
    item.addEventListener('click', () => {
      state.sizeUnit = item.getAttribute('data-size');
      el('popupSize').style.display = 'none';
      syncFromCppEngine();
    });
  });
  document.querySelectorAll('[data-speed]').forEach(item => {
    item.addEventListener('click', () => {
      state.speedUnit = item.getAttribute('data-speed');
      el('popupSpeed').style.display = 'none';
      syncFromCppEngine();
    });
  });

  document.addEventListener('click', () => {
    el('popupSize').style.display = 'none';
    el('popupSpeed').style.display = 'none';
    el('infoTooltip').style.display = 'none';
  });

  el('btnMode').addEventListener('click', () => {
    state.dark = !state.dark;
    el('clientArea').classList.toggle('light', !state.dark);
    el('titlebar').classList.toggle('light', !state.dark);
    el('btnMode').textContent = state.dark ? 'Dark' : 'Light';
    el('btnMode').style.background = state.dark ? '#ffffff' : '#000000';
    el('btnMode').style.color = state.dark ? '#000000' : '#ffffff';
  });

  el('btnClear').addEventListener('click', () => {
    state.fileSize = state.speed = state.days = state.hours = state.minutes = state.seconds = state.timeSeconds = '';
    state.mode = 'NONE';
    syncFromCppEngine();
  });

  syncFromCppEngine();
</script>
</body>
</html>)CPPHTML";
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);

    int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(3000);

    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind port 3000\n";
        ::close(serverFd);
        return 1;
    }

    if (::listen(serverFd, 64) < 0) {
        std::cerr << "Failed to listen on port 3000\n";
        ::close(serverFd);
        return 1;
    }

    std::cout << "DLCalc C++17 native host listening on http://0.0.0.0:3000\n" << std::flush;

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = ::accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) continue;

        char buf[4096] = {0};
        ssize_t n = ::recv(clientFd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            ::close(clientFd);
            continue;
        }

        std::string req(buf, static_cast<size_t>(n));
        std::istringstream iss(req);
        std::string method, target;
        iss >> method >> target;

        std::string path = target;
        std::string query = "";
        size_t qPos = target.find('?');
        if (qPos != std::string::npos) {
            path = target.substr(0, qPos);
            query = target.substr(qPos + 1);
        }

        if (path == "/DLCalc.exe") {
            std::string exeBytes = readBinaryFile("DLCalc.exe");
            if (exeBytes.empty()) {
                sendHttpResponse(clientFd, "404 Not Found", "text/plain", "DLCalc.exe not found");
            } else {
                sendHttpResponse(
                    clientFd,
                    "200 OK",
                    "application/vnd.microsoft.portable-executable",
                    exeBytes,
                    "Content-Disposition: attachment; filename=\"DLCalc.exe\"\r\n"
                );
            }
        } else if (path == "/dlcalc.ico" || path == "/favicon.ico") {
            std::string icoBytes = readBinaryFile("cpp/dlcalc.ico");
            sendHttpResponse(clientFd, "200 OK", "image/x-icon", icoBytes);
        } else if (path == "/api/calc") {
            std::string json = buildCalculationJson(query);
            sendHttpResponse(clientFd, "200 OK", "application/json; charset=utf-8", json);
        } else {
            std::string html = buildWin32PreviewDocument();
            sendHttpResponse(clientFd, "200 OK", "text/html; charset=utf-8", html);
        }

        ::close(clientFd);
    }

    ::close(serverFd);
    return 0;
}
