#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Retail magic-object table — C++ port of open-igi src/OpenIGI.Game/World/
// MagicObjectDefinition.cs + MagicObjectRegistry.cs (igi2.pdb evidence:
// DefineMagicObj native 0x4C4520, attachment walk 0x4DFA50).
//
// The table maps a model attachment's NAME to the object the game hangs off it:
// { name -> model, task type }. Retail fills it by running magicobj/magicobj.qvm,
// which is nothing but 387 DefineMagicObj(name, model, TASKTYPE_*) calls; in most
// entries name == model. The original appends unconditionally and its lookup takes
// the first match for a repeated name — reproduced here.
//
// Editor integration: when LoadAttachmentsRecursive resolves an ATTA child whose
// name hits this table, the spawned child carries the DEFINED model (which may
// differ from the attachment name) plus the task-type metadata, tagged so the
// tree/inspector can show "spawned by magic-object table".

namespace igi {

struct MagicObjectDefinition {
    std::string name;            // attachment name this definition answers to
    std::string model;           // model the spawned object draws
    std::string task_type_name;  // TASKTYPE_* constant from the script ("" if unknown)
    int task_type_id = 0;        // id allocated in first-seen order (see note below)
};

class MagicObjectRegistry {
public:
    static MagicObjectRegistry& Get();

    // Loads magicobj/magicobj.qvm from the IGI root (same path convention as the
    // existing deathzone/magicobj id scans) and registers every DefineMagicObj call.
    // A missing or invalid script is NOT an error: the registry stays empty and the
    // editor keeps its current attachment behavior (open-igi Load() semantics).
    // Idempotent; call again after Clear() to reload.
    void EnsureLoaded();

    // Parses decompiled QSC text (as produced by QVM_DecompileToString on
    // magicobj.qvm) and registers every DefineMagicObj("name", "model", TASKTYPE_X)
    // line. Exposed for tests and for callers that already hold the text.
    // Returns the number of definitions registered by THIS call.
    int LoadFromDecompiledText(const std::string& decompiled_qsc);

    void Clear();

    bool TryGet(const std::string& attachment_name, MagicObjectDefinition& out) const;
    int Count() const;
    bool Loaded() const { return loaded_; }

    // Task-type ids: the original allocates ids at startup in class-registration order
    // (0x4B8810) so the number never appears in data files. Divergence, deliberate and
    // matching open-igi's fallback: ids here are allocated in the order the constants
    // are FIRST SEEN in the script text, kept alongside their names.
    std::string TaskTypeName(int task_type_id) const;

private:
    MagicObjectRegistry() = default;

    // Case-insensitive keyed lookup, mirroring open-gi's OrdinalIgnoreCase dictionary.
    std::unordered_map<std::string, MagicObjectDefinition> by_name_;
    std::unordered_map<int, std::string> task_type_names_;
    bool loaded_ = false;
};

} // namespace igi
