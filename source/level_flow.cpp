// level_flow.cpp - Mission objective evaluation, flow state, and extraction logic implementation
#include "level_flow.h"

namespace igi {

LevelFlow::LevelFlow() {
    InitializeMission(1);
}

void LevelFlow::InitializeMission(uint32_t mission_number) {
    mission_number_ = mission_number;
    objectives_.clear();
    status_ = MissionStatus::InProgress;

    switch (mission_number) {
        case 1:
            AddObjective(1, "Infiltrate the trainyard and download train schedule", true);
            AddObjective(2, "Get to the train depot and rendezvous with Anya", false);
            break;
        case 2:
            AddObjective(1, "Infiltrate the SAM base and disable the SAM sites", true);
            AddObjective(2, "Locate Josef Priboi and extract via helicopter", false);
            break;
        case 3:
            AddObjective(1, "Infiltrate the military airbase without raising alarms", true);
            AddObjective(2, "Locate and steal the MiG-29 fighter jet", false);
            break;
        case 4:
            AddObjective(1, "Survive the plane crash and evade pursuit patrols", true);
            AddObjective(2, "Reach the hilltop extraction helicopter", false);
            break;
        case 5:
            AddObjective(1, "Infiltrate the radar base and sabotage the dish", true);
            AddObjective(2, "Destroy communication arrays and reach the exit", false);
            break;
        case 6:
            AddObjective(1, "Locate Josef Priboi in the high-security bunker", true);
            AddObjective(2, "Capture Josef Priboi alive and escort to extraction", false);
            break;
        case 7:
            AddObjective(1, "Cross the heavily fortified border zone", true);
            AddObjective(2, "Avoid searchlights and border patrol towers", false);
            break;
        case 8:
            AddObjective(1, "Infiltrate the weapons facility and recover gear", true);
            AddObjective(2, "Sabotage heavy armory and escape undetected", false);
            break;
        case 9:
            AddObjective(1, "Locate the nuclear missile transport train", true);
            AddObjective(2, "Disable missile guidance computers on the train", false);
            break;
        case 10:
            AddObjective(1, "Defend Josef Priboi from enemy assault waves", true);
            AddObjective(2, "Eliminate all incoming enemy soldiers and snipers", false);
            break;
        case 11:
            AddObjective(1, "Infiltrate Jach Priboi's mountain castle fortress", true);
            AddObjective(2, "Disable exterior perimeter security systems", false);
            break;
        case 12:
            AddObjective(1, "Breach the inner sanctum and locate Jach Priboi", true);
            AddObjective(2, "Apprehend Jach Priboi and clear the helicopter LZ", false);
            break;
        case 13:
            AddObjective(1, "Infiltrate the underground nuclear warhead complex", true);
            AddObjective(2, "Locate the primary reactor core and disable cooling", false);
            break;
        case 14:
            AddObjective(1, "Track down Ekk in the nuclear launch silo", true);
            AddObjective(2, "Eliminate Ekk and disarm the nuclear bomb", false);
            break;
        default:
            AddObjective(1, "Complete mission objectives and extract safely", true);
            break;
    }
}

void LevelFlow::AddObjective(uint32_t id, const std::string& desc, bool is_primary) {
    MissionObjective obj;
    obj.id = id;
    obj.description = desc;
    obj.is_primary = is_primary;
    obj.state = ObjectiveState::Pending;
    objectives_.push_back(obj);
}

void LevelFlow::SetObjectiveState(uint32_t id, ObjectiveState state) {
    for (auto& obj : objectives_) {
        if (obj.id == id) {
            obj.state = state;
            break;
        }
    }
}

void LevelFlow::Update(bool player_alive, bool in_extraction_zone) {
    if (status_ != MissionStatus::InProgress) return;

    if (!player_alive) {
        status_ = MissionStatus::Failed;
        return;
    }

    // Check primary objectives
    bool all_primaries_complete = true;
    for (const auto& obj : objectives_) {
        if (obj.is_primary) {
            if (obj.state == ObjectiveState::Failed) {
                status_ = MissionStatus::Failed;
                return;
            }
            if (obj.state != ObjectiveState::Completed) {
                all_primaries_complete = false;
            }
        }
    }

    if (all_primaries_complete && in_extraction_zone) {
        status_ = MissionStatus::Success;
    }
}

} // namespace igi
