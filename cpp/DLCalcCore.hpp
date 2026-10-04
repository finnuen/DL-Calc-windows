#pragma once
/**
 * DLCalcCore.hpp
 * Complete C++17 port of DL Calc's business logic, bidirectional unit calculator,
 * expression evaluator, thousands-separator formatter, and state manager.
 */

#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace dlcalc {

enum class FileSizeUnit {
    MB = 0,
    GB = 1,
    TB = 2
};

enum class SpeedUnit {
    KB_S = 0,
    MB_S = 1,
    MBPS = 2,
    GBPS = 3
};

enum class CalcMode {
    NONE = 0,
    TIME = 1,
    SPEED = 2,
    SIZE = 3
};

inline std::string fileSizeUnitToLabel(FileSizeUnit u) {
    switch (u) {
        case FileSizeUnit::MB: return "MB";
        case FileSizeUnit::GB: return "GB";
        case FileSizeUnit::TB: return "TB";
    }
    return "GB";
}

inline double fileSizeUnitMultiplier(FileSizeUnit u) {
    switch (u) {
        case FileSizeUnit::MB: return 1024.0 * 1024.0;
        case FileSizeUnit::GB: return 1024.0 * 1024.0 * 1024.0;
        case FileSizeUnit::TB: return 1024.0 * 1024.0 * 1024.0 * 1024.0;
    }
    return 1024.0 * 1024.0 * 1024.0;
}

inline FileSizeUnit fileSizeUnitFromLabel(const std::string& label) {
    std::string up = label;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    if (up == "MB") return FileSizeUnit::MB;
    if (up == "TB") return FileSizeUnit::TB;
    return FileSizeUnit::GB;
}

inline std::string speedUnitToLabel(SpeedUnit u) {
    switch (u) {
        case SpeedUnit::KB_S: return "KB/s";
        case SpeedUnit::MB_S: return "MB/s";
        case SpeedUnit::MBPS: return "Mbps";
        case SpeedUnit::GBPS: return "Gbps";
    }
    return "Mbps";
}

inline double speedUnitMultiplier(SpeedUnit u) {
    switch (u) {
        case SpeedUnit::KB_S: return 1024.0;
        case SpeedUnit::MB_S: return 1024.0 * 1024.0;
        case SpeedUnit::MBPS: return 1000000.0 / 8.0;
        case SpeedUnit::GBPS: return 1000000000.0 / 8.0;
    }
    return 1000000.0 / 8.0;
}

inline SpeedUnit speedUnitFromLabel(const std::string& label) {
    std::string low = label;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low == "kb/s") return SpeedUnit::KB_S;
    if (low == "mb/s") return SpeedUnit::MB_S;
    if (low == "gbps") return SpeedUnit::GBPS;
    return SpeedUnit::MBPS;
}

inline std::string calcModeToString(CalcMode m) {
    switch (m) {
        case CalcMode::NONE: return "NONE";
        case CalcMode::TIME: return "TIME";
        case CalcMode::SPEED: return "SPEED";
        case CalcMode::SIZE: return "SIZE";
    }
    return "NONE";
}

inline CalcMode calcModeFromString(const std::string& s) {
    if (s == "TIME") return CalcMode::TIME;
    if (s == "SPEED") return CalcMode::SPEED;
    if (s == "SIZE") return CalcMode::SIZE;
    return CalcMode::NONE;
}

struct DownloadTimeResult {
    bool hasResult = false;
    bool isBelowOneSecond = false;
    int64_t days = 0;
    int64_t hours = 0;
    int64_t minutes = 0;
    int64_t seconds = 0;
    int64_t totalSeconds = 0;
    std::string formattedDays;
    std::string formattedHours;
    std::string formattedMinutes;
    std::string formattedSeconds;
    std::string formattedTotalSeconds;
    std::string calculatedSpeed;
    bool hasCalculatedSpeedUnit = false;
    SpeedUnit calculatedSpeedUnit = SpeedUnit::MBPS;
    std::string calculatedSize;
    bool hasCalculatedSizeUnit = false;
    FileSizeUnit calculatedSizeUnit = FileSizeUnit::GB;
    CalcMode mode = CalcMode::TIME;

    std::string toTimeBreakdownString() const {
        if (!hasResult && totalSeconds <= 0 && !isBelowOneSecond) return "";
        if (isBelowOneSecond) return "< 1 second";

        std::string sb;
        if (days > 0) {
            sb += std::to_string(days) + (days == 1 ? " day" : " days");
        }
        if (hours > 0) {
            if (!sb.empty()) sb += ", ";
            sb += std::to_string(hours) + (hours == 1 ? " hour" : " hours");
        }
        if (minutes > 0) {
            if (!sb.empty()) sb += ", ";
            sb += std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
        }
        if (seconds > 0) {
            if (!sb.empty()) sb += ", ";
            sb += std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
        }
        return sb.empty() ? "0 seconds" : sb;
    }

    std::string toSecondsRawString() const {
        if (!hasResult && totalSeconds <= 0 && !isBelowOneSecond) return "";
        return isBelowOneSecond ? "1<" : std::to_string(totalSeconds);
    }
};

class DownloadCalculator {
public:
    static std::string trim(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
        return s.substr(start, end - start);
    }

    static std::string formatWithCommas(int64_t value) {
        if (value < 0) return "-" + formatWithCommas(-value);
        std::string s = std::to_string(value);
        if (s.length() <= 3) return s;
        std::string out;
        out.reserve(s.length() + (s.length() - 1) / 3);
        size_t len = s.length();
        for (size_t i = 0; i < len; ++i) {
            out.push_back(s[i]);
            if (i < len - 1 && (len - 1 - i) % 3 == 0) {
                out.push_back(',');
            }
        }
        return out;
    }

    /**
     * Formats arbitrary numeric/expression strings with thousands commas for integer runs
     * (matching ThousandsSeparatorVisualTransformation.formatText in Kotlin).
     */
    static std::string formatTextWithThousandsCommas(const std::string& originalText) {
        size_t origLen = originalText.length();
        if (origLen <= 3) return originalText;

        std::string sb;
        sb.reserve(origLen + origLen / 3);
        size_t i = 0;
        while (i < origLen) {
            char ch = originalText[i];
            if (std::isdigit(static_cast<unsigned char>(ch)) && (i == 0 || originalText[i - 1] != '.')) {
                size_t runEnd = i + 1;
                while (runEnd < origLen && std::isdigit(static_cast<unsigned char>(originalText[runEnd]))) {
                    runEnd++;
                }
                for (size_t oIdx = i; oIdx < runEnd; ++oIdx) {
                    sb.push_back(originalText[oIdx]);
                    if (oIdx < runEnd - 1 && (runEnd - 1 - oIdx) % 3 == 0) {
                        sb.push_back(',');
                    }
                }
                i = runEnd;
            } else if (ch == '.') {
                sb.push_back(ch);
                i++;
                while (i < origLen && std::isdigit(static_cast<unsigned char>(originalText[i]))) {
                    sb.push_back(originalText[i]);
                    i++;
                }
            } else {
                sb.push_back(ch);
                i++;
            }
        }
        return sb;
    }

    static std::string formatDecimal(double value) {
        if (std::isnan(value) || std::isinf(value) || value < 0.0) return "";
        if (value == 0.0) return "0";

        int decimals = (value < 0.01) ? 4 : 2;
        double factor = std::pow(10.0, decimals);
        double rounded = std::round(value * factor) / factor;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(decimals) << rounded;
        std::string s = oss.str();

        // Strip trailing zeros after decimal point
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }

        // Apply thousands commas to integer part
        size_t dotPos = s.find('.');
        std::string intPart = (dotPos == std::string::npos) ? s : s.substr(0, dotPos);
        std::string fracPart = (dotPos == std::string::npos) ? "" : s.substr(dotPos);
        try {
            int64_t intVal = std::stoll(intPart);
            return formatWithCommas(intVal) + fracPart;
        } catch (...) {
            return s;
        }
    }

    static std::pair<double, SpeedUnit> determineBestSpeedUnit(double bytesPerSec, SpeedUnit preferredUnit) {
        if (bytesPerSec <= 0.0 || std::isnan(bytesPerSec) || std::isinf(bytesPerSec)) {
            return {0.0, preferredUnit};
        }
        bool isBitRate = (preferredUnit == SpeedUnit::MBPS || preferredUnit == SpeedUnit::GBPS);
        if (isBitRate) {
            double mbps = (bytesPerSec * 8.0) / 1000000.0;
            if (mbps >= 1000.0) {
                double gbps = (bytesPerSec * 8.0) / 1000000000.0;
                return {gbps, SpeedUnit::GBPS};
            } else {
                return {mbps, SpeedUnit::MBPS};
            }
        } else {
            double kbPerSec = bytesPerSec / 1024.0;
            if (kbPerSec >= 1024.0) {
                double mbPerSec = kbPerSec / 1024.0;
                return {mbPerSec, SpeedUnit::MB_S};
            } else {
                return {kbPerSec, SpeedUnit::KB_S};
            }
        }
    }

    static std::pair<double, FileSizeUnit> determineBestSizeUnit(double totalBytes, FileSizeUnit preferredUnit) {
        if (totalBytes <= 0.0 || std::isnan(totalBytes) || std::isinf(totalBytes)) {
            return {0.0, preferredUnit};
        }
        double bytesInTB = fileSizeUnitMultiplier(FileSizeUnit::TB);
        double bytesInGB = fileSizeUnitMultiplier(FileSizeUnit::GB);
        double bytesInMB = fileSizeUnitMultiplier(FileSizeUnit::MB);

        if (totalBytes >= bytesInTB) return {totalBytes / bytesInTB, FileSizeUnit::TB};
        if (totalBytes >= bytesInGB) return {totalBytes / bytesInGB, FileSizeUnit::GB};
        return {totalBytes / bytesInMB, FileSizeUnit::MB};
    }

    static std::string cleanNumericString(const std::string& str) {
        std::string trimmed = trim(str);
        size_t commaIndex = trimmed.find(',');
        if (commaIndex == std::string::npos) return trimmed;

        int commaCount = 0;
        bool hasDot = false;
        for (char c : trimmed) {
            if (c == ',') commaCount++;
            else if (c == '.') hasDot = true;
        }
        bool isThousandsComma = (commaCount > 1) ||
                                hasDot ||
                                (trimmed.length() - 1 - commaIndex == 3 && commaIndex > 0);
        std::string out;
        out.reserve(trimmed.size());
        if (isThousandsComma) {
            for (char c : trimmed) {
                if (c != ',') out.push_back(c);
            }
        } else {
            for (char c : trimmed) {
                out.push_back(c == ',' ? '.' : c);
            }
        }
        return out;
    }

    static bool parseDoubleSafe(const std::string& s, double& out) {
        std::string cl = cleanNumericString(s);
        if (cl.empty()) return false;
        char* endPtr = nullptr;
        out = std::strtod(cl.c_str(), &endPtr);
        if (endPtr == cl.c_str() || *endPtr != '\0') return false;
        if (std::isnan(out) || std::isinf(out)) return false;
        return true;
    }

    static bool evaluateFileSizeExpression(const std::string& str, double& outSum) {
        if (str.empty()) return false;
        if (str.find('+') == std::string::npos) {
            double v = 0.0;
            if (!parseDoubleSafe(str, v) || v < 0.0) return false;
            outSum = v;
            return true;
        }
        double sum = 0.0;
        bool hasValidPart = false;
        size_t start = 0;
        size_t len = str.length();
        while (start <= len) {
            size_t plusIdx = str.find('+', start);
            size_t end = (plusIdx == std::string::npos) ? len : plusIdx;
            if (end > start) {
                std::string segment = trim(str.substr(start, end - start));
                if (!segment.empty()) {
                    double value = 0.0;
                    if (!parseDoubleSafe(segment, value) || value < 0.0) return false;
                    sum += value;
                    hasValidPart = true;
                }
            }
            if (plusIdx == std::string::npos) break;
            start = plusIdx + 1;
        }
        if (!hasValidPart) return false;
        outSum = sum;
        return true;
    }

    static std::string formatFileSizeResultSuffix(const std::string& str) {
        size_t len = str.length();
        if (len < 3 || str.find('+') == std::string::npos) return "";
        double sum = 0.0;
        int validPartCount = 0;
        size_t start = 0;
        while (start <= len) {
            size_t plusIdx = str.find('+', start);
            size_t end = (plusIdx == std::string::npos) ? len : plusIdx;
            if (end > start) {
                std::string p = trim(str.substr(start, end - start));
                if (!p.empty()) {
                    double val = 0.0;
                    if (!parseDoubleSafe(p, val) || val < 0.0) return "";
                    sum += val;
                    validPartCount++;
                }
            }
            if (plusIdx == std::string::npos) break;
            start = plusIdx + 1;
        }
        if (validPartCount < 2) return "";

        // Format sum without trailing zeros and with thousands commas
        double roundedInt = std::round(sum);
        if (std::abs(sum - roundedInt) < 1e-9) {
            return "=" + formatWithCommas(static_cast<int64_t>(roundedInt));
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << sum;
        std::string plain = oss.str();
        while (!plain.empty() && plain.back() == '0') plain.pop_back();
        if (!plain.empty() && plain.back() == '.') plain.pop_back();
        size_t dotIdx = plain.find('.');
        if (dotIdx != std::string::npos) {
            try {
                int64_t intPart = std::stoll(plain.substr(0, dotIdx));
                return "=" + formatWithCommas(intPart) + plain.substr(dotIdx);
            } catch (...) {}
        }
        return "=" + plain;
    }

    static DownloadTimeResult buildTimeBreakdown(double totalSecondsExact) {
        DownloadTimeResult empty;
        if (std::isnan(totalSecondsExact) || std::isinf(totalSecondsExact) || totalSecondsExact <= 0.0) {
            return empty;
        }
        if (totalSecondsExact < 1.0) {
            DownloadTimeResult res;
            res.hasResult = true;
            res.isBelowOneSecond = true;
            res.formattedSeconds = "1<";
            res.formattedTotalSeconds = "1<";
            return res;
        }
        const int64_t maxSafeSeconds = 3153600000000LL;
        int64_t totalSeconds = (totalSecondsExact >= static_cast<double>(maxSafeSeconds))
            ? maxSafeSeconds
            : static_cast<int64_t>(std::llround(totalSecondsExact));

        int64_t days = totalSeconds / 86400;
        int64_t remAfterDays = totalSeconds % 86400;
        int64_t hours = remAfterDays / 3600;
        int64_t remAfterHours = remAfterDays % 3600;
        int64_t minutes = remAfterHours / 60;
        int64_t seconds = remAfterHours % 60;

        DownloadTimeResult res;
        res.hasResult = true;
        res.isBelowOneSecond = false;
        res.days = days;
        res.hours = hours;
        res.minutes = minutes;
        res.seconds = seconds;
        res.totalSeconds = totalSeconds;
        res.formattedDays = (days > 0) ? formatWithCommas(days) : "";
        res.formattedHours = (hours > 0) ? formatWithCommas(hours) : "";
        res.formattedMinutes = (minutes > 0) ? formatWithCommas(minutes) : "";
        res.formattedSeconds = (seconds > 0) ? formatWithCommas(seconds) : "";
        res.formattedTotalSeconds = formatWithCommas(totalSeconds);
        return res;
    }

    static DownloadTimeResult calculate(
        const std::string& fileSizeStr,
        FileSizeUnit fileUnit,
        const std::string& speedStr,
        SpeedUnit speedUnit,
        const std::string& timeStr = "",
        CalcMode mode = CalcMode::TIME
    ) {
        std::string cleanSpeedStr = cleanNumericString(speedStr);
        std::string cleanTimeStr = cleanNumericString(timeStr);

        switch (mode) {
            case CalcMode::TIME: {
                if (trim(fileSizeStr).empty() || cleanSpeedStr.empty()) {
                    DownloadTimeResult r;
                    r.mode = CalcMode::TIME;
                    return r;
                }
                double size = 0.0, speed = 0.0;
                if (!evaluateFileSizeExpression(fileSizeStr, size) || !parseDoubleSafe(cleanSpeedStr, speed)) {
                    return DownloadTimeResult{};
                }
                if (size <= 0.0 || speed <= 0.0) return DownloadTimeResult{};

                double totalBytes = size * fileSizeUnitMultiplier(fileUnit);
                double bytesPerSec = speed * speedUnitMultiplier(speedUnit);
                if (bytesPerSec <= 0.0) return DownloadTimeResult{};

                double totalSecondsExact = totalBytes / bytesPerSec;
                DownloadTimeResult res = buildTimeBreakdown(totalSecondsExact);
                res.mode = CalcMode::TIME;
                return res;
            }
            case CalcMode::SPEED: {
                double timeSeconds = 0.0;
                bool hasValidTime = parseDoubleSafe(cleanTimeStr, timeSeconds) && timeSeconds > 0.0;
                DownloadTimeResult timeBreakdown;
                if (hasValidTime) {
                    timeBreakdown = buildTimeBreakdown(timeSeconds);
                }
                timeBreakdown.mode = CalcMode::SPEED;

                double size = 0.0;
                if (trim(fileSizeStr).empty() || !hasValidTime || !evaluateFileSizeExpression(fileSizeStr, size) || size <= 0.0) {
                    timeBreakdown.hasResult = false;
                    return timeBreakdown;
                }

                double totalBytes = size * fileSizeUnitMultiplier(fileUnit);
                double bytesPerSec = totalBytes / timeSeconds;
                auto [speedExact, bestSpeedUnit] = determineBestSpeedUnit(bytesPerSec, speedUnit);
                std::string calcSpeedStr = formatDecimal(speedExact);

                timeBreakdown.hasResult = !calcSpeedStr.empty();
                timeBreakdown.calculatedSpeed = calcSpeedStr;
                timeBreakdown.hasCalculatedSpeedUnit = true;
                timeBreakdown.calculatedSpeedUnit = bestSpeedUnit;
                timeBreakdown.mode = CalcMode::SPEED;
                return timeBreakdown;
            }
            case CalcMode::SIZE: {
                double timeSeconds = 0.0;
                bool hasValidTime = parseDoubleSafe(cleanTimeStr, timeSeconds) && timeSeconds > 0.0;
                DownloadTimeResult timeBreakdown;
                if (hasValidTime) {
                    timeBreakdown = buildTimeBreakdown(timeSeconds);
                }
                timeBreakdown.mode = CalcMode::SIZE;

                double speed = 0.0;
                if (cleanSpeedStr.empty() || !hasValidTime || !parseDoubleSafe(cleanSpeedStr, speed) || speed <= 0.0) {
                    timeBreakdown.hasResult = false;
                    return timeBreakdown;
                }

                double bytesPerSec = speed * speedUnitMultiplier(speedUnit);
                double totalBytes = bytesPerSec * timeSeconds;
                auto [sizeExact, bestSizeUnit] = determineBestSizeUnit(totalBytes, fileUnit);
                std::string calcSizeStr = formatDecimal(sizeExact);

                timeBreakdown.hasResult = !calcSizeStr.empty();
                timeBreakdown.calculatedSize = calcSizeStr;
                timeBreakdown.hasCalculatedSizeUnit = true;
                timeBreakdown.calculatedSizeUnit = bestSizeUnit;
                timeBreakdown.mode = CalcMode::SIZE;
                return timeBreakdown;
            }
            case CalcMode::NONE: {
                double timeSeconds = 0.0;
                if (parseDoubleSafe(cleanTimeStr, timeSeconds) && timeSeconds > 0.0) {
                    DownloadTimeResult res = buildTimeBreakdown(timeSeconds);
                    res.hasResult = false;
                    res.mode = CalcMode::NONE;
                    return res;
                }
                DownloadTimeResult r;
                r.mode = CalcMode::NONE;
                return r;
            }
        }
        return DownloadTimeResult{};
    }

    static bool isValidDecimalInput(const std::string& s) {
        bool seenDot = false;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) continue;
            if (c == '.' && !seenDot) {
                seenDot = true;
                continue;
            }
            return false;
        }
        return true;
    }

    static bool sanitizeFileSizeInput(const std::string& raw, std::string& outClean) {
        std::string clean = raw;
        size_t eqPos = clean.find('=');
        if (eqPos != std::string::npos) {
            clean = clean.substr(0, eqPos);
        }
        std::string stripped;
        stripped.reserve(clean.size());
        for (char c : clean) {
            if (c != ',' && c != ' ') stripped.push_back(c);
        }
        // Validate against ^\d*(\.\d*)?(\+\d*(\.\d*)?)*$
        bool seenDotInSegment = false;
        for (char c : stripped) {
            if (std::isdigit(static_cast<unsigned char>(c))) continue;
            if (c == '.') {
                if (seenDotInSegment) return false;
                seenDotInSegment = true;
            } else if (c == '+') {
                seenDotInSegment = false;
            } else {
                return false;
            }
        }
        outClean = stripped;
        return true;
    }
};

} // namespace dlcalc
