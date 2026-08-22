// Uses the retail ingamemenu.qvm decompile preserved under docs/reference/.
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "renderer/menu_qvm.h"
namespace {
std::string LoadDecompiled() {
    std::ifstream f("docs/reference/ingamemenu_decompiled.qsc");
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
} // namespace
int main_if_not_gtest() {
    std::string s = LoadDecompiled();
    igi::MenuDef def = igi::ParseMenuQsc(ss.str());
    printf("valid=%d screens=%zu manager_id=%d res=%s\n",
           def.valid, def.screens.size(), def.manager_id, def.resource_path.c_str());
    for (const auto& sc : def.screens) {
        printf("  screen %d [%s] bg='%s' rect=(%d,%d,%d,%d) items=%zu\n",
               sc.id, sc.title.c_str(), sc.background.c_str(),
               sc.rect.x0, sc.rect.y0, sc.rect.x1, sc.rect.y1, sc.items.size());
        for (const auto& it : sc.items)
            if (!it.is_frame && !it.text.empty())
                printf("      text '%s' font=%s colour=%d click=[%s]\n",
                       it.text.c_str(), it.font.c_str(), it.colour_index,
                       it.click_script.substr(0,50).c_str());
    }
}
