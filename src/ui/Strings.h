#pragma once

#include <string>
#include <string_view>

namespace tarkov::ui {

enum class Lang { En, Ru };

/// Every string the player can see, by identifier.
///
/// Identifiers rather than English text as keys, so rewording the English
/// never silently orphans a translation. Russian is the default: the player
/// runs the game in Russian (see CLAUDE.md), and the quest and extract names
/// already arrive in Russian from tarkov.dev.
enum class Str {
    // Map view
    Ground,
    LayerN,              // %d
    NoMapImageHeadline,
    NoMapImageDetail,
    Optional,
    OneOfSeveralSpawns,
    OtherFloor,
    OpacityFormat,       // %.2f
    LayerQuests,
    LayerExtracts,
    LayerTransits,
    LayerSpawns,
    NoUpperFloors,
    FloorAuto,
    AutoSuffix,
    NoImageForFloor,
    Following,
    FreeView,
    RendererAbstract,
    RendererSatellite,
    FixAge,              // %s
    AgeSeconds,          // %d
    AgeMinutesSeconds,   // %d %d
    PressScreenshot,
    NoPositionPressScreenshot,
    ObjectivesCount,     // %d
    BossSpawn,
    PmcSpawn,
    TransitTo,           // %s
    WaitingForRaid,
    WaitingForRaidDetail,

    // Quest panel
    QuestsTitle,
    ActiveQuestsFiltered,  // %d
    NoProgressKnown,
    MarkAsDone,
    Assumed,
    AssumedTooltip,
    Undo,
    TakingIt,

    // HUD
    HudMap,
    HudState,
    HudElapsed,
    HudPosition,
    HudElevation,
    HudFacing,
    HudInGame,
    HudFixAge,
    HudFixes,
    HudRaid,
    HudUnrecognisedMap,
    HudPressToUpdate,
    HudNoPosition,
    HudPressKey,
    PhaseIdle,
    PhaseLoading,
    PhaseMatching,
    PhaseLoaded,
    PhaseInRaid,
    PhaseEnded,

    // Status messages
    StatusCouldNotParseKey,    // %s %s
    StatusReservedKey,         // %s %s
    StatusHotkeyBound,         // %s %s
    StatusHotkeyClaimed,       // %s
    StatusHotkeyFailed,        // %s
    HotkeyPurposeMap,
    HotkeyPurposeInteract,
    HotkeyPurposeHud,
    StatusCaptureHidden,
    StatusCaptureBlanked,
    StatusCaptureVisible,
    StatusCaptureFailed,
    StatusMapDataBroken,
    StatusForcedMap,           // %s
    StatusInjectedPosition,
    StatusInjectedPositionBad,
    StatusNoGameData,
    StatusStandalone,
    StatusInteractive,
    StatusClickThrough,
    StatusSwitchToBorderless,
    StatusGameNotFound,
    StatusNoLogFolder,
    StatusNoScreenshotFolder,
    StatusWatchingSession,     // %s
    StatusRaidOn,              // %s
    StatusQuestEventsImported, // %d %d
    StatusQuestsFromCache,     // %d
    StatusQuestsDownloadFailed,// %d
    StatusNoTranslations,      // %s
    StatusQuestsDidNotParse,
    StatusMapsUnavailable,
    StatusDataDownloaded,      // %d %d
    StatusDrawingDownloadFailed, // %s
    StatusDrawingDidNotParse,  // %s
    ErrorInterfaceInit,

    Count_
};

void setLanguage(Lang language);
Lang language();

/// Parses "ru" / "en". Anything else is Russian, the default.
Lang parseLanguage(std::string_view code);

const char* tr(Str id);
const char* tr(Str id, Lang language);

/// printf-style formatting of a translated string.
std::string trf(Str id, ...);

/// A map's display name from its tarkov.dev normalized name, e.g. "customs".
/// Unknown names are returned unchanged.
std::string mapDisplayName(std::string_view normalizedName);

/// A floor's display name from the English one maps.json uses, e.g.
/// "2nd Floor". Unknown names are returned unchanged.
std::string floorDisplayName(std::string_view englishName);

/// Eight-point compass abbreviation for a bearing in degrees.
const char* compassPoint(double degrees);

namespace detail {
/// The first table index whose entry is not the Str it is looked up as, or -1.
/// The table is indexed by enum value, so an entry out of order would silently
/// shift every translation after it; tests call this.
int misorderedStringIndex();
}  // namespace detail

}  // namespace tarkov::ui
