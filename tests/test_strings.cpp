#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ui/Strings.h"

using namespace tarkov::ui;

namespace {

/// The printf conversions in a format string, e.g. {"d", ".2f", "s"}.
std::vector<std::string> conversions(const char* format) {
    std::vector<std::string> out;
    for (const char* p = format; *p != '\0'; ++p) {
        if (*p != '%') {
            continue;
        }
        ++p;
        if (*p == '%') {
            continue;
        }
        std::string spec;
        while (*p != '\0' && std::string("diouxXeEfgGcsp").find(*p) == std::string::npos) {
            spec.push_back(*p++);
        }
        if (*p == '\0') {
            break;
        }
        spec.push_back(*p);
        out.push_back(spec);
    }
    return out;
}

struct LanguageGuard {
    Lang saved = language();
    ~LanguageGuard() { setLanguage(saved); }
};

}  // namespace

TEST_CASE("the string table is in enum order") {
    // tr() indexes by enum value. One entry out of place would shift every
    // translation after it by one -- every label wrong, nothing failing.
    CHECK(detail::misorderedStringIndex() == -1);
}

TEST_CASE("every string exists in both languages") {
    for (int i = 0; i < static_cast<int>(Str::Count_); ++i) {
        const auto id = static_cast<Str>(i);
        CAPTURE(i);
        CHECK(std::string(tr(id, Lang::En)).size() > 0);
        CHECK(std::string(tr(id, Lang::Ru)).size() > 0);
    }
}

TEST_CASE("both languages take the same printf arguments") {
    // A translation with %s where the English has %d is not a typo; it is a
    // crash the first time that message is shown.
    for (int i = 0; i < static_cast<int>(Str::Count_); ++i) {
        const auto id = static_cast<Str>(i);
        CAPTURE(i);
        CAPTURE(tr(id, Lang::En));
        CAPTURE(tr(id, Lang::Ru));
        CHECK(conversions(tr(id, Lang::En)) == conversions(tr(id, Lang::Ru)));
    }
}

TEST_CASE("Russian is the default and formatting works in it") {
    LanguageGuard guard;
    CHECK(parseLanguage("") == Lang::Ru);
    CHECK(parseLanguage("ru") == Lang::Ru);
    CHECK(parseLanguage("en") == Lang::En);
    CHECK(parseLanguage("xx") == Lang::Ru);

    setLanguage(Lang::Ru);
    CHECK(trf(Str::ObjectivesCount, 38) ==
          "\xd1\x86\xd0\xb5\xd0\xbb\xd0\xb5\xd0\xb9: 38");  // "целей: 38"
    setLanguage(Lang::En);
    CHECK(trf(Str::ObjectivesCount, 38) == "38 objectives");
}

TEST_CASE("maps and floors have Russian names") {
    LanguageGuard guard;
    setLanguage(Lang::Ru);
    CHECK(mapDisplayName("customs") == "\xd0\xa2\xd0\xb0\xd0\xbc\xd0\xbe\xd0\xb6\xd0\xbd\xd1\x8f");
    CHECK(floorDisplayName("2nd Floor") == "2-\xd0\xb9 \xd1\x8d\xd1\x82\xd0\xb0\xd0\xb6");
    // Anything the table does not know passes through rather than vanishing.
    CHECK(mapDisplayName("brand-new-map") == "brand-new-map");
    CHECK(floorDisplayName("Mezzanine") == "Mezzanine");

    setLanguage(Lang::En);
    CHECK(mapDisplayName("customs") == "Customs");
    CHECK(floorDisplayName("2nd Floor") == "2nd Floor");
}

TEST_CASE("the compass follows the language") {
    LanguageGuard guard;
    setLanguage(Lang::En);
    CHECK(std::string(compassPoint(0.0)) == "N");
    CHECK(std::string(compassPoint(270.0)) == "W");
    setLanguage(Lang::Ru);
    CHECK(std::string(compassPoint(0.0)) == "\xd0\xa1");      // С
    CHECK(std::string(compassPoint(270.0)) == "\xd0\x97");    // З
}
