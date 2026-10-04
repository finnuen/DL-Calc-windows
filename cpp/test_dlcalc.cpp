#include "DLCalcCore.hpp"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace dlcalc;

int main() {
    // 1. Empty or invalid inputs
    auto emptyResult = DownloadCalculator::calculate("", FileSizeUnit::GB, "", SpeedUnit::MBPS);
    assert(!emptyResult.hasResult);
    auto invalidResult = DownloadCalculator::calculate("abc", FileSizeUnit::GB, "10", SpeedUnit::MBPS);
    assert(!invalidResult.hasResult);
    auto zeroSpeedResult = DownloadCalculator::calculate("10", FileSizeUnit::GB, "0", SpeedUnit::MBPS);
    assert(!zeroSpeedResult.hasResult);

    // 2. 10 GB at 100 Mbps -> 859 seconds (14m 19s)
    auto res10GB = DownloadCalculator::calculate("10", FileSizeUnit::GB, "100", SpeedUnit::MBPS);
    assert(res10GB.hasResult);
    assert(res10GB.days == 0);
    assert(res10GB.hours == 0);
    assert(res10GB.minutes == 14);
    assert(res10GB.seconds == 19);
    assert(res10GB.totalSeconds == 859);
    assert(res10GB.formattedTotalSeconds == "859");
    assert(res10GB.toTimeBreakdownString() == "14 minutes, 19 seconds");
    assert(res10GB.toSecondsRawString() == "859");

    // 3. Comma and dot decimals
    auto resComma = DownloadCalculator::calculate("1,5", FileSizeUnit::GB, "100", SpeedUnit::MBPS);
    auto resDot = DownloadCalculator::calculate("1.5", FileSizeUnit::GB, "100", SpeedUnit::MBPS);
    assert(resComma.hasResult && resDot.hasResult);
    assert(resComma.totalSeconds == resDot.totalSeconds);

    // 4. Below 1 second
    auto resSubSec = DownloadCalculator::calculate("1", FileSizeUnit::MB, "1", SpeedUnit::GBPS);
    assert(resSubSec.hasResult);
    assert(resSubSec.isBelowOneSecond);
    assert(resSubSec.formattedTotalSeconds == "1<");
    assert(resSubSec.toSecondsRawString() == "1<");
    assert(resSubSec.toTimeBreakdownString() == "< 1 second");

    // 5. Large file (5 TB at 50 Mbps)
    auto res5TB = DownloadCalculator::calculate("5", FileSizeUnit::TB, "50", SpeedUnit::MBPS);
    assert(res5TB.hasResult);
    assert(res5TB.days == 10);
    assert(res5TB.hours == 4);
    assert(res5TB.minutes == 20);
    assert(res5TB.seconds == 9);
    assert(res5TB.totalSeconds == 879609);
    assert(res5TB.formattedTotalSeconds == "879,609");
    assert(res5TB.toTimeBreakdownString() == "10 days, 4 hours, 20 minutes, 9 seconds");
    assert(res5TB.toSecondsRawString() == "879609");

    // 6. Triangle calculation: Speed from Size & Time
    auto speedRes = DownloadCalculator::calculate("100", FileSizeUnit::MB, "", SpeedUnit::MB_S, "10", CalcMode::SPEED);
    assert(speedRes.hasResult);
    assert(speedRes.calculatedSpeed == "10");
    assert(speedRes.totalSeconds == 10);
    assert(speedRes.toTimeBreakdownString() == "10 seconds");

    // 7. Triangle calculation: Size from Speed & Time
    auto sizeRes = DownloadCalculator::calculate("", FileSizeUnit::MB, "10", SpeedUnit::MB_S, "10", CalcMode::SIZE);
    assert(sizeRes.hasResult);
    assert(sizeRes.calculatedSize == "100");
    assert(sizeRes.calculatedSizeUnit == FileSizeUnit::MB);
    assert(sizeRes.totalSeconds == 10);

    // 8. Auto-raise / lower units
    auto [spd1, u1] = DownloadCalculator::determineBestSpeedUnit(125000000.0, SpeedUnit::MBPS);
    assert(u1 == SpeedUnit::GBPS && std::abs(spd1 - 1.0) < 0.001);
    auto [spd2, u2] = DownloadCalculator::determineBestSpeedUnit(62500000.0, SpeedUnit::GBPS);
    assert(u2 == SpeedUnit::MBPS && std::abs(spd2 - 500.0) < 0.001);

    // 9. File size addition expressions & suffixes
    auto exprRes = DownloadCalculator::calculate("40+10+50", FileSizeUnit::GB, "100", SpeedUnit::MBPS);
    auto directRes = DownloadCalculator::calculate("100", FileSizeUnit::GB, "100", SpeedUnit::MBPS);
    assert(exprRes.hasResult && exprRes.totalSeconds == directRes.totalSeconds);
    assert(DownloadCalculator::formatFileSizeResultSuffix("5+5+5") == "=15");
    assert(DownloadCalculator::formatFileSizeResultSuffix("1000+500") == "=1,500");
    assert(DownloadCalculator::formatFileSizeResultSuffix("40++50") == "=90");
    assert(DownloadCalculator::formatFileSizeResultSuffix("50+") == "");
    assert(DownloadCalculator::formatTextWithThousandsCommas("1000+2500.5+50") == "1,000+2,500.5+50");

    std::cout << "All C++17 DLCalcCore unit tests passed!" << std::endl;
    return 0;
}
