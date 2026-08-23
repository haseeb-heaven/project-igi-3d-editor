#include "mcp_transport_stdio.h"

#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_server.h"

#include <iostream>
#include <string>

namespace mcp {

int StdioTransport::Run(McpServer& server) const {
    std::string line;
    line.reserve(kMaxJsonMessageBytes);
    for (;;) {
        line.clear();
        bool too_large = false;
        char character = '\0';
        while (std::cin.get(character)) {
            if (character == '\n') break;
            if (line.size() >= kMaxJsonMessageBytes) {
                too_large = true;
                while (std::cin.get(character) && character != '\n') {}
                break;
            }
            if (character != '\r') line.push_back(character);
        }
        if (!std::cin && line.empty() && !too_large) break;
        if (too_large) {
            std::cout << JsonStringify(MakeJsonRpcError(JsonValue(nullptr), kParseError,
                                                        "JSON message too large"))
                      << '\n' << std::flush;
            continue;
        }
        if (line.empty()) continue;
        JsonValue request;
        JsonError parse_error;
        if (!JsonParse(line, request, parse_error)) {
            std::cout << JsonStringify(MakeJsonRpcError(JsonValue(nullptr), kParseError,
                                                        "invalid JSON")) << '\n' << std::flush;
            continue;
        }
        const JsonValue response = server.Handle(request);
        if (!response.is_null()) std::cout << JsonStringify(response) << '\n' << std::flush;
    }
    return 0;
}

}  // namespace mcp
