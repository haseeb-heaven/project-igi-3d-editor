#include "ilsf_sound.h"

#include "../logger.h"

#include <algorithm>
#include <cstring>

namespace igi {

bool IsIlsf(const uint8_t* data, size_t size) {
    return size >= 4 && data[0] == 'I' && data[1] == 'L' && data[2] == 'S' && data[3] == 'F';
}

bool ParseIlsfHeader(const uint8_t* data, size_t size, IlsfSoundHeader& out) {
    if (size < static_cast<size_t>(IlsfSoundHeader::kSize) || !IsIlsf(data, size)) {
        return false;
    }
    uint16_t format = 0, bits = 0, channels = 0, reserved = 0;
    uint32_t rate = 0, frames = 0;
    std::memcpy(&format, data + 4, 2);
    std::memcpy(&bits, data + 6, 2);
    std::memcpy(&channels, data + 8, 2);
    std::memcpy(&reserved, data + 10, 2);
    std::memcpy(&rate, data + 12, 4);
    std::memcpy(&frames, data + 16, 4);

    if (static_cast<uint16_t>(IlsfFormat::ResidentAdpcm) < format || channels < 1 ||
        channels > IlsfAdpcmDecoder::kMaximumChannels || rate == 0) {
        return false;
    }
    out.format = static_cast<IlsfFormat>(format);
    out.bits_per_sample = bits;
    out.channels = channels;
    out.reserved = reserved;
    out.sample_rate = rate;
    out.frame_count = frames;
    return true;
}

// ── IMA ADPCM ────────────────────────────────────────────────────────────────
// Step-index adjustment per nibble (0x543920): {-1,-1,-1,-1,2,4,6,8} for the
// signed half, repeated.
namespace {
constexpr int kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};
// The 89 quantiser step sizes (0x543930), the standard IMA table.
constexpr int kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
} // namespace

IlsfAdpcmDecoder::IlsfAdpcmDecoder(int channels) : channels_(channels) {}

long IlsfAdpcmDecoder::EncodedByteCount(int channels, long frame_count) {
    const long nibbles = frame_count * channels;
    return (nibbles + 1) / 2;
}

int16_t IlsfAdpcmDecoder::Step(int channel, int nibble) {
    // The step in force is the one the PREVIOUS index selected; the index moves
    // first but the new step only applies to the next sample — reproduces the
    // original's cached-step variable exactly.
    const int step = kStepTable[step_index_[channel]];
    step_index_[channel] = std::clamp(step_index_[channel] + kIndexTable[nibble],
                                      0, kMaximumStepIndex);

    // difference = step/8 + step/4*b0 + step/2*b1 + step*b2, built by shifts.
    int magnitude = step >> 3;
    if ((nibble & 4) != 0) magnitude += step;
    if ((nibble & 2) != 0) magnitude += step >> 1;
    if ((nibble & 1) != 0) magnitude += step >> 2;

    int predictor = (nibble & 8) != 0 ? predictor_[channel] - magnitude
                                      : predictor_[channel] + magnitude;
    predictor = std::clamp(predictor, -32768, 32767);
    predictor_[channel] = predictor;
    return static_cast<int16_t>(predictor);
}

int IlsfAdpcmDecoder::Decode(const uint8_t* source, size_t source_size,
                             int16_t* destination, size_t dest_samples, int frame_count) {
    const size_t sample_count = static_cast<size_t>(frame_count) * channels_;
    if (dest_samples < sample_count) return 0;

    const size_t nibbles_needed = sample_count - (has_pending_ ? 1 : 0);
    const size_t bytes_needed = (nibbles_needed + 1) / 2;
    if (source_size < bytes_needed) return 0;

    size_t read = 0;
    size_t written = 0;
    for (int frame = 0; frame < frame_count; ++frame) {
        for (int channel = 0; channel < channels_; ++channel) {
            int nibble;
            if (has_pending_) {
                nibble = pending_ & 0xF;      // low nibble second
            } else {
                pending_ = source[read++];    // HIGH nibble first within a byte
                nibble = pending_ >> 4;
            }
            has_pending_ = !has_pending_;
            destination[written++] = Step(channel, nibble);
        }
    }
    return static_cast<int>(read);
}

bool DecodeIlsfPcm(const uint8_t* data, size_t size, std::vector<int16_t>& out_pcm) {
    IlsfSoundHeader header;
    if (!ParseIlsfHeader(data, size, header)) return false;

    const uint8_t* payload = data + IlsfSoundHeader::kSize;
    const size_t payload_size = size - IlsfSoundHeader::kSize;
    out_pcm.assign(header.DecodedByteCount() / sizeof(int16_t), 0);

    if (!header.IsAdpcm()) {
        // Raw PCM: payload is already signed 16-bit little-endian (decoded width).
        const size_t want = header.DecodedByteCount();
        if (payload_size < want) return false;
        std::memcpy(out_pcm.data(), payload, want);
        return true;
    }

    // ADPCM: payload is frameCount bytes (one byte per stereo frame of 4-bit ADPCM).
    const long expected_bytes =
        IlsfAdpcmDecoder::EncodedByteCount(header.channels, header.frame_count);
    if (payload_size < static_cast<size_t>(expected_bytes)) return false;

    IlsfAdpcmDecoder decoder(header.channels);
    decoder.Decode(payload, payload_size, out_pcm.data(), out_pcm.size(),
                   static_cast<int>(header.frame_count));
    return true;
}

} // namespace igi
