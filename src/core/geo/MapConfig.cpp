#include "core/geo/MapConfig.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace tarkov::geo {
namespace {

using nlohmann::json;

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

double numberOr(const json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<double>() : fallback;
}

std::string stringOr(const json& j, const char* key) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

/// maps.json writes a corner as [x, z].
std::optional<Vec2> readCorner(const json& j) {
    if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number()) {
        return std::nullopt;
    }
    return Vec2{j[0].get<double>(), j[1].get<double>()};
}

void readLayers(const json& src, MapVariant& out) {
    const auto layersIt = src.find("layers");
    if (layersIt == src.end() || !layersIt->is_array()) {
        return;
    }
    for (const auto& layerJson : *layersIt) {
        MapLayer layer;
        layer.name = stringOr(layerJson, "name");
        layer.svgLayer = stringOr(layerJson, "svgLayer");
        layer.tilePath = stringOr(layerJson, "tilePath");
        const auto showIt = layerJson.find("show");
        layer.showByDefault = showIt != layerJson.end() && showIt->is_boolean() &&
                              showIt->get<bool>();

        const auto extentsIt = layerJson.find("extents");
        if (extentsIt != layerJson.end() && extentsIt->is_array()) {
            for (const auto& extentJson : *extentsIt) {
                LayerExtent extent;
                const auto heightIt = extentJson.find("height");
                if (heightIt != extentJson.end() && heightIt->is_array() &&
                    heightIt->size() >= 2) {
                    extent.heightMin = (*heightIt)[0].get<double>();
                    extent.heightMax = (*heightIt)[1].get<double>();
                }
                const auto boundsIt = extentJson.find("bounds");
                if (boundsIt != extentJson.end() && boundsIt->is_array()) {
                    for (const auto& regionJson : *boundsIt) {
                        if (!regionJson.is_array() || regionJson.size() < 2) {
                            continue;
                        }
                        const auto c1 = readCorner(regionJson[0]);
                        const auto c2 = readCorner(regionJson[1]);
                        if (!c1 || !c2) {
                            continue;
                        }
                        LayerRegion region;
                        region.corner1 = *c1;
                        region.corner2 = *c2;
                        if (regionJson.size() >= 3 && regionJson[2].is_string()) {
                            region.name = regionJson[2].get<std::string>();
                        }
                        extent.regions.push_back(std::move(region));
                    }
                }
                layer.extents.push_back(std::move(extent));
            }
        }
        out.layers.push_back(std::move(layer));
    }
}

}  // namespace

bool LayerRegion::contains(const Vec2& p) const {
    const auto [xLo, xHi] = std::minmax(corner1.x, corner2.x);
    const auto [yLo, yHi] = std::minmax(corner1.y, corner2.y);
    return p.x >= xLo && p.x <= xHi && p.y >= yLo && p.y <= yHi;
}

const MapVariant* MapDefinition::interactiveVariant() const {
    for (const auto& variant : variants) {
        if (variant.projection == "interactive") {
            return &variant;
        }
    }
    return variants.empty() ? nullptr : &variants.front();
}

std::vector<MapDefinition> parseMapsJson(std::string_view jsonText) {
    std::vector<MapDefinition> maps;
    const json root = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (!root.is_array()) {
        return maps;
    }

    for (const auto& mapJson : root) {
        MapDefinition def;
        def.normalizedName = stringOr(mapJson, "normalizedName");

        const auto variantsIt = mapJson.find("maps");
        if (variantsIt == mapJson.end() || !variantsIt->is_array()) {
            continue;
        }
        for (const auto& variantJson : *variantsIt) {
            MapVariant variant;
            variant.key = stringOr(variantJson, "key");
            variant.projection = stringOr(variantJson, "projection");
            variant.tilePath = stringOr(variantJson, "tilePath");
            variant.svgPath = stringOr(variantJson, "svgPath");
            variant.svgLayer = stringOr(variantJson, "svgLayer");
            variant.tileSize = static_cast<int>(numberOr(variantJson, "tileSize", 256));
            variant.minZoom = static_cast<int>(numberOr(variantJson, "minZoom", 0));
            variant.maxZoom = static_cast<int>(numberOr(variantJson, "maxZoom", 0));
            variant.coordinateRotation = numberOr(variantJson, "coordinateRotation", 0.0);

            const auto transformIt = variantJson.find("transform");
            if (transformIt != variantJson.end() && transformIt->is_array() &&
                transformIt->size() >= 4) {
                for (std::size_t i = 0; i < 4; ++i) {
                    variant.transform[i] = (*transformIt)[i].get<double>();
                }
            }

            const auto boundsIt = variantJson.find("bounds");
            if (boundsIt != variantJson.end() && boundsIt->is_array() &&
                boundsIt->size() >= 2) {
                const auto c1 = readCorner((*boundsIt)[0]);
                const auto c2 = readCorner((*boundsIt)[1]);
                if (c1 && c2) {
                    variant.bounds = {*c1, *c2};
                    variant.hasBounds = true;
                }
            }

            const auto svgBoundsIt = variantJson.find("svgBounds");
            if (svgBoundsIt != variantJson.end() && svgBoundsIt->is_array() &&
                svgBoundsIt->size() >= 2) {
                const auto c1 = readCorner((*svgBoundsIt)[0]);
                const auto c2 = readCorner((*svgBoundsIt)[1]);
                if (c1 && c2) {
                    variant.svgBounds = {*c1, *c2};
                    variant.hasSvgBounds = true;
                }
            }

            readLayers(variantJson, variant);
            def.variants.push_back(std::move(variant));
        }
        maps.push_back(std::move(def));
    }
    return maps;
}

const MapDefinition* findMap(const std::vector<MapDefinition>& maps,
                             std::string_view normalizedName) {
    for (const auto& map : maps) {
        if (iequals(map.normalizedName, normalizedName)) {
            return &map;
        }
    }
    return nullptr;
}

std::optional<std::string_view> mapIdToNormalizedName(std::string_view gameMapId) {
    struct Entry {
        std::string_view gameId;
        std::string_view normalized;
    };
    // Location ids, from the server's assignment and an offline raid's
    // transit line. Confirmed from this machine's own logs: all but
    // factory4_night, rezervbase's lowercase, terminal and icebreaker.
    //
    // Then scene ids, from "rcid:" in the scene preset line, where they differ
    // -- the only name an offline raid has until its transit line. Every one
    // seen on this machine from 2026-09-16 to 10-22. "Sandbox_SL" is the
    // first-run tutorial ("Sandbox_start"), which has no map here.
    static constexpr Entry kTable[] = {
        {"bigmap", "customs"},
        {"factory4_day", "factory"},
        {"factory4_night", "factory"},
        {"woods", "woods"},
        {"shoreline", "shoreline"},
        {"rezervbase", "reserve"},
        {"interchange", "interchange"},
        {"laboratory", "the-lab"},
        {"lighthouse", "lighthouse"},
        {"tarkovstreets", "streets-of-tarkov"},
        {"sandbox", "ground-zero"},
        {"sandbox_high", "ground-zero"},
        {"labyrinth", "the-labyrinth"},
        {"terminal", "terminal"},
        {"icebreaker", "icebreaker"},

        {"city", "streets-of-tarkov"},
        {"factory_day", "factory"},
        {"shopping_mall", "interchange"},
        {"rezerv_base", "reserve"},
    };
    for (const auto& entry : kTable) {
        if (iequals(entry.gameId, gameMapId)) {
            return entry.normalized;
        }
    }
    return std::nullopt;
}

}  // namespace tarkov::geo
