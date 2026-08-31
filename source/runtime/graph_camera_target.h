#pragma once

#include "renderer/graph_writer.h"

#include <glm/glm.hpp>

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

// Graph node coordinates are local to the AIGraph task. F11 therefore uses
// the selected node plus the overlay world offset; with no selected node it
// visits the graph origin, and otherwise preserves object teleport behavior.
inline GraphCameraTarget ResolveGraphCameraTarget(
    const GraphFile& graph,
    bool graph_visible,
    int selected_node_id,
    const glm::dvec3& graph_offset,
    const glm::dvec3& object_position) {
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

    return {GraphCameraTargetKind::Object, object_position, -1};
}