#pragma once

#include "renderer/graph_writer.h"
#include "level/level_objects.h"

#include <glm/glm.hpp>

#include <optional>

enum class GraphCameraTargetKind {
    Object,
    GraphOrigin,
    GraphNode,
};

struct GraphCameraTarget {
    GraphCameraTargetKind kind = GraphCameraTargetKind::Object;
    glm::dvec3 position{0.0};
    int node_id = -1;
};

// Resolve the graph task referenced by a selected editor object. HumanSoldier
// stores the relationship on its nested HumanAI task, while AIGraph and
// HumanAI selections carry it directly. Keep this lookup independent of the
// renderer so F11 and tests use the same authored-object relationship.
inline std::string FindRelatedGraphTaskId(const std::vector<LevelObject>& objects,
                                           int selected_object_index) {
    if (selected_object_index < 0 ||
        selected_object_index >= static_cast<int>(objects.size())) return {};
    const LevelObject& selected = objects[selected_object_index];
    if (selected.deleted) return {};
    if (selected.type == "AIGraph") return selected.taskId;
    if (selected.aiGraphTaskId >= 0) return std::to_string(selected.aiGraphTaskId);
    if (!selected.graphId.empty()) return selected.graphId;

    for (int child_index : selected.childrenIndices) {
        if (child_index < 0 || child_index >= static_cast<int>(objects.size())) continue;
        const LevelObject& child = objects[child_index];
        if (!child.deleted && child.type == "HumanAI" && child.aiGraphTaskId >= 0)
            return std::to_string(child.aiGraphTaskId);
    }
    return {};
}

inline std::optional<glm::dvec3> FindRelatedGraphOrigin(
    const std::vector<LevelObject>& objects, int selected_object_index) {
    const std::string graph_task_id =
        FindRelatedGraphTaskId(objects, selected_object_index);
    if (graph_task_id.empty()) return std::nullopt;
    for (const LevelObject& candidate : objects) {
        if (!candidate.deleted && candidate.type == "AIGraph" &&
            candidate.taskId == graph_task_id)
            return candidate.pos;
    }
    return std::nullopt;
}

// Graph node coordinates are local to the AIGraph task. F11 therefore uses
// the selected node plus the overlay world offset; with no selected node it
// visits the graph origin, and otherwise preserves object teleport behavior.
inline GraphCameraTarget ResolveGraphCameraTarget(
    const GraphFile& graph,
    bool graph_visible,
    int selected_node_id,
    const glm::dvec3& graph_offset,
    const glm::dvec3& object_position,
    const std::optional<glm::dvec3>& related_graph_origin = std::nullopt) {
    if (graph_visible && selected_node_id >= 0) {
        if (const GraphNode* node = GRAPH_FindNode(graph, selected_node_id)) {
            return {GraphCameraTargetKind::GraphNode,
                    graph_offset + glm::dvec3(node->x, node->y, node->z),
                    node->id};
        }
    }

    if (graph_visible && graph.valid) {
        return {GraphCameraTargetKind::GraphOrigin, graph_offset, -1};
    }

    // F11 is also useful before the overlay is opened.  A selected soldier
    // still has a resolved AIGraph task, so use that task's world origin
    // instead of silently teleporting to the soldier itself.
    if (!graph_visible && related_graph_origin.has_value()) {
        return {GraphCameraTargetKind::GraphOrigin, *related_graph_origin, -1};
    }

    return {GraphCameraTargetKind::Object, object_position, -1};
}
