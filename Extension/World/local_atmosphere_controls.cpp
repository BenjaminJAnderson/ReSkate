#include "local_atmosphere_controls.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/atmosphere.h"
#include "Engine/Core/Platform/memory.h"
#include <cstring>

namespace dingosdk::profile_runtime {
namespace {
using namespace addr::atmosphere;
using Bytes = std::array<std::byte,80>;
bool bytes(const EnvironmentNode& n, const AtmosphereBinding& b, Bytes& value) {
    return dingosdk::memory::read_bytes(n.component+b.cache,value.data(),b.size);
}
bool same(const Bytes& a, const Bytes& b, unsigned size) {
    return std::memcmp(a.data(),b.data(),size) == 0;
}
template<class T> T unpack(const Bytes& b, unsigned offset = 0) {
    T v{}; std::memcpy(&v,b.data()+offset,sizeof(v)); return v;
}
template<class T> void pack(Bytes& b, T v, unsigned offset = 0) {
    std::memcpy(b.data()+offset,&v,sizeof(v));
}
std::uintptr_t pointer(const Bytes& b) { return unpack<std::uintptr_t>(b) & ~std::uintptr_t{4}; }
using AssignReference = void(*)(std::uintptr_t*, std::uintptr_t);
std::shared_ptr<std::uintptr_t> retain(std::uintptr_t value) {
    if (!value) return {};
    const auto assign = reinterpret_cast<AssignReference>(local_runtime().base+reference_assign);
    auto result = std::shared_ptr<std::uintptr_t>(new std::uintptr_t{}, [assign](std::uintptr_t* p) {
        assign(p,0); delete p;
    });
    assign(result.get(),value);
    return result;
}
std::string texture_name(std::uintptr_t value) {
    std::string name;
    if (!value) return "none";
    if (!identifier(reinterpret_cast<const void*>(value+0x18),name) || name.empty() || name.size()>512) return {};
    return name;
}
bool mask(const EnvironmentNode& n, const AtmosphereBinding& b, std::uint32_t& value) {
    if (n.kind == 2) { std::uint16_t v{}; if (!read(n.component+b.mask_offset,v)) return false; value=v; return true; }
    return read(n.component+b.mask_offset,value);
}
bool submit(EnvironmentNode& n, const AtmosphereBinding& b, const Bytes& value, bool enabled) {
    auto property=world_control_runtime().property[n.kind];
    if (!property) return false;
    const auto p = b.texture ? reinterpret_cast<const void*>(pointer(value)) : value.data();
    const EnvironmentChange change{b.hash,0,p};
    property(n.component,&change);
    std::uint32_t flags{};
    if (!mask(n,b,flags)) return false;
    const auto desired=enabled ? flags|b.mask : flags&~b.mask;
    if (desired!=flags) {
        const EnvironmentChange flag_change{0xd421d5afu-b.group,0,&desired};
        property(n.component,&flag_change);
    }
    Bytes observed{};
    if (!bytes(n,b,observed) || !mask(n,b,flags)) return false;
    return (b.texture ? pointer(observed)==pointer(value) : same(observed,value,b.size)) &&
        ((flags&b.mask)!=0)==enabled;
}
AtmosphereValue decode(const AtmosphereControl& c, const Bytes& b) {
    AtmosphereValue value;
    if (c.type==AtmosphereType::texture) { value.texture=texture_name(pointer(b)); return value; }
    for (unsigned j=0;j<c.lanes;++j) {
        if (c.type==AtmosphereType::toggle) value.number[j]=unpack<std::uint8_t>(b,c.offset);
        else if (c.type==AtmosphereType::integer || c.type==AtmosphereType::enumeration)
            value.number[j]=unpack<std::uint32_t>(b,c.offset+j*4);
        else value.number[j]=unpack<float>(b,c.offset+j*4);
    }
    return value;
}
bool encode(const AtmosphereControl& c, const AtmosphereValue& value, Bytes& b) {
    if (!valid_atmosphere_value(c,value)) return false;
    if (c.type==AtmosphereType::texture) {
        const auto& textures=world_control_runtime().textures;
        const auto group=textures.find(c.property);
        if (group==textures.end()) return false;
        const auto it=group->second.find(value.texture);
        if (it==group->second.end()) return false;
        pack(b,*it->second); return true;
    }
    for (unsigned j=0;j<c.lanes;++j) {
        if (c.type==AtmosphereType::toggle) pack(b,static_cast<std::uint8_t>(value.number[j]),c.offset);
        else if (c.type==AtmosphereType::integer || c.type==AtmosphereType::enumeration)
            pack(b,static_cast<std::uint32_t>(value.number[j]),c.offset+j*4);
        else pack(b,static_cast<float>(value.number[j]),c.offset+j*4);
    }
    return true;
}
}
void collect_atmosphere_textures(const EnvironmentNode& n) {
    auto& runtime=world_control_runtime();
    for (unsigned i=0;i<atmosphere_bindings.size();++i) {
        const auto& b=atmosphere_bindings[i];
        if (b.kind!=n.kind || !b.texture) continue;
        std::uintptr_t ref{};
        // Sources are typed, authored sky slots. Each picker stays within the
        // same slot (e.g. cube environment maps never become 2D cloud textures).
        if (!read(n.data+b.asset_offset,ref)) continue;
        ref &= ~std::uintptr_t{4};
        const auto name=texture_name(ref);
        if (!ref || name.empty()) continue;
        auto& textures=runtime.textures[i];
        if (!textures.contains(name) && textures.size()<128) textures.emplace(name,retain(ref));
    }
}
bool apply_atmosphere_controls(EnvironmentNode& n, const WorldControls& choices, bool selected) {
    auto& r=world_control_runtime();
    if (!selected && n.atmosphere.empty()) return true;
    // Sample a component once per update. Reading each of the 194 fields
    // separately would add thousands of process-memory calls every tick.
    std::array<std::byte,0x300> snapshot{};
    unsigned extent=0;
    for (const auto& b:atmosphere_bindings) if (b.kind==n.kind)
        extent=std::max({extent,b.cache+b.size,b.mask_offset+(n.kind==2 ? 2u : 4u)});
    if (!extent || extent>snapshot.size() || !memory::read_bytes(n.component,snapshot.data(),extent)) return false;
    bool ok=true;
    for (unsigned i=0;i<atmosphere_bindings.size();++i) {
        const auto& b=atmosphere_bindings[i];
        if (b.kind!=n.kind) continue;
        Bytes current{};std::uint32_t flags{};
        std::memcpy(current.data(),snapshot.data()+b.cache,b.size);
        std::memcpy(&flags,snapshot.data()+b.mask_offset,n.kind==2 ? 2 : 4);
        auto owned=n.atmosphere.find(i);
        if (owned!=n.atmosphere.end() && (!same(current,owned->second.applied,b.size) || !(flags&b.mask))) {
            n.atmosphere.erase(owned); owned=n.atmosphere.end();
        }
        AtmosphereOwnership state = owned==n.atmosphere.end() ? AtmosphereOwnership{} : owned->second;
        if (owned==n.atmosphere.end()) {
            state.original=current; state.original_flag=(flags&b.mask)!=0;
        }
        Bytes desired=state.original;
        bool requested=false,valid=true;
        // Preserve the existing convenience commands. Exact property overrides
        // below take precedence, sharing the same baseline and reset ownership.
        for (unsigned legacy=0;legacy<environment_fields.size();++legacy) {
            const auto& f=environment_fields[legacy];
            if (f.kind!=n.kind || f.hash!=b.hash) continue;
            const float original=f.boolean ? static_cast<float>(unpack<std::uint8_t>(state.original)) : unpack<float>(state.original);
            const float v=environment_choice(legacy,choices,original,n);
            if (v>=0 && std::isfinite(v)) {
                requested=true;
                if (f.boolean) pack(desired,static_cast<std::uint8_t>(v!=0)); else pack(desired,v);
            }
        }
        for (const auto& c : dingosdk::atmosphere_controls) {
            if (c.property!=i) continue;
            const auto choice=choices.atmosphere.find(c.key);
            if (choice!=choices.atmosphere.end()) {
                requested=true;valid &= encode(c,choice->second,desired);
            }
        }
        if (b.texture && owned==n.atmosphere.end() && requested && valid) {
            // A null property argument reloads the authored slot. Only acquire
            // a null baseline if that native reset will restore it exactly.
            std::uintptr_t authored{};
            if (!pointer(current) && (!read(n.data+b.asset_offset,authored) || (authored&~std::uintptr_t{4}))) valid=false;
            if (valid) state.texture=retain(pointer(current));
        }
        const bool needs_update=requested || owned!=n.atmosphere.end();
        bool applied=false;
        if (valid && (requested || owned!=n.atmosphere.end())) {
            const bool enabled=requested || state.original_flag;
            if (same(current,desired,b.size) && ((flags&b.mask)!=0)==enabled) applied=true;
            else {
                applied=submit(n,b,desired,enabled);
                if (!applied) {
                    // A rejected notification must not leave a partially changed
                    // value or override flag behind.
                    submit(n,b,current,(flags&b.mask)!=0);
                }
            }
            if (applied) {
                if (requested) { state.applied=desired;n.atmosphere[i]=std::move(state); }
                else n.atmosphere.erase(i);
            }
        }
        ok &= valid && (!needs_update || applied);
        if (!selected) continue;
        Bytes observed=applied ? desired : current;
        for (unsigned index=0;index<dingosdk::atmosphere_controls.size();++index) {
            const auto& c=dingosdk::atmosphere_controls[index];
            if (c.property!=i) continue;
            auto& reading=r.model.atmosphere[index];
            const auto v=decode(c,observed);
            if (c.type!=AtmosphereType::texture && !std::all_of(v.number.begin(),v.number.end(),[](double a){return std::isfinite(a);})) continue;
            if (!reading.value) { reading.value=v; reading.applied=!requested || applied; }
            else { reading.varies |= *reading.value!=v; reading.applied &= !requested || applied; }
            if (c.type==AtmosphereType::texture) {
                auto& names=r.model.textures[index];
                if (const auto it=r.textures.find(i);it!=r.textures.end())
                    for (const auto& [name,_]:it->second) if (std::find(names.begin(),names.end(),name)==names.end()) names.push_back(name);
            }
        }
    }
    return ok;
}
}
