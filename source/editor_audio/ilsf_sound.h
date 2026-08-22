#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ILSF sound format — C++ port of open-igi src/OpenIGI.Formats/Sound/IlsfHeader.cs,
// IlsfFormat.cs and IlsfAdpcmDecoder.cs (igi2.pdb evidence: original loader 0x495F70,
// ADPCM decoder 0x496280, state init 0x496420).
//
// Twenty-byte header every ILSF sound starts with:
//   +0  char[4] "ILSF"
//   +4  u16     format          — 0=StreamedPcm, 1=ResidentPcm, 2=StreamedAdpcm, 3=ResidentAdpcm
//                                 (bit 0: resident vs streamed, bit 1: ADPCM vs PCM)
//   +6  u16     bitsPerSample   — the *decoded* width; 16 for every shipped sound
//   +8  u16     channels        — 1 or 2
//   +10 u16     reserved        — zero in every shipped file
//   +12 u32     sampleRate      — mostly 22050 (retail output rate, Snd_Init 0x4952F0),
//                                 but 44100 and 11025 also ship
//   +16 u32     frameCount      — samples per channel, NOT bytes (verified: streamed PCM
//                                 stereo payload == frameCount*4 bytes; streamed ADPCM
//                                 stereo == frameCount bytes, one byte per stereo frame
//                                 of four-bit ADPCM)
//
// All four format codes ship. Both streamed codes decode exactly like their resident
// counterparts — the distinction is runtime storage policy.

namespace igi {

enum class IlsfFormat : uint16_t {
    StreamedPcm = 0,   // retail ambience beds
    ResidentPcm = 1,   // ordinary sound effects inside sounds.res
    StreamedAdpcm = 2, // retail music tracks
    ResidentAdpcm = 3,
};

struct IlsfSoundHeader {
    static constexpr int kSize = 20;
    static constexpr uint32_t kRetailSampleRate = 22050;

    IlsfFormat format = IlsfFormat::StreamedPcm;
    uint16_t bits_per_sample = 16; // decoded width
    uint16_t channels = 1;
    uint16_t reserved = 0;
    uint32_t sample_rate = kRetailSampleRate;
    uint32_t frame_count = 0;

    bool IsAdpcm() const {
        return format == IlsfFormat::StreamedAdpcm || format == IlsfFormat::ResidentAdpcm;
    }
    bool IsStreamed() const {
        return format == IlsfFormat::StreamedPcm || format == IlsfFormat::StreamedAdpcm;
    }
    // Decoded PCM byte size (ADPCM expands to bits_per_sample width).
    uint64_t DecodedByteCount() const {
        return static_cast<uint64_t>(frame_count) * channels * (bits_per_sample / 8);
    }
};

// True when the bytes start with the ILSF magic.
bool IsIlsf(const uint8_t* data, size_t size);

// Parses the twenty-byte header. Returns false when the magic or size check fails.
bool ParseIlsfHeader(const uint8_t* data, size_t size, IlsfSoundHeader& out);

// ── IMA/DVI ADPCM decoder (IlsfAdpcmDecoder.cs, 0x496280/0x496420) ──────────
// Plain IMA ADPCM: index table {-1,-1,-1,-1,2,4,6,8}x2, standard 89-entry step
// table (both verified byte-for-byte against the image). Two non-obvious details:
// nibbles are taken HIGH first within a byte, and channels interleave PER SAMPLE —
// a stereo frame is one byte holding left in the high nibble, right in the low.
class IlsfAdpcmDecoder {
public:
    static constexpr int kMaximumChannels = 2;
    static constexpr int kMaximumStepIndex = 88;

    explicit IlsfAdpcmDecoder(int channels);

    int Channels() const { return channels_; }
    bool HasPendingNibble() const { return has_pending_; }

    // Encoded byte count for frameCount frames ((nibbles+1)/2).
    static long EncodedByteCount(int channels, long frame_count);

    // Decodes frame_count frames into destination (frame_count*channels samples,
    // channel-interleaved). Returns source bytes consumed. State carries across
    // calls so a stream decodes in arbitrary forward slices.
    int Decode(const uint8_t* source, size_t source_size, int16_t* destination,
               size_t dest_samples, int frame_count);

private:
    int16_t Step(int channel, int nibble);

    int channels_;
    int predictor_[kMaximumChannels] = {0, 0};
    int step_index_[kMaximumChannels] = {0, 0};
    uint8_t pending_ = 0;
    bool has_pending_ = false;
};

// Decodes one complete resident ILSF payload into signed 16-bit PCM at the header's
// decoded width. Returns false for streamed/unknown forms or truncated payloads.
bool DecodeIlsfPcm(const uint8_t* data, size_t size, std::vector<int16_t>& out_pcm);

} // namespace igi
