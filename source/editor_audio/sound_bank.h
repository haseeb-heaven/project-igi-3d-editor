#pragma once
#include "ilsf_sound.h"

#include <map>
#include <string>
#include <vector>

// Sound bank — C++ port of open-igi src/OpenIGI.Engine/Audio/SoundBank.cs +
// SoundEntry.cs + SoundName.cs.
//
// Every sound the game can name, gathered from the directories and archives the
// original mounts. The original mounts a sound directory with 0x4E68D0; its
// callback 0x4E68F0 routes .WAV to the ILSF loader 0x495F70 and .RES to the
// archive reader (whose entries are themselves sounds); anything else is ignored.
//
// Sounds are filed under the bare stem of their path — "ak47_1", not
// "LOCAL:common/sounds/ak47_1.wav" — because that is all a script ever says.
//
// FIRST MOUNT WINS: the original's insert does not check for an existing entry,
// but its loader looks the key up first and reports "SOUND - File %s already
// loaded!" without replacing anything. A mission's own sounds cannot shadow a
// common one of the same name.

namespace igi {

// Bare-stem upper-cased key from a path ("dir/ak47_1.wav" -> "AK47_1").
// Empty when the path has no usable stem (leading dot or no extension).
std::string SoundNameToKey(const std::string& path);

struct SoundBankEntry {
    std::string name;      // lookup key, upper-cased bare stem
    std::string origin;    // path/archive entry the sound was found at
    IlsfSoundHeader header;
    // Decoded signed 16-bit PCM for a resident sound; empty for a streamed one
    // (streamed sounds keep only their path, exactly as the original does —
    // record +40 stores the file name and nothing is allocated).
    std::vector<int16_t> pcm;
    bool resident = false;
};

class SoundBank {
public:
    int Count() const { return static_cast<int>(sounds_.size()); }

    // Finds a sound by the name a script uses, case-insensitively.
    bool TryGet(const std::string& name, SoundBankEntry& out) const;

    // Mounts every .wav (ILSF) and .res (archive of ILSF entries) under a disk
    // directory such as "<IGI>/common/sounds". A directory that is not there is
    // not an error — most missions ship no sounds of their own. Returns sounds added.
    int MountDirectory(const std::string& directory);

    // Mounts one .res archive via RES_ForEachEntry; each entry that IsIlsfs is added
    // by its entry name. Returns sounds added.
    int MountArchive(const std::string& path);

    void Clear();

private:
    bool Add(const std::string& path_or_key_hint, const uint8_t* data, size_t size);

    std::map<std::string, SoundBankEntry> sounds_; // keyed upper-cased
    std::vector<std::string> mounted_;
};

} // namespace igi
