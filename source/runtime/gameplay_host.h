// gameplay_host.h - Host controller for gameplay runtime, simulation, and twin-window lifecycle
#pragma once

#include "runtime_session.h"

namespace igi {

class GameplayHost {
public:
    GameplayHost();
    ~GameplayHost();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z) = nullptr);
    void Shutdown();

    // Mode Transitions (Editor <-> Gameplay)
    bool OpenGameplay(const EditorSnapshot& snapshot);
    bool CloseGameplay(EditorSnapshot& out_snapshot);
    void RestartGameplay();

    // Frame update & render
    void Update(int64_t now_milliseconds);
    void Render();

    // State queries
    bool IsGameplayActive() const { return session_.IsActive(); }
    bool IsPaused() const { return session_.IsPaused(); }
    void SetPaused(bool paused);

    RuntimeSessionState GetSessionState() const { return session_.GetState(); }

    RuntimeWorld& GetWorld() { return session_.GetWorld(); }
    const RuntimeWorld& GetWorld() const { return session_.GetWorld(); }

    WindowInputRouter& GetInputRouter() { return session_.GetInputRouter(); }
    SimulationScheduler& GetScheduler() { return session_.GetScheduler(); }

private:
    RuntimeSession session_;
};

} // namespace igi
