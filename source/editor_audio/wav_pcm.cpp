#include "wav_pcm.h"

#include <cstring>

namespace igi {

namespace {
uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
int32_t ReadI32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
bool TagIs(const uint8_t* p, const char (&tag)[5]) {
    return std::memcmp(p, tag, 4) == 0;
}
} // namespace

bool WavPcmTryRead(const uint8_t* data, size_t size, WavPcmFormat& out_format,
                   std::vector<uint8_t>& out_pcm) {
    // 'RIFF' <size> 'WAVE', then chunks; sizes little-endian like everything here.
    if (size < 44 || !TagIs(data, "RIFF") || !TagIs(data + 8, "WAVE")) {
        return false;
    }

    uint32_t sample_rate = 0, channels = 0, bits = 0;
    bool format_read = false;

    size_t offset = 12;
    while (offset + 8 <= size) {
        const uint8_t* tag = data + offset;
        const int32_t chunk_size = ReadI32(data + offset + 4);
        offset += 8;

        if (chunk_size < 0 || offset + static_cast<size_t>(chunk_size) > size) {
            return false;
        }

        if (TagIs(tag, "fmt ") && chunk_size >= 16) {
            const uint16_t audio_format = ReadU16(data + offset);
            channels = ReadU16(data + offset + 2);
            sample_rate = static_cast<uint32_t>(ReadI32(data + offset + 4));
            bits = ReadU16(data + offset + 14);
            format_read = (audio_format == 1 || audio_format == 0xFFFE);
        } else if (TagIs(tag, "data")) {
            if (!format_read || channels == 0 || sample_rate == 0 ||
                (bits != 8 && bits != 16)) {
                return false;
            }
            out_format.sample_rate = sample_rate;
            out_format.channels = static_cast<uint16_t>(channels);
            out_format.bits_per_sample = static_cast<uint16_t>(bits);
            out_pcm.assign(data + offset, data + offset + chunk_size);
            return !out_pcm.empty();
        }

        // Word-aligned: an odd chunk size carries one pad byte.
        offset += static_cast<size_t>(chunk_size) + (chunk_size & 1);
    }
    return false;
}

} // namespace igi
