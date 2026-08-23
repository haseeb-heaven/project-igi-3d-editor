#include "game_data_service.h"
#include "mcp_server.h"
#include "mcp_transport_http.h"
#include "mcp_transport_stdio.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
HANDLE g_http_stop_event = nullptr;

BOOL WINAPI HandleConsoleControl(DWORD control_type) {
    if ((control_type == CTRL_C_EVENT || control_type == CTRL_BREAK_EVENT ||
         control_type == CTRL_CLOSE_EVENT) && g_http_stop_event != nullptr) {
        SetEvent(g_http_stop_event);
        return TRUE;
    }
    return FALSE;
}

void WaitForHttpStop() {
    g_http_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_http_stop_event == nullptr) {
        std::string line;
        std::getline(std::cin, line);
        return;
    }
    SetConsoleCtrlHandler(HandleConsoleControl, TRUE);
    std::thread stdin_watcher([] {
        std::string line;
        if (std::getline(std::cin, line) && g_http_stop_event != nullptr) SetEvent(g_http_stop_event);
    });
    WaitForSingleObject(g_http_stop_event, INFINITE);
    if (stdin_watcher.joinable()) {
        CancelSynchronousIo(static_cast<HANDLE>(stdin_watcher.native_handle()));
        stdin_watcher.join();
    }
    SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
    CloseHandle(g_http_stop_event);
    g_http_stop_event = nullptr;
}
#else
void WaitForHttpStop() {
    std::string line;
    std::getline(std::cin, line);
}
#endif

}  // namespace

int main(int argc, char** argv) {
    bool stdio = true;
    std::filesystem::path project;
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--stdio") {
            stdio = true;
        } else if (argument == "--http") {
            stdio = false;
        } else if (argument == "--project" && index + 1 < argc) {
            project = argv[++index];
        } else if (argument == "--host" && index + 1 < argc) {
            host = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            try {
                const auto parsed = std::stoul(argv[++index]);
                if (parsed > 65535) throw std::out_of_range("port");
                port = static_cast<std::uint16_t>(parsed);
            } catch (...) {
                std::cerr << "invalid port\n";
                return 2;
            }
        } else {
            std::cerr << "usage: igi_mcp [--stdio] --project <game-root>\n";
            return 2;
        }
    }
    if (project.empty()) {
        std::cerr << "--project is required\n";
        return 2;
    }
    std::string error;
    const auto scope = mcp::ProjectScope::Open(project, error);
    if (!scope) {
        std::cerr << "invalid project root\n";
        return 2;
    }
    mcp::GameDataService service(*scope);
    mcp::McpServer server(service);
    if (!stdio) {
        mcp::HttpTransport transport;
        mcp::HttpEndpoint endpoint;
        mcp::HttpOptions options;
        options.host = host;
        options.port = port;
        if (!transport.Start(options, server, endpoint, error)) {
            std::cerr << "HTTP start failed\n";
            return 2;
        }
        std::cerr << "MCP HTTP listening on http://" << endpoint.host << ':' << endpoint.port
                  << "/mcp\nBearer token: " << endpoint.bearer_token << "\n";
        WaitForHttpStop();
        transport.Stop();
        return 0;
    }
    return mcp::StdioTransport().Run(server);
}
