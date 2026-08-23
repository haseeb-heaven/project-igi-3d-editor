#pragma once
#include <cstdint>
#include <vector>

// RIFF/WAVE PCM reader — C++ port of open-igi src/OpenIGI.Engine/Audio/WavPcm.cs.
//
// Reads a plain WAV file down to its format and PCM payload. Only uncompressed
// integer PCM is accepted (audioFormat 1, or 0xFFFE extensible whose first
// subformat word spells the same), 8- or 16-bit. The retail data never ships a
// .wav — its sounds are ILSF — so this serves editor-added assets.

namespace igi {

struct WavPcmFormat {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 16;
};

// Tries to read a WAV file's format and PCM payload. Returns false when the bytes
// are not an uncompressed PCM WAV (bad magic, missing fmt/data, compressed form,
// or unsupported bit width). Chunks are word-aligned; an odd size carries one pad.
bool WavPcmTryRead(const uint8_t* data, size_t size, WavPcmFormat& out_format,
                   std::vector<uint8_t>& out_pcm);

} // namespace igi
