#include <doctest/doctest.h>

#include <map>

#include "core/data/MapFeatures.h"
#include "core/data/MapIds.h"
#include "fixtures.h"

using namespace tarkov::data;

namespace {

constexpr const char* kCustoms = "56f40101d2720b2a4d8b45d6";

const MapFeatures& customs() {
    static const auto all = parseMapFeatures(
        tarkov::test::readFile(tarkov::test::fixturePath("maps/customs.json")),
        TaskLocale::parse(tarkov::test::readFile(
            tarkov::test::fixturePath("maps/customs_ru.json"))));
    static const MapFeatures& one = all.at(kCustoms);
    return one;
}

}  // namespace

TEST_CASE("Customs' extracts parse with names in Russian") {
    const auto& features = customs();
    REQUIRE(features.extracts.size() == 27);

    std::map<ExtractFaction, int> factions;
    for (const auto& extract : features.extracts) {
        ++factions[extract.faction];
        CAPTURE(extract.name);
        CHECK_FALSE(extract.name.empty());
    }
    // Counts from the live data: 16 scav, 9 PMC, 2 shared.
    CHECK(factions[ExtractFaction::Scav] == 16);
    CHECK(factions[ExtractFaction::Pmc] == 9);
    CHECK(factions[ExtractFaction::Shared] == 2);

    bool foundTranslated = false;
    for (const auto& extract : features.extracts) {
        foundTranslated = foundTranslated ||
                          extract.name == "\xd0\x92\xd0\xbe\xd1\x80\xd0\xbe\xd1\x82\xd0\xb0 "
                                          "\xd1\x83 \xd1\x81\xd1\x82\xd0\xb0\xd1\x80\xd0\xbe\xd0\xb9 "
                                          "\xd0\xb7\xd0\xb0\xd0\xbf\xd1\x80\xd0\xb0\xd0\xb2\xd0\xba\xd0\xb8";
    }
    CHECK(foundTranslated);  // "Ворота у старой заправки"
}

TEST_CASE("an untranslated extract keeps its readable key rather than going blank") {
    const auto all = parseMapFeatures(
        tarkov::test::readFile(tarkov::test::fixturePath("maps/customs.json")),
        TaskLocale{});
    for (const auto& extract : all.at(kCustoms).extracts) {
        CAPTURE(extract.name);
        CHECK_FALSE(extract.name.empty());
    }
}

TEST_CASE("only player and boss spawns are notable") {
    const auto& features = customs();
    REQUIRE(features.spawns.size() == 40);

    const auto notable = notableSpawns(features);
    CHECK_FALSE(notable.empty());
    CHECK(notable.size() < features.spawns.size());
    for (const auto* spawn : notable) {
        CHECK((spawn->boss || (spawn->pmc && spawn->player)));
    }
}

TEST_CASE("transits point at maps we can resolve") {
    const auto& features = customs();
    REQUIRE(features.transits.size() == 4);
    const MapIdTable ids = MapIdTable::parse(kMapIdsJson);
    for (const auto& transit : features.transits) {
        CAPTURE(transit.targetMapId);
        CHECK(ids.drawnAs(transit.targetMapId).has_value());
    }
}

TEST_CASE("feature positions are in the same world space as everything else") {
    for (const auto& extract : customs().extracts) {
        CHECK(std::abs(extract.position.x) < 2000.0);
        CHECK(std::abs(extract.position.z) < 2000.0);
    }
}

TEST_CASE("malformed map data yields nothing rather than a crash") {
    for (const char* bad : {"", "{", "null", R"({"data":{"maps":[]}})"}) {
        CAPTURE(bad);
        CHECK(parseMapFeatures(bad, TaskLocale{}).empty());
    }
}

TEST_CASE("Customs' stashes parse, and other containers are left out") {
    const auto& features = customs();
    // The fixture holds Customs' 65 stashes from the data of 2026-10-21, and
    // three safes that must not come through.
    REQUIRE(features.caches.size() == 65);

    std::map<CacheKind, int> kinds;
    for (const auto& cache : features.caches) {
        ++kinds[cache.kind];
    }
    CHECK(kinds[CacheKind::BuriedBarrel] == 56);
    CHECK(kinds[CacheKind::Ground] == 9);

    for (const auto& cache : features.caches) {
        if (cache.kind == CacheKind::Ground) {
            // "Закопанный ящик"
            CHECK(cache.name == "\xd0\x97\xd0\xb0\xd0\xba\xd0\xbe\xd0\xbf\xd0\xb0"
                                "\xd0\xbd\xd0\xbd\xd1\x8b\xd0\xb9 "
                                "\xd1\x8f\xd1\x89\xd0\xb8\xd0\xba");
            break;
        }
    }
}
