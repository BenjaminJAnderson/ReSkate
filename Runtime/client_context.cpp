#include "runtime_internal.h"
#include <algorithm>

namespace dingosdk::runtime::detail {
namespace {
// A registered sublevel's asset path: terminated within 256 bytes, of path
// characters only. Copied up to the end of a page at a time (the bytes after
// the terminator may belong to a page that is not mapped), not a system call
// per character.
bool asset_name(std::uintptr_t address, std::string& result) {
    constexpr std::size_t page = 0x1000;
    std::array<char, 256> text{};
    result.clear();
    for (std::size_t done = 0; done < text.size();) {
        const auto at = address + done;
        const auto size = std::min<std::size_t>(text.size() - done, page - at % page);
        if (!memory::peek_bytes(at, text.data() + done, size)) return false;
        for (auto i = done; i < done + size; ++i) {
            const char c = text[i];
            if (!c) return true;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '/' || c == '_' || c == '-' || c == '.' || c == ' ')) return false;
            result += c;
        }
        done += size;
    }
    return false;
}
}
ClientContent read_client_content(std::uintptr_t base, std::uintptr_t client, const std::string& requested) {
    ClientContent output;
    std::uintptr_t object{}, vtable{}, listener{}, begin{}, end{}, capacity{}, check{};
    if (read(base + rt::sublevel_list, object) && object >= 0x10000 && object <= highest - 0x78 &&
        read(object, vtable) && vtable == base + rt::sublevel_list_vtable &&
        read(object + 0x40, listener) && listener == base + rt::sublevel_listener_vtable &&
        read(object + 0x48, begin) && read(object + 0x50, end) && read(object + 0x58, capacity) &&
        begin <= end && end <= capacity && !(begin % 8) && !(end % 8) && !(capacity % 8) &&
        (capacity - begin) / 8 <= 128 && end <= highest && (!end || begin >= 0x10000)) {
        bool okay = true;
        for (auto item = begin; item < end; item += 8) {
            std::uintptr_t pointer{};
            if (!read(item, pointer) || pointer < 0x10000 || pointer > highest - 256) { okay = false; break; }
            std::string asset;
            if (!asset_name(pointer, asset) || asset.size() < 7 || !equals(asset.substr(0, 7), "levels/")) { okay = false; break; }
            output.requested_sublevel_present = output.requested_sublevel_present || equals(asset, requested);
        }
        output.sublevels_available = okay && read(object + 0x48, check) && check == begin &&
            read(object + 0x50, check) && check == end && read(object + 0x58, check) && check == capacity &&
            read(base + rt::sublevel_list, check) && check == object;
        if (output.sublevels_available) output.sublevel_count = static_cast<std::uint32_t>((end - begin) / 8);
        else output.requested_sublevel_present = false;
    }
    std::uintptr_t context{}, manager{};
    std::uint32_t offset{};
    if (!read(client + 8, context) || context < 0x10000 || context > highest - 0x1000100 ||
        !read(base + engine::context_player_manager_offset, offset) || offset > 0x1000000 || !read(context + offset, manager)) return output;
    output.player_manager_present = manager != 0;
    if (manager < 0x10000 || manager > highest - 0x4d8 || !read(manager, vtable) || vtable != base + engine::local_player_manager_vtable ||
        !read(manager + 0x4c8, begin) || !read(manager + 0x4d0, end) || begin > end || begin % 8 || end % 8 ||
        (end - begin) / 8 > 64 || end > highest || (!begin && end) || (begin && begin < 0x10000)) return output;
    std::uint32_t references{};
    for (auto item = begin; item < end; item += 8) {
        std::uintptr_t player{}, handle{}, live{}, entity{};
        if (!read(item, player) || player < 0x10000 || player > highest - 0xc0 ||
            !read(player + 0xb0, handle) || !read(player + 0xb8, entity)) return output;
        if (handle && read(handle, live) && live && entity) ++references;
    }
    if (!read(manager + 0x4c8, check) || check != begin || !read(manager + 0x4d0, check) || check != end ||
        !read(context + offset, check) || check != manager) return output;
    output.players_available = true;
    output.local_player_count = static_cast<std::uint32_t>((end - begin) / 8);
    output.controllable_reference_count = references;
    return output;
}
bool native_context(std::uintptr_t base, std::uintptr_t client, DWORD game_type) {
    std::uintptr_t context{}, queue{}, connection{}, allocator{}, request_entry{}, server{}, server_vtable{};
    DWORD bus_offset{}, type_offset{}, connection_offset{}, context_type{};
    std::uint16_t variant_offset{};
    for (const auto field : {0x20u, 0x50u, 0x68u, 0x80u}) {
        if (!read(base + rt::level_description_layout + field, variant_offset) || variant_offset > 0x1000 ||
            (field == 0x20 && variant_offset % 8)) return false;
    }
    std::uint8_t active{};
    if (!read(base + engine::client_vtable + 0x70, request_entry) || request_entry != base + rt::local_level_request ||
        !read(base + engine::ui_allocator, allocator) || allocator < 0x10000 || allocator > highest - 0x40 ||
        !read(client + 8, context) || context < 0x10000 ||
        !read(base + rt::context_message_bus_offset, bus_offset) || !read(base + engine::context_type_offset, type_offset) ||
        !read(base + engine::context_client_connection_offset, connection_offset) ||
        bus_offset > 0x1000000 || type_offset > 0x1000000 || connection_offset > 0x1000000 ||
        context > highest - 0x1000100 ||
        !read(context + type_offset, context_type) || context_type != 0xbf0f9789 ||
        !read(context + bus_offset + 0x38, active) || active != 1 ||
        !read(context + bus_offset + 0x28, queue) || queue < 0x10000 || queue > highest - 0x40 ||
        !read(context + connection_offset, connection)) return false;
    const bool local_server = read(base + engine::game_server, server) && server >= 0x10000 && server <= highest - 0x1c8 &&
        read(server, server_vtable) && server_vtable == base + engine::server_vtable;
    std::uintptr_t controller{}, controller_vtable{}, owned_server{};
    const bool owned_local_server = local_server && read(client + 0xd0, controller) && controller >= 0x10000 &&
        controller <= highest - 0x4c8 && read(controller, controller_vtable) && controller_vtable == base + engine::controller_vtable &&
        read(controller + 0x4c0, owned_server) && owned_server == server;
    // A Hosted context must have both its connection and this process's native
    // game-server instance. Joined/live-server contexts never qualify.
    return (game_type == 0 && (!connection || owned_local_server)) ||
        (game_type == 1 && owned_local_server && connection >= 0x10000 && connection <= highest - 0x90);
}
}
