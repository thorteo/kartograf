#include <doctest/doctest.h>

#include <string>

#include "ui/Hud.h"

using tarkov::ui::formatDuration;
using tarkov::ui::formatGameClock;

namespace {
std::string duration(double seconds) {
    char buffer[64];
    formatDuration(seconds, buffer, sizeof(buffer));
    return buffer;
}
std::string clock(double hours) {
    char buffer[64];
    formatGameClock(hours, buffer, sizeof(buffer));
    return buffer;
}
}  // namespace

TEST_CASE("durations read as a raid timer") {
    CHECK(duration(0.0) == "0:00");
    CHECK(duration(9.0) == "0:09");
    CHECK(duration(65.0) == "1:05");
    CHECK(duration(600.0) == "10:00");
    CHECK(duration(2400.0) == "40:00");
    CHECK(duration(3600.0) == "1:00:00");
    CHECK(duration(3725.0) == "1:02:05");
    CHECK(duration(-5.0) == "0:00");
}

TEST_CASE("the in-game clock reads fractional hours, not minutes") {
    // The trailing number in a screenshot filename is a fraction of an hour:
    // 14.65 is 14:39, not 14:65. Getting this wrong would put the displayed
    // clock up to an hour off, plausibly enough that nobody notices.
    CHECK(clock(14.65) == "14:39");
    CHECK(clock(0.0) == "00:00");
    CHECK(clock(6.44) == "06:26");
    CHECK(clock(22.28) == "22:17");
    CHECK(clock(13.5) == "13:30");
}

TEST_CASE("the clock wraps rather than overflowing") {
    CHECK(clock(23.999) == "00:00");   // rounds up past midnight
    CHECK(clock(24.0) == "00:00");
    CHECK(clock(25.5) == "01:30");
    CHECK(clock(-1.0) == "23:00");
}
