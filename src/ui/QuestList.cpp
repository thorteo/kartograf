#include "ui/QuestList.h"

#include <imgui.h>

#include "ui/Strings.h"

#include <algorithm>
#include <cstdio>

namespace tarkov::ui {
namespace {

constexpr ImVec4 kActive{0.96f, 0.82f, 0.36f, 1.0f};
constexpr ImVec4 kDone{0.52f, 0.62f, 0.46f, 1.0f};
constexpr ImVec4 kMuted{0.55f, 0.57f, 0.52f, 1.0f};

int rank(data::QuestState state) {
    switch (state) {
        case data::QuestState::Active: return 0;
        case data::QuestState::Unknown: return 1;
        case data::QuestState::Failed: return 2;
        case data::QuestState::Completed: return 3;
    }
    return 4;
}

}  // namespace

bool drawQuestList(const std::vector<QuestListEntry>& entries, bool filteredToActive,
                   int activeTotal, const QuestListActions& actions) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = 380.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - 16.0f,
                                   viewport->WorkPos.y + 16.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, viewport->WorkSize.y * 0.6f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.90f);

    bool changed = false;
    char title[96];
    std::snprintf(title, sizeof(title), "%s###quests", tr(Str::QuestsTitle));
    if (!ImGui::Begin(title, nullptr,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return false;
    }

    if (filteredToActive) {
        ImGui::TextColored(kMuted, "%s", trf(Str::ActiveQuestsFiltered, activeTotal).c_str());
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kActive, "%s", tr(Str::NoProgressKnown));
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    std::vector<const QuestListEntry*> sorted;
    sorted.reserve(entries.size());
    for (const auto& entry : entries) {
        sorted.push_back(&entry);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const QuestListEntry* a, const QuestListEntry* b) {
                         return rank(a->state) < rank(b->state);
                     });

    ImGui::BeginChild("list");
    for (const QuestListEntry* entry : sorted) {
        ImGui::PushID(entry->id.c_str());

        bool done = entry->state == data::QuestState::Completed;
        if (ImGui::Checkbox("##done", &done) && actions.setState) {
            actions.setState(entry->id,
                             done ? data::QuestState::Completed : data::QuestState::Active);
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(Str::MarkAsDone));
        }
        ImGui::SameLine();

        const ImVec4 colour = entry->state == data::QuestState::Active    ? kActive
                              : entry->state == data::QuestState::Completed ? kDone
                                                                            : kMuted;
        ImGui::TextColored(colour, "%s", entry->name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "(%d)", entry->objectivesHere);

        if (entry->inferred) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "%s", tr(Str::Assumed));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr(Str::AssumedTooltip));
            }
        }
        if (entry->manual) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(Str::Undo)) && actions.clearCorrection) {
                actions.clearCorrection(entry->id);
                changed = true;
            }
        }
        if (entry->state == data::QuestState::Unknown) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(Str::TakingIt)) && actions.setState) {
                actions.setState(entry->id, data::QuestState::Active);
                changed = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    return changed;
}

}  // namespace tarkov::ui
