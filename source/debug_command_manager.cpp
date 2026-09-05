#include "pch.h"
#include "debug_command_manager.h"
#include "app.h"
#include "logger.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/tinygltf/stb_image_write.h"
#include "renderer/renderer.h"
#include "level/level.h"
#include "level/level_objects.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <direct.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

DebugCommandManager::DebugCommandManager(App* app) 
    : app_(app), running_(false), commands_file_path_("editor/tools/debug-command.txt") {
}

DebugCommandManager::~DebugCommandManager() {
    Stop();
}

void DebugCommandManager::Start() {
    if (running_) return;
    running_ = true;
    watcher_thread_ = std::thread(&DebugCommandManager::WatcherThread, this);
}

void DebugCommandManager::Stop() {
    running_ = false;
    if (watcher_thread_.joinable()) {
        watcher_thread_.join();
    }
}

void DebugCommandManager::WatcherThread() {
    while (running_) {
        std::ifstream file(commands_file_path_);
        if (file.is_open()) {
            std::string line;
            std::vector<std::string> lines;
            bool has_commands = false;
            while (std::getline(file, line)) {
                lines.push_back(line);
                if (line.empty()) continue;
                
                std::istringstream iss(line);
                std::string token;
                iss >> token;
                
                if (token == "goto" || token == "capture-model" || token == "delete" || token == "wireframe" || token == "draw-parts") {
                    DebugCommand cmd;
                    cmd.type = token;
                    while (iss >> token) {
                        if (token.find("level=") == 0) {
                            cmd.level = std::stoi(token.substr(6));
                        } else if (token.find("model=") == 0) {
                            cmd.modelId = token.substr(6);
                        } else if (token.find("val=") == 0) {
                            cmd.val = std::stoi(token.substr(4));
                        } else if (token.find("x=") == 0) {
                            cmd.x = std::stod(token.substr(2));
                            cmd.has_pos = true;
                        } else if (token.find("y=") == 0) {
                            cmd.y = std::stod(token.substr(2));
                            cmd.has_pos = true;
                        } else if (token.find("z=") == 0) {
                            cmd.z = std::stod(token.substr(2));
                            cmd.has_pos = true;
                        } else if (token.find("orbit_frames=") == 0) {
                            cmd.orbit_frames = std::stoi(token.substr(13));
                        } else if (token.find("video_fps=") == 0) {
                            cmd.video_fps = std::stoi(token.substr(10));
                        } else if (token.find("orbit=") == 0) {
                            cmd.orbit_frames = std::stoi(token.substr(6));
                        }
                    }
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    command_queue_.push(cmd);
                    has_commands = true;
                }
            }
            file.close();

            if (has_commands) {
                // Clear file
                std::ofstream out_file(commands_file_path_, std::ios::trunc);
                out_file.close();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DebugCommandManager::Update() {
    std::queue<DebugCommand> local_queue;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::swap(local_queue, command_queue_);
    }

    while (!local_queue.empty()) {
        ProcessCommand(local_queue.front());
        local_queue.pop();
    }
}

void DebugCommandManager::ProcessCommand(const DebugCommand& cmd) {
    if (cmd.level != -1 && cmd.level != app_->GetCurLevelNo()) {
        app_->LoadLevel(cmd.level);
    }
    
    if (cmd.type == "goto") {
        GotoModel(cmd);
    } else if (cmd.type == "capture-model") {
        CaptureModel(cmd);
    } else if (cmd.type == "delete") {
        DeleteModel(cmd);
    } else if (cmd.type == "wireframe") {
        if (cmd.val) {
            if (!app_->GetOverlayWireframe()) app_->ToggleOverlayWireframe();
        } else {
            if (app_->GetOverlayWireframe()) app_->ToggleOverlayWireframe();
        }
    } else if (cmd.type == "draw-parts") {
        app_->SetDrawParts(cmd.val);
    }
}

void DebugCommandManager::GotoModel(const DebugCommand& cmd) {
    auto& objects = app_->level_.GetLevelObjects().GetObjects();
    
    double cx = cmd.x, cy = cmd.y, cz = cmd.z;
    if (std::abs(cx) < 1000000.0 && std::abs(cy) < 1000000.0 && std::abs(cz) < 1000000.0) {
        cx *= 256.0; cy *= 256.0; cz *= 256.0; // Assume meters, convert to engine units
    }

    int best_idx = -1;
    double min_dist = 1e30;

    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].deleted && (objects[i].modelId == cmd.modelId || objects[i].segmentModelId == cmd.modelId)) {
            if (!cmd.has_pos) {
                best_idx = (int)i;
                break; // If no pos specified, pick first
            }
            double dx = objects[i].pos.x - cx;
            double dy = objects[i].pos.y - cy;
            double dz = objects[i].pos.z - cz;
            double dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq < min_dist) {
                min_dist = dist_sq;
                best_idx = (int)i;
            }
        }
    }

    if (best_idx != -1) {
        app_->viewer_.pos_ = glm::vec3(objects[best_idx].pos);
        app_->viewer_.yaw_ = -objects[best_idx].rot.z;
        app_->viewer_.pitch_ = 0;
        app_->viewer_.roll_ = 0;
        
        app_->UpdateViewerVectors();
        app_->selected_object_index_ = best_idx;
    }
}

void DebugCommandManager::DeleteModel(const DebugCommand& cmd) {
    auto& objects = app_->level_.GetLevelObjects().GetObjects();
    
    double cx = cmd.x, cy = cmd.y, cz = cmd.z;
    if (std::abs(cx) < 1000000.0 && std::abs(cy) < 1000000.0 && std::abs(cz) < 1000000.0) {
        cx *= 256.0; cy *= 256.0; cz *= 256.0; // Assume meters, convert to engine units
    }

    int best_idx = -1;
    double min_dist = 1e30;

    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].deleted && (objects[i].modelId == cmd.modelId || objects[i].segmentModelId == cmd.modelId)) {
            if (!cmd.has_pos) {
                best_idx = (int)i;
                break; // If no pos specified, pick first
            }
            double dx = objects[i].pos.x - cx;
            double dy = objects[i].pos.y - cy;
            double dz = objects[i].pos.z - cz;
            double dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq < min_dist) {
                min_dist = dist_sq;
                best_idx = (int)i;
            }
        }
    }

    if (best_idx != -1) {
        objects[best_idx].deleted = true;
        objects[best_idx].modified = true;
        if (app_->selected_object_index_ == best_idx) {
            app_->selected_object_index_ = -1;
        }
        Logger::Get().Log(LogLevel::INFO, "[App] Deleted model via developer command: " + cmd.modelId);
    }
}


static std::string FindFFmpegBin() {
    static const char* kCandidates[] = {
        "D:\\henv\\Lib\\site-packages\\imageio_ffmpeg\\binaries\\ffmpeg-win-x86_64-v7.1.exe",
        "ffmpeg.exe",
        nullptr
    };
    for (int i = 0; kCandidates[i]; ++i) {
        DWORD attr = GetFileAttributesA(kCandidates[i]);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            char shortPath[MAX_PATH];
            if (GetShortPathNameA(kCandidates[i], shortPath, MAX_PATH) > 0)
                return std::string(shortPath);
            return std::string(kCandidates[i]);
        }
    }
    return "ffmpeg.exe";
}

static std::string JsonStr(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    out += '"';
    return out;
}

static bool WriteImageBGRA(const char* bmpPath, const char* pngPath,
                            const unsigned char* bgra, int w, int h) {
    // Convert BGRA (bottom-up) to RGB (top-down)
    std::vector<unsigned char> rgb(w * h * 3);
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = bgra + (size_t)(h - 1 - y) * w * 4;
        unsigned char*       dst = rgb.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; ++x, src += 4, dst += 3) {
            dst[0] = src[2]; // R
            dst[1] = src[1]; // G
            dst[2] = src[0]; // B
        }
    }
    bool ok = true;
    if (bmpPath) ok &= (stbi_write_bmp(bmpPath, w, h, 3, rgb.data()) != 0);
    if (pngPath) ok &= (stbi_write_png(pngPath, w, h, 3, rgb.data(), w * 3) != 0);
    return ok;
}
void DebugCommandManager::CaptureModel(const DebugCommand& cmd) {
    auto& objects = app_->level_.GetLevelObjects().GetObjects();

    double cx = cmd.x, cy = cmd.y, cz = cmd.z;
    if (std::abs(cx) < 1000000.0 && std::abs(cy) < 1000000.0 && std::abs(cz) < 1000000.0)
        cx *= 256.0, cy *= 256.0, cz *= 256.0;

    int target_idx = -1;
    double min_dist = 1e30;
    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].deleted &&
            (objects[i].modelId == cmd.modelId || objects[i].segmentModelId == cmd.modelId)) {
            if (!cmd.has_pos) { target_idx = (int)i; break; }
            double dx = objects[i].pos.x - cx,
                   dy = objects[i].pos.y - cy,
                   dz = objects[i].pos.z - cz;
            double d2 = dx*dx + dy*dy + dz*dz;
            if (d2 < min_dist) { min_dist = d2; target_idx = (int)i; }
        }
    }
    if (target_idx == -1) {
        Logger::Get().Log(LogLevel::WARNING, "[CaptureModel] Model not found: " + cmd.modelId);
        return;
    }

    auto& obj = objects[target_idx];
    app_->selected_object_index_ = target_idx;
    Logger::Get().Log(LogLevel::INFO, "[CaptureModel] target=" + obj.modelId +
        " taskId=" + obj.taskId +
        " lightmap=" + std::to_string(app_->renderer_.HasLightmapForTask(LightmapTaskKey(obj))));

    // Viewport
    const int W = app_->window_state_.viewport_width_;
    const int H = app_->window_state_.viewport_height_;
    const int bgraBytes = W * H * 4;

    // ── Category & Framing Calculation ──────────────────────────────────────
    const bool isAI = (obj.type.rfind("Human", 0) == 0) || (obj.type == "AI") || (obj.type.find("Soldier") != std::string::npos);
    const bool isVehicle = (obj.type == "Car" || obj.type == "Heli" || obj.type == "Train" || obj.type == "Plane" || obj.type == "CarAI");
    const bool isBuilding = obj.isBuilding || (obj.type == "Building");

    // Query mesh extents (in MEF units, 1 MEF unit = 40.96 engine units)
    glm::vec3 meshExt = app_->renderer_.GetMeshExtents(obj.modelId, obj.isBuilding);
    const float scale = (obj.scale > 0.0f) ? obj.scale : 1.0f;
    const float kMefToEngine = 40.96f * scale;

    glm::vec3 extEngine = meshExt * kMefToEngine;
    float boundRadius = glm::length(extEngine);

    double targetX = obj.pos.x;
    double targetY = obj.pos.y;
    double targetZ = obj.pos.z;

    double kOrbitRadius    = 650.0;
    double kExteriorHeight = 300.0;
    double kInteriorHeight = 150.0;

    if (isAI) {
        // AI characters: height ~ 460 engine units (1.8m)
        // Frame full body clearly: "for AI it can be little far but not that much"
        kOrbitRadius    = 600.0;
        kExteriorHeight = 180.0;
        targetZ        += 180.0; // center on torso
        kInteriorHeight = targetZ;
    } else if (isVehicle) {
        // Vehicles: cars, trucks, APCs, helicopters, planes
        if (boundRadius > 50.0f) {
            kOrbitRadius = std::clamp((double)boundRadius * 1.8, 1400.0, 5000.0);
            targetZ     += std::max((double)extEngine.z, 120.0);
        } else {
            kOrbitRadius = 2500.0;
            targetZ     += 200.0;
        }
        kExteriorHeight = kOrbitRadius * 0.35;
        kInteriorHeight = targetZ;
    } else if (isBuilding) {
        // Buildings: water towers, hangars, offices, bunkers
        // "for buildings are too much it can be far away but not that far OK right now only for the buildings it can be too far but not that too far"
        if (boundRadius > 50.0f) {
            kOrbitRadius = std::clamp((double)boundRadius * 1.4, 2500.0, 9500.0);
            targetZ     += std::max((double)extEngine.z * 0.6, 350.0);
        } else {
            kOrbitRadius = 7500.0;
            targetZ     += 1000.0;
        }
        kExteriorHeight = kOrbitRadius * 0.40;
        kInteriorHeight = std::clamp((double)extEngine.z * 0.5, 400.0, 2500.0);
    } else {
        // Rigid objects: chair, table, desk, barrel, computer, phone, alarm switch, pickups
        // "it needs to be very close for smaller objects OK rigid objects are very smaller they needs to be very close"
        // "like chair and table and smaller objects it needs to be camera needs to be very close to them OK"
        if (boundRadius > 10.0f) {
            kOrbitRadius = std::clamp((double)boundRadius * 1.35, 250.0, 750.0);
            targetZ     += std::max((double)extEngine.z * 0.5, 40.0);
        } else {
            // Very small props or unmeasured
            kOrbitRadius = 380.0;
            targetZ     += 40.0;
        }
        kExteriorHeight = kOrbitRadius * 0.35;
        kInteriorHeight = targetZ;
    }

    const float extPitchDeg = -glm::degrees(static_cast<float>(
        std::atan2(kExteriorHeight, kOrbitRadius)));

    Logger::Get().Log(LogLevel::INFO, "[CaptureModel] Camera framing: radius=" +
        std::to_string(kOrbitRadius) + " height=" + std::to_string(kExteriorHeight) +
        " pitch=" + std::to_string(extPitchDeg) + " targetZ=" + std::to_string(targetZ) +
        " boundRadius=" + std::to_string(boundRadius) + " type=" + obj.type);

    // Output paths
    _mkdir("screenshots");
    char donePath[256], evidPath[256];
    snprintf(donePath, sizeof(donePath),  "screenshots/Level%02d_Model%s_Done.txt",
             cmd.level, cmd.modelId.c_str());
    snprintf(evidPath, sizeof(evidPath),  "screenshots/Level%02d_Model%s_evidence.jsonl",
             cmd.level, cmd.modelId.c_str());
    _unlink(donePath);
    _unlink(evidPath);

    // Evidence JSONL (one record per captured view)
    std::ofstream evFile(evidPath, std::ios::out | std::ios::trunc);

    // Camera setter
    auto set_camera = [&](float camX, float camY, float camZ, float yawDeg, float pitchDeg) {
        app_->viewer_.pos_   = glm::vec3(camX, camY, camZ);
        app_->viewer_.yaw_   = yawDeg;
        app_->viewer_.pitch_ = pitchDeg;
        app_->UpdateViewerVectors();
        app_->UpdateViewDefine();
    };

    // Sync still capture: double-render -> glReadPixels -> BMP + PNG + evidence
    std::vector<unsigned char> bgra(bgraBytes);
    auto capture_still = [&](const char* suffix,
                              float camX, float camY, float camZ,
                              float yawDeg, float pitchDeg) {
        set_camera(camX, camY, camZ, yawDeg, pitchDeg);
        app_->OnDisplay(); // fill back-buffer
        app_->OnDisplay(); // present to front
        glReadBuffer(GL_FRONT);
        glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, bgra.data());
        glReadBuffer(GL_BACK);

        char bmpPath[256], pngPath[256];
        snprintf(bmpPath, sizeof(bmpPath), "screenshots/Level%02d_Model%s_%s.bmp",
                 cmd.level, cmd.modelId.c_str(), suffix);
        snprintf(pngPath, sizeof(pngPath), "screenshots/Level%02d_Model%s_%s.png",
                 cmd.level, cmd.modelId.c_str(), suffix);
        WriteImageBGRA(bmpPath, pngPath, bgra.data(), W, H);

        if (evFile.is_open()) {
            evFile << "{\"level\":" << cmd.level
                   << ",\"modelId\":" << JsonStr(cmd.modelId)
                   << ",\"taskId\":"  << JsonStr(obj.taskId)
                   << ",\"view\":"    << JsonStr(std::string(suffix))
                   << ",\"camera\":{\"x\":" << camX << ",\"y\":" << camY
                   << ",\"z\":" << camZ << ",\"yaw\":" << yawDeg
                   << ",\"pitch\":" << pitchDeg << "}"
                   << ",\"bmp\":" << JsonStr(std::string(bmpPath))
                   << ",\"png\":" << JsonStr(std::string(pngPath))
                   << ",\"source\":\"rendered-framebuffer\""
                   << ",\"rendered\":true"
                   << "}\n";
        }
    };

    // 6 exterior shots (60 degrees apart)
    for (int angle = 0; angle < 360; angle += 60) {
        const float rad = glm::radians((float)angle);
        const float camX = (float)targetX - std::sin(rad) * (float)kOrbitRadius;
        const float camY = (float)targetY - std::cos(rad) * (float)kOrbitRadius;
        const float camZ = (float)targetZ + (float)kExteriorHeight;
        char suffix[32]; snprintf(suffix, sizeof(suffix), "Ext_%03d", angle);
        capture_still(suffix, camX, camY, camZ, (float)((360 - angle) % 360), extPitchDeg);
    }

    // 4 interior / detail shots (90 degrees apart)
    for (int angle = 0; angle < 360; angle += 90) {
        char suffix[32]; snprintf(suffix, sizeof(suffix), "Int_%03d", angle);
        if (isBuilding) {
            // Inside the building looking outwards
            capture_still(suffix,
                (float)targetX, (float)targetY, (float)(obj.pos.z + kInteriorHeight),
                (float)((360 - angle) % 360), 0.0f);
        } else {
            // Close-up detail inspection view from 4 cardinal directions
            const float rad = glm::radians((float)angle);
            const float closeRadius = (float)std::max(kOrbitRadius * 0.55, 220.0);
            const float closeCamX = (float)targetX - std::sin(rad) * closeRadius;
            const float closeCamY = (float)targetY - std::cos(rad) * closeRadius;
            const float closeCamZ = (float)targetZ + (float)(kExteriorHeight * 0.25);
            const float closePitch = -glm::degrees(static_cast<float>(
                std::atan2(kExteriorHeight * 0.25, closeRadius)));
            capture_still(suffix, closeCamX, closeCamY, closeCamZ, (float)((360 - angle) % 360), closePitch);
        }
    }

    // Orbit video: PBO ping-pong -> FFmpeg stdin pipe
    bool orbitOk = false;
    std::string encError;
    if (cmd.orbit_frames > 0) {
        const int videoFps = (cmd.video_fps > 0) ? cmd.video_fps : 12;
        char videoPath[256], sidecarPath[256];
        snprintf(videoPath,   sizeof(videoPath),   "screenshots/Level%02d_Model%s_orbit.mp4",
                 cmd.level, cmd.modelId.c_str());
        snprintf(sidecarPath, sizeof(sidecarPath), "screenshots/Level%02d_Model%s_orbit.json",
                 cmd.level, cmd.modelId.c_str());

        std::string ffmpegBin = FindFFmpegBin();
        // Pipe: raw BGRA -> libx264/yuv420p MP4
        char pipeCmd[1024];
        snprintf(pipeCmd, sizeof(pipeCmd),
            "%s -y -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - "
            "-c:v libx264 -pix_fmt yuv420p -crf 20 -movflags +faststart "
            "%s 2>screenshots/ffmpeg_err.log",
            ffmpegBin.c_str(), W, H, videoFps, videoPath);

        Logger::Get().Log(LogLevel::INFO, "[CaptureModel] Opening FFmpeg stdin pipe: " + std::string(pipeCmd));
        FILE* pipe = _popen(pipeCmd, "wb");

        if (!pipe) {
            encError = "_popen failed";
            Logger::Get().Log(LogLevel::WARNING, "[CaptureModel] " + encError);
        } else {
            // PBO ping-pong setup
            GLuint pbos[2] = {0, 0};
            glGenBuffers(2, pbos);
            for (int i = 0; i < 2; ++i) {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[i]);
                glBufferData(GL_PIXEL_PACK_BUFFER, bgraBytes, nullptr, GL_STREAM_READ);
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

            int curPbo = 0;

            // Warm-up render to seed PBO[0] before the loop
            {
                const float camX = (float)targetX - 0.0f * (float)kOrbitRadius;
                const float camY = (float)targetY - 1.0f * (float)kOrbitRadius;
                const float camZ = (float)targetZ + (float)kExteriorHeight;
                set_camera(camX, camY, camZ, 0.0f, extPitchDeg);
                app_->OnDisplay();
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[curPbo]);
            glReadBuffer(GL_FRONT);
            glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, 0); // async DMA start
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

            for (int frame = 0; frame < cmd.orbit_frames; ++frame) {
                const float angle_deg = (360.0f * (float)frame) / (float)cmd.orbit_frames;
                const float rad = glm::radians(angle_deg);
                set_camera(
                    (float)targetX - std::sin(rad) * (float)kOrbitRadius,
                    (float)targetY - std::cos(rad) * (float)kOrbitRadius,
                    (float)targetZ + (float)kExteriorHeight,
                    std::fmod(360.0f - angle_deg, 360.0f), extPitchDeg);
                app_->OnDisplay(); // render next frame

                // Kick async readback for THIS frame into next PBO
                int nextPbo = curPbo ^ 1;
                glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[nextPbo]);
                glReadBuffer(GL_FRONT);
                glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, 0);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                // Map PREVIOUS PBO (overlaps with current render) -> pipe to FFmpeg
                glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[curPbo]);
                void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
                if (ptr) {
                    fwrite(ptr, 1, bgraBytes, pipe);
                    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                }
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                curPbo = nextPbo;
            }

            // Flush last PBO (frame cmd.orbit_frames-1's readback)
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[curPbo]);
            void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (ptr) {
                fwrite(ptr, 1, bgraBytes, pipe);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

            glDeleteBuffers(2, pbos);

            int exitCode = _pclose(pipe); // blocks until FFmpeg finishes encoding
            DWORD attr = GetFileAttributesA(videoPath);
            orbitOk = (exitCode == 0) ||
                      (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
            Logger::Get().Log(LogLevel::INFO, "[CaptureModel] FFmpeg exit=" +
                std::to_string(exitCode) + (orbitOk ? " MP4 ok" : " MP4 missing"));
        }

        // Sidecar JSON
        std::ofstream sidecar(sidecarPath);
        sidecar << "{\"file\":" << JsonStr(std::string(videoPath))
                << ",\"frames\":" << cmd.orbit_frames
                << ",\"fps\":" << ((cmd.video_fps > 0) ? cmd.video_fps : 12)
                << ",\"durationSeconds\":" <<
                   ((double)cmd.orbit_frames / ((cmd.video_fps > 0) ? cmd.video_fps : 12))
                << ",\"source\":\"rendered-framebuffer\""
                << ",\"ok\":" << (orbitOk ? "true" : "false")
                << ",\"encoderError\":" << JsonStr(encError)
                << "}\n";
    }

    evFile.close();

    // Done sentinel — written last so PowerShell wait function triggers only when all files exist
    std::ofstream doneFile(donePath);
    doneFile << "DONE frames=" << cmd.orbit_frames
             << " orbit=" << (orbitOk ? "ok" : "skip") << "\n";
    doneFile.close();

    Logger::Get().Log(LogLevel::INFO, "[CaptureModel] Complete model=" + cmd.modelId +
        " orbit=" + (orbitOk ? "ok" : "skip"));
}
