#include "ui/SettingsView.h"

#include <imgui.h>

#include <cstdio>

#include "ui/MapView.h"
#include "ui/Strings.h"

namespace tarkov::ui {
namespace {

constexpr ImVec4 kGood{0.62f, 0.82f, 0.45f, 1.0f};
constexpr ImVec4 kBad{0.92f, 0.45f, 0.40f, 1.0f};
constexpr ImVec4 kMuted{0.58f, 0.60f, 0.55f, 1.0f};
constexpr ImVec4 kAccent{0.96f, 0.82f, 0.36f, 1.0f};

void statusRow(const char* label, bool ok, const char* okText, const char* badText) {
    ImGui::TextColored(kMuted, "%s", label);
    ImGui::SameLine(210.0f);
    ImGui::TextColored(ok ? kGood : kBad, "%s", ok ? okText : badText);
}

}  // namespace

input::Hotkey& hotkeyFor(settings::Settings& s, HotkeySlot slot) {
    switch (slot) {
        case HotkeySlot::Map: return s.mapKey;
        case HotkeySlot::Interact: return s.interactKey;
        case HotkeySlot::Hud: return s.hudKey;
    }
    return s.mapKey;
}

const input::Hotkey& hotkeyFor(const settings::Settings& s, HotkeySlot slot) {
    return hotkeyFor(const_cast<settings::Settings&>(s), slot);
}

std::string validateCapturedKey(const settings::Settings& s, HotkeySlot slot,
                                const input::Hotkey& key) {
    if (input::isReservedCombination(key)) {
        return tr(Str::KeyReserved);
    }
    for (int i = 0; i < kHotkeySlotCount; ++i) {
        const auto other = static_cast<HotkeySlot>(i);
        if (other != slot && hotkeyFor(s, other) == key) {
            return tr(Str::KeyDuplicate);
        }
    }
    return {};
}

SettingsViewResult drawSettings(settings::Settings& s, SettingsViewState& state,
                                const SettingsStatus& status) {
    SettingsViewResult result;

    // Apply a key captured since the last frame.
    if (state.capturing && state.captured) {
        const HotkeySlot slot = *state.capturing;
        const input::Hotkey key = *state.captured;
        state.capturing.reset();
        state.captured.reset();
        auto& error = state.hotkeyErrors[static_cast<int>(slot)];
        if (key.valid()) {
            error = validateCapturedKey(s, slot, key);
            if (error.empty()) {
                hotkeyFor(s, slot) = key;
                result.changed = true;
            }
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // --- Hotkeys -------------------------------------------------------------
    ImGui::SeparatorText(tr(Str::SectionHotkeys));
    const struct {
        HotkeySlot slot;
        Str label;
    } rows[] = {{HotkeySlot::Map, Str::KeyMap},
                {HotkeySlot::Interact, Str::KeyInteract},
                {HotkeySlot::Hud, Str::KeyHud}};
    for (const auto& row : rows) {
        const int index = static_cast<int>(row.slot);
        ImGui::PushID(index);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(row.label));
        ImGui::SameLine(250.0f);
        const bool capturingThis = state.capturing && *state.capturing == row.slot;
        if (capturingThis) {
            ImGui::TextColored(kAccent, "%s", tr(Str::PressAKey));
        } else {
            const std::string text = input::formatHotkey(hotkeyFor(s, row.slot));
            ImGui::TextColored(kAccent, "%s", text.empty() ? "-" : text.c_str());
            ImGui::SameLine(380.0f);
            if (ImGui::Button(tr(Str::Change))) {
                state.capturing = row.slot;
                state.captured.reset();
                state.hotkeyErrors[index].clear();
            }
        }
        if (!state.hotkeyErrors[index].empty()) {
            ImGui::TextColored(kBad, "%s", state.hotkeyErrors[index].c_str());
        }
        ImGui::PopID();
    }

    // --- Map -----------------------------------------------------------------
    ImGui::SeparatorText(tr(Str::SectionMap));
    ImGui::SetNextItemWidth(260.0f);
    result.changed |= ImGui::SliderFloat(tr(Str::Opacity), &s.mapOpacity, 0.15f, 1.0f, "%.2f");

    ImGui::TextUnformatted(tr(Str::MapStyle));
    ImGui::SameLine(250.0f);
    if (ImGui::RadioButton(tr(Str::MapStyleAbstract), s.preferAbstractMap)) {
        result.changed |= !s.preferAbstractMap;
        s.preferAbstractMap = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr(Str::MapStyleSatellite), !s.preferAbstractMap)) {
        result.changed |= s.preferAbstractMap;
        s.preferAbstractMap = false;
    }
    ImGui::TextColored(kMuted, "%s", tr(Str::MapStyleNote));

    ImGui::TextUnformatted(tr(Str::ShowOnMap));
    ImGui::SameLine(250.0f);
    result.changed |= ImGui::Checkbox(tr(Str::LayerQuests), &s.showQuests);
    ImGui::SameLine();
    result.changed |= ImGui::Checkbox(tr(Str::LayerExtracts), &s.showExtracts);
    ImGui::SameLine();
    result.changed |= ImGui::Checkbox(tr(Str::LayerTransits), &s.showTransits);
    ImGui::SameLine();
    result.changed |= ImGui::Checkbox(tr(Str::LayerSpawns), &s.showSpawns);
    result.changed |= ImGui::Checkbox(tr(Str::DimOtherFloors), &s.dimOtherFloors);

    // --- General -------------------------------------------------------------
    ImGui::SeparatorText(tr(Str::SectionGeneral));
    ImGui::TextUnformatted(tr(Str::Language));
    ImGui::SameLine(250.0f);
    if (ImGui::RadioButton("Русский", s.language == "ru")) {
        result.changed |= s.language != "ru";
        s.language = "ru";
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("English", s.language == "en")) {
        result.changed |= s.language != "en";
        s.language = "en";
    }
    result.changed |= ImGui::Checkbox(tr(Str::ExcludeFromCapture), &s.excludeFromCapture);
    result.changed |= ImGui::Checkbox(tr(Str::ShowHud), &s.showHud);
    result.changed |= ImGui::Checkbox(tr(Str::ShowOnStart), &s.showSettingsOnStart);

    // --- Status --------------------------------------------------------------
    ImGui::SeparatorText(tr(Str::SectionStatus));
    statusRow(tr(Str::StatusGame), status.gameRunning, tr(Str::StatusGameRunning),
              tr(Str::StatusGameNotRunning));
    statusRow(tr(Str::StatusLogs), status.logFolderFound, tr(Str::StatusFound),
              tr(Str::StatusNotFound));
    statusRow(tr(Str::StatusScreenshots), status.screenshotFolderFound,
              tr(Str::StatusFound), tr(Str::StatusNotFound));
    if (!status.screenshotFolderFound) {
        ImGui::TextColored(kBad, "%s", tr(Str::StatusScreenshotHelp));
    }

    ImGui::TextColored(kMuted, "%s", tr(Str::StatusQuestData));
    ImGui::SameLine(210.0f);
    if (status.questsLoaded > 0) {
        ImGui::TextColored(kGood, "%s",
                           trf(Str::StatusQuestDataLoaded, status.questsLoaded,
                               status.questsActive)
                               .c_str());
    } else {
        ImGui::TextColored(status.questDataLoading ? kAccent : kBad, "%s",
                           status.questDataLoading ? tr(Str::StatusQuestDataLoading)
                                                   : tr(Str::StatusNotFound));
    }

    ImGui::TextColored(kMuted, "%s", tr(Str::StatusPosition));
    ImGui::SameLine(210.0f);
    if (status.positionAgeSeconds < 0.0) {
        ImGui::TextColored(kMuted, "%s", tr(Str::StatusNoPositionYet));
    } else {
        ImGui::TextColored(kGood, "%s",
                           trf(Str::StatusPositionAge,
                               formatFixAge(status.positionAgeSeconds).c_str())
                               .c_str());
    }

    // --- Buttons -------------------------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button(tr(Str::Done), ImVec2(140.0f, 0.0f))) {
        result.done = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::Quit), ImVec2(140.0f, 0.0f))) {
        result.quit = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "%s", tr(Str::DoneHint));

    ImGui::End();
    return result;
}

}  // namespace tarkov::ui
