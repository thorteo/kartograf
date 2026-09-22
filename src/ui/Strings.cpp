#include "ui/Strings.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <utility>

namespace tarkov::ui {
namespace {

struct Entry {
    Str id;
    const char* en;
    const char* ru;
};

// Kept in enum order; a test checks every id is present exactly once and that
// both languages carry the same printf conversions.
constexpr Entry kTable[] = {
    {Str::Ground, "Ground", "Земля"},
    {Str::LayerN, "Layer %d", "Уровень %d"},
    {Str::NoMapImageHeadline, "No map image available for this location yet.",
     "Для этой локации пока нет изображения карты."},
    {Str::NoMapImageDetail,
     "Position tracking still works; open the detail panel for coordinates.",
     "Отслеживание позиции работает; координаты — в подробной панели."},
    {Str::Optional, "optional", "необязательно"},
    {Str::OneOfSeveralSpawns, "one of several possible spawns",
     "одна из нескольких возможных точек"},
    {Str::OtherFloor, "other floor", "другой этаж"},
    {Str::NeedBring, "Bring: %s", "С собой: %s"},
    {Str::NeedBringAnyOf, "Bring any of: %s", "С собой, любой из: %s"},
    {Str::NeedKey, "Key: %s", "Ключ: %s"},
    {Str::NeedKeyAnyOf, "Key, any of: %s", "Ключ, любой из: %s"},
    {Str::NeedHandIn, "Hand over: %s", "Сдать: %s"},
    {Str::NeedHandInAnyOf, "Hand over %d of any: %s", "Сдать %d шт., любые из: %s"},
    {Str::NeedCount, "%s ×%d", "%s ×%d"},
    {Str::NeedFoundInRaid, "found in raid", "найдено в рейде"},
    {Str::NeedAndMore, "and %d more", "и ещё %d"},
    {Str::OpacityFormat, "opacity %.2f", "прозрачность %.2f"},
    {Str::LayerQuests, "quests", "квесты"},
    {Str::LayerExtracts, "extracts", "выходы"},
    {Str::LayerTransits, "transits", "переходы"},
    {Str::LayerSpawns, "spawns", "спавны"},
    {Str::NoUpperFloors, "no upper floors", "без верхних этажей"},
    {Str::FloorAuto, "auto", "авто"},
    {Str::AutoSuffix, " (auto)", " (авто)"},
    {Str::NoImageForFloor, " - no image for this floor", " — нет изображения этажа"},
    {Str::Following, "following", "за игроком"},
    {Str::FreeView, "free", "свободно"},
    {Str::RendererAbstract, "abstract", "схема"},
    {Str::RendererSatellite, "satellite", "спутник"},
    {Str::FixAge, "fix %s", "позиция %s назад"},
    {Str::AgeSeconds, "%ds", "%d с"},
    {Str::AgeMinutesSeconds, "%dm%ds", "%d мин %d с"},
    {Str::PressScreenshot, " - press screenshot", " — сделай скриншот"},
    {Str::NoPositionPressScreenshot, "no position - press screenshot",
     "позиции нет — сделай скриншот"},
    {Str::ObjectivesCount, "%d objectives", "целей: %d"},
    {Str::BossSpawn, "boss spawn", "спавн босса"},
    {Str::PmcSpawn, "PMC spawn", "спавн ЧВК"},
    {Str::TransitTo, "transit to %s", "переход: %s"},
    {Str::WaitingForRaid, "Waiting for a raid to start.", "Ждём начала рейда."},
    {Str::WaitingForRaidDetail,
     "The map opens by itself once the game says which one you are on.",
     "Карта откроется сама, как только игра сообщит локацию."},

    {Str::QuestsTitle, "Quests", "Квесты"},
    {Str::ActiveQuestsFiltered, "%d active quests. Markers show only those.",
     "Активных квестов: %d. На карте только они."},
    {Str::NoProgressKnown,
     "No quest progress known yet, so every quest on this map is shown. Progress "
     "fills in by itself as the game reports quests; mark what you already have below.",
     "Прогресс квестов пока неизвестен, поэтому показаны все квесты локации. Он "
     "заполнится сам по мере того, как игра сообщает о квестах; отметь уже "
     "взятые ниже."},
    {Str::MarkAsDone, "Mark as done", "Отметить выполненным"},
    {Str::Assumed, "assumed", "предположительно"},
    {Str::AssumedTooltip,
     "A later quest you took requires this one, so it must be done.",
     "Этого требует взятый позже квест, значит этот выполнен."},
    {Str::Undo, "undo", "отменить"},
    {Str::TakingIt, "taking it", "взят"},

    {Str::HudMap, "map", "карта"},
    {Str::HudState, "state", "статус"},
    {Str::HudElapsed, "elapsed", "прошло"},
    {Str::HudPosition, "pos", "поз."},
    {Str::HudElevation, "elev", "выс."},
    {Str::HudFacing, "facing", "взгляд"},
    {Str::HudInGame, "in-game", "в игре"},
    {Str::HudFixAge, "fix age", "позиция"},
    {Str::HudFixes, "fixes", "отметок"},
    {Str::HudRaid, "raid", "рейд"},
    {Str::HudUnrecognisedMap, "unrecognised map id", "неизвестная локация"},
    {Str::HudPressToUpdate, "press the screenshot key to update",
     "нажми клавишу скриншота, чтобы обновить"},
    {Str::HudNoPosition, "no position yet", "позиции пока нет"},
    {Str::HudPressKey, "press the in-game screenshot key",
     "нажми в игре клавишу скриншота"},
    {Str::PhaseIdle, "idle", "вне рейда"},
    {Str::PhaseLoading, "loading", "загрузка"},
    {Str::PhaseMatching, "matching", "поиск сервера"},
    {Str::PhaseLoaded, "loaded", "локация загружена"},
    {Str::PhaseInRaid, "in raid", "в рейде"},
    {Str::PhaseEnded, "ended", "рейд окончен"},

    {Str::StatusCouldNotParseKey, "could not parse '%s', using %s",
     "не удалось разобрать «%s», используется %s"},
    {Str::StatusReservedKey, "'%s' is reserved, using %s",
     "«%s» занята системой, используется %s"},
    {Str::StatusHotkeyBound, "%s: %s", "%s: %s"},
    {Str::StatusHotkeyClaimed, "%s is already claimed by another application",
     "%s уже занята другим приложением"},
    {Str::StatusHotkeyFailed, "could not bind %s", "не удалось назначить %s"},
    {Str::HotkeyPurposeMap, "map", "карта"},
    {Str::HotkeyPurposeInteract, "interact", "управление"},
    {Str::HotkeyPurposeHud, "detail panel", "подробная панель"},
    {Str::StatusCaptureHidden, "hidden from screen capture",
     "скрыт от записи экрана"},
    {Str::StatusCaptureBlanked, "blanked in screen capture (Windows 10 2004+ hides it)",
     "затемнён в записи экрана (Windows 10 2004+ скрывает полностью)"},
    {Str::StatusCaptureVisible, "visible to screen capture", "виден в записи экрана"},
    {Str::StatusCaptureFailed, "could not exclude from screen capture",
     "не удалось скрыть от записи экрана"},
    {Str::StatusMapDataBroken, "embedded map data failed to parse; the map is disabled",
     "встроенные данные карт повреждены; карта отключена"},
    {Str::StatusForcedMap, "forced map: %s", "выбрана карта: %s"},
    {Str::StatusInjectedPosition, "position taken from a screenshot name",
     "позиция взята из имени скриншота"},
    {Str::StatusInjectedPositionBad, "could not read the screenshot name given",
     "не удалось разобрать имя скриншота"},
    {Str::StatusNoGameData, "running without game data", "работаю без данных игры"},
    {Str::StatusStandalone, "preview mode, not attached to the game",
     "режим просмотра, игра не подключена"},
    {Str::StatusInteractive, "interactive: clicks go to the overlay",
     "управление: клики идут в оверлей"},
    {Str::StatusClickThrough, "click-through", "клики проходят в игру"},
    {Str::StatusSwitchToBorderless, "if nothing shows, switch the game to borderless",
     "если ничего не видно, переключи игру в оконный режим без рамки"},
    {Str::StatusRestartedAfterDeviceLoss,
     "the graphics driver reset; the overlay restarted itself",
     "видеодрайвер перезапустился; оверлей перезапущен"},
    {Str::StatusGameNotFound, "could not find the game's folders; is it installed?",
     "не найдены папки игры; она установлена?"},
    {Str::StatusNoLogFolder, "no log folder found; map detection will not work",
     "папка логов не найдена; карта не определится сама"},
    {Str::StatusNoScreenshotFolder,
     "no screenshots folder found; position tracking will not work",
     "папка скриншотов не найдена; позиция отслеживаться не будет"},
    {Str::StatusWatchingSession, "watching log session %s", "читаю сессию логов %s"},
    {Str::StatusRaidOn, "raid on %s", "рейд: %s"},
    {Str::StatusQuestEventsImported, "quests: %d events read from %d log sessions",
     "квесты: %d событий из %d сессий логов"},
    {Str::StatusQuestsFromCache, "quests: %d loaded from cache",
     "квесты: %d загружено из кэша"},
    {Str::StatusQuestsDownloadFailed, "quests: download failed (HTTP %d)",
     "квесты: не удалось скачать (HTTP %d)"},
    {Str::StatusNoTranslations, "quests: no '%s' translations, using plain names",
     "квесты: нет перевода «%s», названия без перевода"},
    {Str::StatusQuestsDidNotParse, "quests: downloaded data did not parse",
     "квесты: скачанные данные не читаются"},
    {Str::StatusMapsUnavailable, "maps: extract and spawn data could not be downloaded",
     "карты: не удалось скачать выходы и спавны"},
    {Str::StatusDataDownloaded, "quests: %d and %d maps downloaded",
     "квесты: %d, карты: %d — скачано"},
    {Str::StatusDrawingDownloadFailed, "map: could not download the drawing for %s",
     "карта: не удалось скачать схему для %s"},
    {Str::StatusDrawingDidNotParse, "map: the drawing for %s did not parse",
     "карта: схема для %s не читается"},
    {Str::ErrorInterfaceInit, "Could not initialise the interface layer.",
     "Не удалось запустить интерфейс."},

    {Str::SettingsTitle, "tarkov-help settings", "Настройки tarkov-help"},
    {Str::SectionHotkeys, "Hotkeys", "Горячие клавиши"},
    {Str::SectionMap, "Map", "Карта"},
    {Str::SectionGeneral, "General", "Общее"},
    {Str::SectionStatus, "Status", "Состояние"},
    {Str::KeyMap, "Open the map", "Открыть карту"},
    {Str::KeyInteract, "Make the overlay clickable", "Управление оверлеем мышью"},
    {Str::KeyHud, "Detail panel", "Подробная панель"},
    {Str::Change, "Change", "Изменить"},
    {Str::PressAKey, "Press a key... (Esc cancels)", "Нажми клавишу... (Esc — отмена)"},
    {Str::KeyReserved, "This combination is reserved by Windows or needed in game", "Эта комбинация занята Windows или нужна в игре"},
    {Str::KeyDuplicate, "Already used for another action", "Уже используется для другого действия"},
    {Str::KeyClaimed, "Another program already uses this combination", "Эту комбинацию уже использует другая программа"},
    {Str::Opacity, "Map opacity", "Прозрачность карты"},
    {Str::MapStyle, "Map style", "Вид карты"},
    {Str::MapStyleAbstract, "Schematic", "Схема"},
    {Str::MapStyleSatellite, "Satellite", "Спутник"},
    {Str::MapStyleNote, "Maps without a schematic always use satellite, and vice versa.", "Где нет схемы, показывается спутник, и наоборот."},
    {Str::ShowOnMap, "Show on the map", "Показывать на карте"},
    {Str::DimOtherFloors, "Fade objectives on other floors", "Приглушать цели на других этажах"},
    {Str::Language, "Language", "Язык"},
    {Str::LanguageRussian, "Русский", "Русский"},
    {Str::LanguageEnglish, "English", "English"},
    {Str::ExcludeFromCapture, "Hide the overlay from screen recording and streams", "Скрывать оверлей от записи экрана и стримов"},
    {Str::ShowHud, "Show the detail panel", "Показывать подробную панель"},
    {Str::ShowOnStart, "Show this window at start", "Показывать это окно при запуске"},
    {Str::StatusGame, "Game", "Игра"},
    {Str::StatusGameRunning, "running", "запущена"},
    {Str::StatusGameNotRunning, "not running", "не запущена"},
    {Str::StatusLogs, "Log folder", "Папка логов"},
    {Str::StatusScreenshots, "Screenshots folder", "Папка скриншотов"},
    {Str::StatusFound, "found", "найдена"},
    {Str::StatusNotFound, "not found", "не найдена"},
    {Str::StatusQuestData, "Quest data", "Данные квестов"},
    {Str::StatusQuestDataLoaded, "%d quests, %d active", "%d квестов, активных %d"},
    {Str::StatusQuestDataLoading, "downloading...", "загружаются..."},
    {Str::StatusPosition, "Last position", "Последняя позиция"},
    {Str::StatusNoPositionYet, "none yet: press the screenshot key in a raid", "пока нет: нажми клавишу скриншота в рейде"},
    {Str::StatusPositionAge, "%s ago", "%s назад"},
    {Str::StatusScreenshotHelp, "Without the screenshots folder the map cannot show where you are.", "Без папки скриншотов карта не сможет показать, где ты."},
    {Str::Done, "Done", "Готово"},
    {Str::Quit, "Quit", "Выход"},
    {Str::DoneHint, "The overlay keeps running in the system tray.", "Оверлей продолжит работать в трее."},
    {Str::TraySettings, "Settings", "Настройки"},
    {Str::TrayQuit, "Quit", "Выход"},
    {Str::TrayRunning, "tarkov-help is running; %s opens the map", "tarkov-help работает; %s открывает карту"},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<std::size_t>(Str::Count_),
              "every Str needs exactly one table entry");

std::atomic<Lang> g_language{Lang::Ru};

}  // namespace

void setLanguage(Lang language) { g_language.store(language, std::memory_order_relaxed); }
Lang language() { return g_language.load(std::memory_order_relaxed); }

Lang parseLanguage(std::string_view code) {
    return code == "en" ? Lang::En : Lang::Ru;
}

const char* tr(Str id, Lang lang) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= sizeof(kTable) / sizeof(kTable[0])) {
        return "";
    }
    const Entry& entry = kTable[index];
    return lang == Lang::Ru ? entry.ru : entry.en;
}

const char* tr(Str id) { return tr(id, language()); }

std::string trf(Str id, ...) {
    va_list args;
    va_start(args, id);
    va_list copy;
    va_copy(copy, args);
    const char* format = tr(id);
    const int size = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string out;
    if (size > 0) {
        out.resize(static_cast<std::size_t>(size));
        std::vsnprintf(out.data(), out.size() + 1, format, args);
    }
    va_end(args);
    return out;
}

std::string mapDisplayName(std::string_view normalized) {
    // The names the game itself uses in its Russian localisation.
    static constexpr std::pair<std::string_view, std::pair<const char*, const char*>> kMaps[] = {
        {"customs", {"Customs", "Таможня"}},
        {"factory", {"Factory", "Завод"}},
        {"woods", {"Woods", "Лес"}},
        {"shoreline", {"Shoreline", "Берег"}},
        {"interchange", {"Interchange", "Развязка"}},
        {"reserve", {"Reserve", "Резерв"}},
        {"the-lab", {"The Lab", "Лаборатория"}},
        {"lighthouse", {"Lighthouse", "Маяк"}},
        {"streets-of-tarkov", {"Streets of Tarkov", "Улицы Таркова"}},
        {"ground-zero", {"Ground Zero", "Эпицентр"}},
        {"the-labyrinth", {"The Labyrinth", "Лабиринт"}},
        {"terminal", {"Terminal", "Терминал"}},
        {"icebreaker", {"Icebreaker", "Ледокол"}},
    };
    for (const auto& [key, names] : kMaps) {
        if (key == normalized) {
            return language() == Lang::Ru ? names.second : names.first;
        }
    }
    return std::string(normalized);
}

std::string floorDisplayName(std::string_view english) {
    if (language() == Lang::En) {
        return std::string(english);
    }
    static constexpr std::pair<std::string_view, const char*> kFloors[] = {
        {"Ground", "Земля"},
        {"Ground Floor", "1-й этаж"},
        {"First Floor", "1-й этаж"},
        {"1st Floor", "1-й этаж"},
        {"2nd Floor", "2-й этаж"},
        {"3rd Floor", "3-й этаж"},
        {"4th Floor", "4-й этаж"},
        {"5th Floor", "5-й этаж"},
        {"Underground", "Подземный уровень"},
        {"Basement", "Подвал"},
        {"Bunkers", "Бункеры"},
        {"Tunnels", "Тоннели"},
        {"Roof", "Крыша"},
    };
    for (const auto& [key, name] : kFloors) {
        if (key == english) {
            return name;
        }
    }
    return std::string(english);
}

namespace detail {
int misorderedStringIndex() {
    for (std::size_t i = 0; i < sizeof(kTable) / sizeof(kTable[0]); ++i) {
        if (static_cast<std::size_t>(kTable[i].id) != i) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
}  // namespace detail

const char* compassPoint(double degrees) {
    static const char* kEn[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    static const char* kRu[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
    const int index = static_cast<int>(std::lround(degrees / 45.0)) & 7;
    return language() == Lang::Ru ? kRu[index] : kEn[index];
}

}  // namespace tarkov::ui
