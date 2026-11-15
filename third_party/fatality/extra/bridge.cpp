#include "menu.h"
#include "../cfg.h"
namespace menu { menu_t men; }
extern std::shared_ptr<evo::gui::control> make_stacked_groupboxes(uint64_t, const evo::ren::vec2&, const std::vector<std::shared_ptr<evo::gui::control>>&);
namespace menu::tools {
std::shared_ptr<evo::gui::control> make_stacked_groupboxes(uint64_t id, const evo::ren::vec2& size, const std::vector<std::shared_ptr<evo::gui::control>>& groups) {
    return ::make_stacked_groupboxes(id, size, groups);
}
}
