#include "audio_out.h"
#include "sound_bank.h"
#include "../logger.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace igi {

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

struct EditorAudio::Impl {
    HWAVEOUT wave_out = nullptr;
    WAVEFORMATEX format{};
    bool ready = false;

    std::mutex mutex;
    SoundBank bank;
    bool bank_loaded = false;

    // One-shot voices: each owns its heap buffer until the driver returns it.
    struct Voice {
        WAVEHDR hdr{};
        std::vector<uint8_t> mixed;
    };
    std::vector<Voice*> voices;

    // Ambience: single looping voice, restarted by the driver's WHDR_DONE poll.
    Voice* ambience = nullptr;
    std::string ambience_name;

    int active_voices() const {
        int n = 0;
        for (Voice* v : voices) if (!(v->hdr.dwFlags & WHDR_DONE)) ++n;
        return n;
    }

    void reap() {
        for (size_t i = 0; i < voices.size();) {
            Voice* v = voices[i];
            if (v->hdr.dwFlags & WHDR_DONE) {
                waveOutUnprepareHeader(wave_out, &v->hdr, sizeof(v->hdr));
                delete v;
                voices.erase(voices.begin() + i);
            } else {
                ++i;
            }
        }
        if (ambience && (ambience->hdr.dwFlags & WHDR_DONE)) {
            // Loop: requeue the same buffer.
            ambience->hdr.dwFlags &= ~WHDR_DONE;
            waveOutPrepareHeader(wave_out, &ambience->hdr, sizeof(ambience->hdr));
            waveOutWrite(wave_out, &ambience->hdr, sizeof(ambience->hdr));
        }
    }

    // Mixes 16-bit PCM at master volume into a fresh byte buffer (winmm wants bytes).
    static std::vector<uint8_t> Mix16(const std::vector<int16_t>& pcm, float volume) {
        std::vector<uint8_t> out(pcm.size() * 2);
        for (size_t i = 0; i < pcm.size(); ++i) {
            int s = static_cast<int>(pcm[i] * volume);
            s = std::clamp(s, -32768, 32767);
            const uint16_t v16 = static_cast<uint16_t>(s);
            std::memcpy(out.data() + i * 2, &v16, 2);
        }
        return out;
    }

    bool Submit(std::vector<uint8_t> bytes, bool loop) {
        Voice* v = new Voice();
        v->mixed = std::move(bytes);
        std::memset(&v->hdr, 0, sizeof(v->hdr));
        v->hdr.lpData = v->mixed.data();
        v->hdr.dwBufferLength = static_cast<DWORD>(v->mixed.size());
        if (loop) v->hdr.dwLoops = 0xFFFFFFFFu; // driver-side loop
        if (waveOutPrepareHeader(wave_out, &v->hdr, sizeof(v->hdr)) != MMSYSERR_NOERROR ||
            waveOutWrite(wave_out, &v->hdr, sizeof(v->hdr)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(wave_out, &v->hdr, sizeof(v->hdr));
            delete v;
            return false;
        }
        if (loop) {
            StopAmbienceLocked();
            ambience = v;
        } else {
            voices.push_back(v);
        }
        return true;
    }

    void StopAmbienceLocked() {
        if (!ambience) return;
        waveOutReset(wave_out); // simplest reliable stop; also drops one-shots
        waveOutUnprepareHeader(wave_out, &ambience->hdr, sizeof(ambience->hdr));
        delete ambience;
        ambience = nullptr;
        for (Voice* v : voices) delete v;
        voices.clear();
    }
};

EditorAudio& EditorAudio::Get() {
    static EditorAudio inst;
    return inst;
}

EditorAudio::Impl& EditorAudio::Impl_() {
    static Impl impl;
    return impl;
}

bool EditorAudio::Init(int sample_rate, int channels, int bits_per_sample) {
    Impl& im = Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    if (im.ready && im.format.nSamplesPerSec == static_cast<DWORD>(sample_rate) &&
        im.format.nChannels == static_cast<WORD>(channels) &&
        im.format.wBitsPerSample == static_cast<WORD>(bits_per_sample)) {
        return true;
    }
    Shutdown();
    std::memset(&im.format, 0, sizeof(im.format));
    im.format.wFormatTag = WAVE_FORMAT_PCM;
    im.format.nChannels = static_cast<WORD>(channels);
    im.format.nSamplesPerSec = static_cast<DWORD>(sample_rate);
    im.format.wBitsPerSample = static_cast<WORD>(bits_per_sample);
    im.format.nBlockAlign = im.format.nChannels * im.format.wBitsPerSample / 8;
    im.format.nAvgBytesPerSec = im.format.nSamplesPerSec * im.format.nBlockAlign;
    if (waveOutOpen(&im.wave_out, WAVE_MAPPER, &im.format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        Logger::Get().Log(LogLevel::WARNING, "[Audio] No waveOut device available; audio disabled.");
        im.ready = false;
        return false;
    }
    im.ready = true;
    Logger::Get().Log(LogLevel::INFO,
                      "[Audio] waveOut ready: " + std::to_string(sample_rate) + " Hz, " +
                      std::to_string(channels) + " ch, " + std::to_string(bits_per_sample) + " bit.");
    return true;
}

void EditorAudio::Shutdown() {
    Impl& im = Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    if (im.wave_out) {
        im.StopAmbienceLocked();
        waveOutClose(im.wave_out);
        im.wave_out = nullptr;
    }
    im.ready = false;
}

bool EditorAudio::IsReady() const {
    return const_cast<EditorAudio*>(this)->Impl_().ready;
}

void EditorAudio::SetMasterVolume(float volume) {
    master_volume_ = std::clamp(volume, 0.0f, 1.0f);
}

bool EditorAudio::PlaySound(const std::string& name) {
    Impl& im = Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    if (!im.ready) return false;
    im.reap();
    if (im.active_voices() >= kVoiceCap) {
        return false; // pool full: fail rather than steal, matching open-igi
    }
    SoundBankEntry entry;
    if (!im.bank.TryGet(name, entry) || entry.pcm.empty()) return false;
    return im.Submit(Impl::Mix16(entry.pcm, master_volume_), /*loop=*/false);
}

bool EditorAudio::PlayAmbience(const std::string& name) {
    Impl& im = Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    if (!im.ready) return false;
    SoundBankEntry entry;
    if (!im.bank.TryGet(name, entry) || entry.pcm.empty()) return false;
    return im.Submit(Impl::Mix16(entry.pcm, master_volume_), /*loop=*/true);
}

void EditorAudio::StopAmbience() {
    Impl& im = Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    if (im.ready) im.StopAmbienceLocked();
}

void EditorAudio::StopAllVoices() {
    Impl& im = Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    if (im.ready) im.StopAmbienceLocked();
}

int EditorAudio::ActiveVoiceCount() const {
    Impl& im = const_cast<EditorAudio*>(this)->Impl_();
    std::lock_guard<std::mutex> lock(im.mutex);
    im.reap();
    return im.active_voices();
}

#else // !_WIN32 — graceful no-op so macOS/Linux builds are untouched

struct EditorAudio::Impl {};

EditorAudio& EditorAudio::Get() {
    static EditorAudio inst;
    return inst;
}
EditorAudio::Impl& EditorAudio::Impl_() {
    static Impl impl;
    return impl;
}
bool EditorAudio::Init(int, int, int) { return false; }
void EditorAudio::Shutdown() {}
bool EditorAudio::IsReady() const { return false; }
void EditorAudio::SetMasterVolume(float volume) { master_volume_ = std::clamp(volume, 0.0f, 1.0f); }
bool EditorAudio::PlaySound(const std::string&) { return false; }
bool EditorAudio::PlayAmbience(const std::string&) { return false; }
void EditorAudio::StopAmbience() {}
void EditorAudio::StopAllVoices() {}
int EditorAudio::ActiveVoiceCount() const { return 0; }

#endif // _WIN32

} // namespace igi
