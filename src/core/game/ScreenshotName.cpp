#include "core/game/ScreenshotName.h"

#include <charconv>
#include <cstddef>

namespace tarkov::game {
namespace {

/// A cursor over the filename. Every helper either advances it and returns
/// true, or leaves it untouched and returns false, so a parse is a short
/// sequence of guarded steps.
class Scanner {
public:
    explicit Scanner(std::string_view s) : s_(s) {}

    bool literal(std::string_view lit) {
        if (s_.substr(pos_, lit.size()) != lit) {
            return false;
        }
        pos_ += lit.size();
        return true;
    }

    /// Fixed-width unsigned integer, e.g. the two digits of an hour.
    bool fixedInt(std::size_t width, int& out) {
        if (pos_ + width > s_.size()) {
            return false;
        }
        for (std::size_t i = 0; i < width; ++i) {
            const char c = s_[pos_ + i];
            if (c < '0' || c > '9') {
                return false;
            }
        }
        const char* begin = s_.data() + pos_;
        if (std::from_chars(begin, begin + width, out).ec != std::errc{}) {
            return false;
        }
        pos_ += width;
        return true;
    }

    /// A signed decimal number.
    ///
    /// std::from_chars is used rather than std::stod because it does not
    /// consult the locale. The game always writes '.' as the decimal
    /// separator, but the user's machine is Russian-locale, where strtod-based
    /// parsing would expect ',' and silently truncate every coordinate.
    bool number(double& out) {
        const char* begin = s_.data() + pos_;
        const char* end = s_.data() + s_.size();
        const auto result = std::from_chars(begin, end, out);
        if (result.ec != std::errc{} || result.ptr == begin) {
            return false;
        }
        pos_ += static_cast<std::size_t>(result.ptr - begin);
        return true;
    }

    bool atEnd() const { return pos_ == s_.size(); }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
};

constexpr std::string_view kSeparator = ", ";

}  // namespace

std::optional<ScreenshotName> parseScreenshotName(std::string_view filename) {
    ScreenshotName out;
    Scanner scan(filename);

    // 2026-09-18[01-14]
    if (!scan.fixedInt(4, out.year) || !scan.literal("-") ||
        !scan.fixedInt(2, out.month) || !scan.literal("-") ||
        !scan.fixedInt(2, out.day) || !scan.literal("[") ||
        !scan.fixedInt(2, out.hour) || !scan.literal("-") ||
        !scan.fixedInt(2, out.minute) || !scan.literal("]")) {
        return std::nullopt;
    }

    // _616.67, 1.05, -103.50
    if (!scan.literal("_") || !scan.number(out.position.x) ||
        !scan.literal(kSeparator) || !scan.number(out.position.y) ||
        !scan.literal(kSeparator) || !scan.number(out.position.z)) {
        return std::nullopt;
    }

    // _0.04218, -0.71487, 0.04329, 0.69664
    if (!scan.literal("_") || !scan.number(out.orientation.x) ||
        !scan.literal(kSeparator) || !scan.number(out.orientation.y) ||
        !scan.literal(kSeparator) || !scan.number(out.orientation.z) ||
        !scan.literal(kSeparator) || !scan.number(out.orientation.w)) {
        return std::nullopt;
    }

    // _14.65 (0).png
    if (!scan.literal("_") || !scan.number(out.gameTimeHours) ||
        !scan.literal(" (") || !scan.fixedInt(1, out.sequence)) {
        return std::nullopt;
    }
    // The counter is usually one digit but nothing guarantees that.
    for (int extra = 0; !scan.literal(")"); ) {
        if (!scan.fixedInt(1, extra)) {
            return std::nullopt;
        }
        out.sequence = out.sequence * 10 + extra;
    }
    if (!scan.literal(".png") || !scan.atEnd()) {
        return std::nullopt;
    }

    return out;
}

}  // namespace tarkov::game
