#pragma once

#include <string_view>

namespace tarkov::data {

/// A pinned copy of tarkov.dev's maps.json, compiled into the binary.
///
/// Embedded rather than shipped alongside so the overlay stays a single
/// executable and cannot be half-updated. Refresh it deliberately by replacing
/// data/maps.json; see docs/research/map-projection.md.
extern const std::string_view kMapsJson;

}  // namespace tarkov::data
