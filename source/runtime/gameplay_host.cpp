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
    runtime_renderer_.Clear();
}

bool GameplayHost::OpenGameplay(const EditorSnapshot& snapshot) {
    return session_.Open(snapshot);
}

bool GameplayHost::CloseGameplay(EditorSnapshot& out_snapshot) {
    return session_.Close(out_snapshot);
}

bool GameplayHost::ApplyAndRestartGameplay(const EditorSnapshot& snapshot) {
    return session_.ApplyEditorSnapshot(snapshot);
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

void GameplayHost::Render(const RuntimeRenderCamera& camera) {
    if (!session_.IsActive()) {
        runtime_renderer_.Clear();
        return;
    }

    runtime_renderer_.Capture(session_.GetWorld(), camera);
}

bool GameplayHost::InitializeGameplayWindow(
    int editor_window_id,
    int width,
    int height,
    const GameplayWindowCallbacks& callbacks) {
    return gameplay_window_.Create(editor_window_id, width, height, callbacks);
}

void GameplayHost::ShutdownGameplayWindow() {
    gameplay_window_.Destroy();
}

void GameplayHost::NotifyGameplayWindowClosed() {
    gameplay_window_.NotifyClosed();
}

void GameplayHost::ShowGameplayWindow() {
    gameplay_window_.Show();
}

void GameplayHost::HideGameplayWindow() {
    gameplay_window_.Hide();
}

void GameplayHost::FocusGameplayWindow() {
    gameplay_window_.Focus();
    session_.GetInputRouter().SetFocus(WindowFocusTarget::GameplayWindow);
}

void GameplayHost::FocusEditorWindow() {
    gameplay_window_.FocusEditor();
    session_.GetInputRouter().SetFocus(WindowFocusTarget::EditorWindow);
}

void GameplayHost::MakeGameplayWindowCurrent() const {
    gameplay_window_.MakeCurrent();
}

} // namespace igi
