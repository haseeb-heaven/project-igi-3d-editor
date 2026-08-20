// gameplay_host.cpp - Host controller for gameplay runtime, simulation, and twin-window lifecycle implementation
#include "gameplay_host.h"

namespace igi {

GameplayHost::GameplayHost()
    = default;

GameplayHost::~GameplayHost() {
    Shutdown();
}

void GameplayHost::Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z)) {
    session_.Initialize(get_terrain_z, check_collision);
}

void GameplayHost::Shutdown() {
    session_.Shutdown();
}

bool GameplayHost::OpenGameplay(const EditorSnapshot& snapshot) {
    return session_.Open(snapshot);
}

bool GameplayHost::CloseGameplay(EditorSnapshot& out_snapshot) {
    return session_.Close(out_snapshot);
}

void GameplayHost::RestartGameplay() {
    session_.Restart();
}

void GameplayHost::SetPaused(bool paused) {
    session_.SetPaused(paused);
}

void GameplayHost::Update(int64_t now_milliseconds) {
    session_.Update(now_milliseconds);
}

void GameplayHost::Render() {
    if (!session_.IsActive()) return;

    // Gameplay HUD & runtime camera render hook
}

} // namespace igi
