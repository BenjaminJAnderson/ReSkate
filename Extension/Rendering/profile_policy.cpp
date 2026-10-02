#include "Extension/Profile/profile_internal.h"

namespace dingosdk::profile {
using namespace detail;
GraphicsControls graphics_controls(const Snapshot& s) {
    GraphicsControls c;
    const auto it = s.settings.find("graphics");
    if (it == s.settings.end()) return c;
    require(it->is_object(), "Graphics settings must be an object");
    for (unsigned i = 0; i < graphics_keys.size(); ++i) {
        const auto field = it->find(graphics_keys[i]);
        if (field == it->end() || field->is_null()) continue;
        require(field->is_boolean(), "Graphics toggle must be Boolean or null");
        c.effects[i] = field->get<bool>() ? 1 : 0;
    }
    return c;
}

}
