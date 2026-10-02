#include "Extension/Profile/profile_internal.h"
#include <cmath>
#include <type_traits>

namespace dingosdk::profile {
using namespace detail;
ParkChoices park_choices(const Snapshot& s) {
    ParkChoices result;
    const auto it = s.settings.find("parks");
    if (it == s.settings.end()) return result;
    require(it->is_object(), "Park settings must be an object");
    for (unsigned i = 0; i < result.size(); ++i) {
        const auto field = it->find(std::string(park_lots[i].key));
        if (field == it->end()) continue;
        require(field->is_string(), "Park choice must be a string");
        result[i] = field->get<std::string>();
        require(valid_park(i, result[i]), "Unknown park layout for this lot");
    }
    return result;
}

WorldLayerChoices world_layer_choices(const Snapshot& s) {
    auto result = default_world_layers();
    const auto it = s.settings.find("world_layers");
    if (it == s.settings.end()) return result;
    require(it->is_object(), "World layers must be an object");
    for (unsigned i = 0; i < world_layers().size(); ++i) {
        const auto field = it->find(std::string(world_layers()[i].key));
        if (field == it->end()) continue;
        require(field->is_string(), "World layer choice must be a string");
        result[i] = field->get<std::string>();
        require(valid_world_layer_mode(result[i]), "Unknown world layer mode");
    }
    return result;
}

WorldControls world_controls(const Snapshot& s) {
    WorldControls c;
    const auto it = s.settings.find("world_controls");
    if (it == s.settings.end()) return c;
    require(it->is_object(), "World controls must be an object");
    const auto number = [&](const char* key, auto& dest) {
        const auto field = it->find(key);
        if (field == it->end() || field->is_null()) return;
        require(field->is_number(), "World control must be numeric or null");
        const double n = field->get<double>();
        require(std::isfinite(n) && n >= -1 && n <= 360, "Invalid world control value");
        if constexpr (std::is_integral_v<std::remove_reference_t<decltype(dest)>>)
            require(std::floor(n) == n, "World control mode must be an integer");
        dest = static_cast<std::remove_reference_t<decltype(dest)>>(n);
    };
    number("traffic", c.traffic); number("pedestrians", c.pedestrians); number("fog", c.fog);
    number("fog_distance", c.fog_distance); number("sky_brightness", c.sky_brightness);
    number("clouds", c.clouds); number("wind_strength", c.wind_strength); number("wind_direction", c.wind_direction);
    const auto detail=it->find("atmosphere");
    if (detail!=it->end()) {
        require(detail->is_object(),"Atmosphere settings must be an object");
        for (const auto& control : atmosphere_controls) {
            const auto saved=detail->find(control.key);
            if (saved==detail->end() || saved->is_null()) continue;
            AtmosphereValue v;
            if (control.type==AtmosphereType::texture) {
                require(saved->is_string(),"Atmosphere texture must be an asset name");
                v.texture=saved->get<std::string>();
            } else if (control.lanes==1) {
                require(saved->is_number() || (control.type==AtmosphereType::toggle && saved->is_boolean()),"Atmosphere value must be numeric");
                v.number[0]=saved->is_boolean() ? (saved->get<bool>() ? 1 : 0) : saved->get<double>();
            } else {
                require(saved->is_array() && saved->size()==control.lanes,"Atmosphere vector has the wrong size");
                for (unsigned i=0;i<control.lanes;++i) {
                    require((*saved)[i].is_number(),"Atmosphere vector must contain numbers");
                    v.number[i]=(*saved)[i].get<double>();
                }
            }
            // Validate doubles before float/uint conversion, including enums.
            require(valid_atmosphere_value(control,v),"Invalid atmosphere control value");
            c.atmosphere.emplace(control.key,std::move(v));
        }
    }
    require(valid_world_controls(c), "World control outside supported range");
    return c;
}

}
