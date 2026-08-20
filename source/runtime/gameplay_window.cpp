// gameplay_window.cpp - Windows gameplay-window lifecycle implementation
#include "gameplay_window.h"

#if defined(_WIN32)
#include <windows.h>
#include <freeglut.h>
#endif

namespace igi {

GameplayWindowHost::~GameplayWindowHost() {
    Destroy();
}

bool GameplayWindowHost::Create(
    int editor_window_id,
    int width,
    int height,
    const GameplayWindowCallbacks& callbacks) {
#if !defined(_WIN32)
    (void)editor_window_id;
    (void)width;
    (void)height;
    (void)callbacks;
    return false;
#else
    if (IsCreated() || editor_window_id <= 0 || callbacks.display == nullptr) {
        return IsCreated();
    }

    editor_window_id_ = editor_window_id;
    glutSetWindow(editor_window_id_);

    glutInitWindowSize(width > 0 ? width : 1280, height > 0 ? height : 720);
    // FreeGLUT's current-context mode keeps the renderer's VAOs, buffers,
    // textures, and shader programs valid in the gameplay window. A separate
    // WGL context plus wglShareLists would not share VAOs, which would make
    // the editor's loaded meshes disappear in gameplay presentation.
    glutSetOption(GLUT_RENDERING_CONTEXT, GLUT_USE_CURRENT_CONTEXT);
    gameplay_window_id_ = glutCreateWindow("IGI Gameplay");
    if (gameplay_window_id_ == 0) {
        editor_window_id_ = 0;
        return false;
    }

    glutDisplayFunc(callbacks.display);
    if (callbacks.reshape != nullptr) glutReshapeFunc(callbacks.reshape);
    if (callbacks.mouse != nullptr) glutMouseFunc(callbacks.mouse);
    if (callbacks.mouse_wheel != nullptr) glutMouseWheelFunc(callbacks.mouse_wheel);
    if (callbacks.motion != nullptr) glutMotionFunc(callbacks.motion);
    if (callbacks.passive_motion != nullptr) glutPassiveMotionFunc(callbacks.passive_motion);
    if (callbacks.special != nullptr) glutSpecialFunc(callbacks.special);
    if (callbacks.special_up != nullptr) glutSpecialUpFunc(callbacks.special_up);
    if (callbacks.keyboard != nullptr) glutKeyboardFunc(callbacks.keyboard);
    if (callbacks.keyboard_up != nullptr) glutKeyboardUpFunc(callbacks.keyboard_up);
    if (callbacks.close != nullptr) glutCloseFunc(callbacks.close);

    glutHideWindow();
    glutSetWindow(editor_window_id_);
    return true;
#endif
}

void GameplayWindowHost::Destroy() {
#if defined(_WIN32)
    if (!IsCreated()) {
        return;
    }

    if (editor_window_id_ != 0) {
        glutSetWindow(editor_window_id_);
    }
    glutDestroyWindow(gameplay_window_id_);
#endif
    gameplay_window_id_ = 0;
    editor_window_id_ = 0;
}

void GameplayWindowHost::NotifyClosed() {
#if defined(_WIN32)
    if (!IsCreated()) {
        return;
    }

    // The close callback is delivered with the gameplay window selected.
    // Restore the editor as the current GLUT window before invalidating the
    // gameplay ID so the next editor frame has a valid context.
    if (editor_window_id_ != 0) {
        glutSetWindow(editor_window_id_);
    }
#endif
    gameplay_window_id_ = 0;
}

void GameplayWindowHost::Show() {
#if defined(_WIN32)
    if (!IsCreated()) return;
    glutSetWindow(gameplay_window_id_);
    glutShowWindow();
#endif
}

void GameplayWindowHost::Hide() {
#if defined(_WIN32)
    if (!IsCreated()) return;
    glutHideWindow();
    if (editor_window_id_ != 0) glutSetWindow(editor_window_id_);
#endif
}

void GameplayWindowHost::Focus() {
#if defined(_WIN32)
    if (!IsCreated()) return;
    glutSetWindow(gameplay_window_id_);
    glutShowWindow();

    // FreeGLUT's current-window selection does not guarantee OS focus after
    // another top-level window was active. Resolve the native handle by the
    // stable title and explicitly return focus to gameplay.
    HWND gameplay_window_handle = FindWindowA(nullptr, "IGI Gameplay");
    if (gameplay_window_handle != nullptr) {
        SetForegroundWindow(gameplay_window_handle);
        SetFocus(gameplay_window_handle);
    }
#endif
}

void GameplayWindowHost::MakeCurrent() const {
#if defined(_WIN32)
    if (IsCreated()) glutSetWindow(gameplay_window_id_);
#endif
}

bool GameplayWindowHost::IsCurrent() const {
#if defined(_WIN32)
    return IsCreated() && glutGetWindow() == gameplay_window_id_;
#else
    return false;
#endif
}

} // namespace igi
