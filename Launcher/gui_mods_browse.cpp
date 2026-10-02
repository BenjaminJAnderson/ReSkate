#include "gui_internal.h"
#include "gui_renderer.h"

#include "mod_manager.h"
#include "updater.h"

#include "Engine/Core/Log/logging.h"

#include <cstdio>
#include <format>

// The Mods panel's BROWSE page: packages from Thunderstore, their icons, and
// downloading and installing them on the panel's worker.
namespace dingosdk::launcher_gui::detail {
namespace {

namespace ts = thunderstore;

constexpr int listing_timeout_ms = 20000;
constexpr int download_timeout_ms = 30000;
constexpr std::size_t max_icons = 128;          // loaded icon textures, of the renderer's 160 slots
constexpr std::uint64_t max_icon_bytes = 6 * 1024 * 1024;

void log(logging::Level level, const std::string& message) {
    logging::write(level, logging::Channel::launcher, message);
}

std::string fetch(const std::wstring& url, std::uint64_t limit, const std::atomic<bool>& stop) {
    std::string body;
    launcher_update::http_stream(url, limit, listing_timeout_ms, [&](const char* data, DWORD size) {
        if (stop) throw std::runtime_error("stopped");
        body.append(data, size);
    });
    return body;
}

// The listing index and its chunks (what r2modman reads), else the plain listing.
std::vector<ts::Package> fetch_listing(const std::string& community, const std::atomic<bool>& stop) {
    try {
        const auto index = ts::parse_index(ts::gunzip(fetch(ts::listing_index_url(community), 1 << 20, stop), 4 << 20));
        std::vector<ts::Package> packages;
        for (const auto& url : index) {
            auto chunk = ts::parse_listing(ts::gunzip(fetch(url, 64ull << 20, stop), 256ull << 20));
            std::move(chunk.begin(), chunk.end(), std::back_inserter(packages));
        }
        return packages;
    } catch (const std::exception& failure) {
        if (stop) throw;
        log(logging::Level::warning, std::string("Thunderstore listing index unavailable (") + failure.what() +
            "); reading the plain listing");
    }
    return ts::parse_listing(ts::gunzip(fetch(ts::listing_url(community), 256ull << 20, stop), 256ull << 20));
}

fs::path icon_cache() {
    std::array<wchar_t, 32768> local{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (!length || length >= local.size()) return {};
    return fs::path(local.data()) / L"ReSkate" / L"thunderstore" / L"icons";
}

// Icon URLs end in Namespace-Name-1.2.3.png, so a file name keyed on that never goes stale.
std::wstring icon_file(std::string_view url) {
    const auto slash = url.rfind('/');
    std::string name(url.substr(slash == std::string_view::npos ? 0 : slash + 1));
    for (auto& ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_'))
            ch = '_';
    if (name.empty() || name.front() == '.') name.insert(name.begin(), 'i');
    return wide(name);
}

std::vector<unsigned char> read_bytes(const fs::path& path) {
    std::vector<unsigned char> bytes;
    FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"rb") || !file) return bytes;
    std::array<unsigned char, 65536> buffer{};
    for (std::size_t read; (read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0;)
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
    std::fclose(file);
    return bytes;
}

void icon_worker(Icons& icons) {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    const auto cache = icon_cache();
    std::atomic<bool> never{};
    for (;;) {
        std::string url;
        {
            std::unique_lock lock(icons.mutex);
            icons.wake.wait(lock, [&] { return icons.stop || !icons.queue.empty(); });
            if (icons.stop) break;
            url = std::move(icons.queue.front());
            icons.queue.pop_front();
        }
        Icons::Decoded decoded{url};
        try {
            const auto cached = cache.empty() ? fs::path() : cache / icon_file(url);
            auto bytes = cached.empty() ? std::vector<unsigned char>() : read_bytes(cached);
            if (bytes.empty()) {
                const auto body = fetch(wide(url), max_icon_bytes, never);
                bytes.assign(body.begin(), body.end());
                if (!cached.empty()) {
                    std::error_code error;
                    fs::create_directories(cache, error);
                    FILE* file{};
                    if (!_wfopen_s(&file, cached.c_str(), L"wb") && file) {
                        std::fwrite(bytes.data(), 1, bytes.size(), file);
                        std::fclose(file);
                    }
                }
            }
            UINT width{}, height{};
            if (!decode_image(bytes, ImVec2(S(96), S(96)), decoded.pixels, width, height)) decoded.pixels.clear();
            decoded.width = width;
            decoded.height = height;
        } catch (const std::exception&) {
            decoded.pixels.clear();
        }
        std::lock_guard lock(icons.mutex);
        icons.done.push_back(std::move(decoded));
    }
    if (com) CoUninitialize();
}

std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& ch : result) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    return result;
}

std::string count_text(std::uint64_t value) {
    if (value >= 1'000'000) return std::format("{:.1f}M", static_cast<double>(value) / 1e6);
    if (value >= 10'000) return std::format("{}k", value / 1000);
    if (value >= 1'000) return std::format("{:.1f}k", static_cast<double>(value) / 1e3);
    return std::to_string(value);
}

std::string size_text(std::uint64_t bytes) {
    if (bytes >= 1024ull * 1024 * 1024) return std::format("{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
    if (bytes >= 1024ull * 1024) return std::format("{:.0f} MB", static_cast<double>(bytes) / (1024.0 * 1024));
    return std::format("{} KB", std::max<std::uint64_t>(1, bytes / 1024));
}

std::string date_text(const std::string& iso) { return iso.substr(0, std::min<std::size_t>(iso.size(), 10)); }

void set_activity(ModsPanel& panel, std::string text) {
    std::lock_guard lock(panel.mutex);
    panel.activity = std::move(text);
}

// Downloads one package into Mods/.reskate-download, then installs it over any
// installed copy (which goes to the Recycle Bin, keeping its place in mods.json).
std::string install_package(ModsPanel& panel, const fs::path& root, const ts::Package& package) {
    const auto& version = package.latest();
    const auto folder = root / L".reskate-download";
    const auto archive = folder / (wide(package.full_name) + L".zip");
    std::error_code error;
    fs::create_directories(folder, error);
    struct Cleanup {
        fs::path folder, archive;
        ~Cleanup() { std::error_code ignored; fs::remove(archive, ignored); fs::remove(folder, ignored); }
    } cleanup{folder, archive};

    set_activity(panel, "Downloading " + package.title() + " v" + version.number +
        (version.file_size ? "  (" + size_text(version.file_size) + ")" : std::string()));
    panel.progress = 0;
    {
        FILE* file{};
        if (_wfopen_s(&file, archive.c_str(), L"wb") || !file)
            throw std::runtime_error("Could not write the download into the Mods folder.");
        std::uint64_t received = 0;
        const auto expected = version.file_size;
        try {
            launcher_update::http_stream(wide(version.download_url), expected ? expected + (1 << 20) : 8ull << 30,
                download_timeout_ms, [&](const char* data, DWORD size) {
                    if (panel.cancel) throw std::runtime_error("Install cancelled.");
                    if (std::fwrite(data, 1, size, file) != size)
                        throw std::runtime_error("Could not write the download (is the disk full?).");
                    received += size;
                    if (expected) panel.progress = static_cast<float>(static_cast<double>(received) / static_cast<double>(expected));
                });
        } catch (...) {
            std::fclose(file);
            throw;
        }
        std::fclose(file);
        if (expected && received != expected)
            throw std::runtime_error(std::format("The download of {} stopped early ({} of {}).", package.title(),
                size_text(received), size_text(expected)));
    }

    set_activity(panel, "Installing " + package.title() + " v" + version.number);
    panel.progress = 0;
    launcher_mods::InstallOptions options;
    options.folder = ts::folder_for(package.full_name);
    options.author = package.owner;
    options.require_content = true;
    return launcher_mods::install(root, archive, true, [&panel](float fraction) { panel.progress = fraction; },
        panel.cancel, options);
}

// Search, category and the sort order, pinned packages first like the site.
std::vector<const ts::Package*> visible_packages(const Store& store, const ts::Installed& installed) {
    const auto query = lower(store.search.data());
    std::vector<const ts::Package*> result;
    for (const auto& package : store.packages) {
        const bool have = installed.contains(ts::folder_for(package.full_name));
        // Deprecated and NSFW packages only show once installed.
        if ((package.deprecated || package.nsfw) && !have) continue;
        if (!store.category.empty() && !package.in_category(store.category)) continue;
        if (!query.empty() && lower(package.title()).find(query) == std::string::npos &&
            lower(package.owner).find(query) == std::string::npos &&
            lower(package.latest().description).find(query) == std::string::npos) continue;
        result.push_back(&package);
    }
    const auto order = [&](const ts::Package* a, const ts::Package* b) {
        if (a->pinned != b->pinned) return a->pinned;
        switch (store.sort) {
        case 1: if (a->downloads != b->downloads) return a->downloads > b->downloads; break;
        case 2: if (a->date_created != b->date_created) return a->date_created > b->date_created; break;
        case 3: if (a->rating != b->rating) return a->rating > b->rating; break;
        case 4: break;
        default: if (a->date_updated != b->date_updated) return a->date_updated > b->date_updated; break;
        }
        return lower(a->title()) < lower(b->title());
    };
    std::stable_sort(result.begin(), result.end(), order);
    return result;
}

constexpr std::array<const char*, 5> sort_names{"Last updated", "Most downloaded", "Newest", "Top rated", "Name"};

void draw_icon(ModsPanel& panel, const ts::Package& package, float size) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 end(start.x + size, start.y + size);
    auto* draw = ImGui::GetWindowDrawList();
    if (const auto id = package_icon(panel, package))
        draw->AddImageRounded(id, start, end, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(4));
    else
        draw->AddRectFilled(start, end, rgba(255, 255, 255, 0.06f), S(4));
    ImGui::Dummy(ImVec2(size, size));
}

// What installing `package` does now: install, update or reinstall.
std::string action_label(const ts::Package& package, const ts::Installed& installed) {
    const auto found = installed.find(ts::folder_for(package.full_name));
    if (found == installed.end()) return "INSTALL";
    if (ts::update_available(package, installed)) return "UPDATE TO v" + package.latest().number;
    return "REINSTALL";
}

} // namespace

void refresh_listing(ModsPanel& panel, double time, bool force) {
    auto& store = panel.store;
    // Once per launch, unless asked; a failed fetch retries after a minute.
    if (store.loading || (!force && store.fetched > -1e8 && (store.error.empty() || time - store.fetched < 60))) return;
    if (store.worker.joinable()) store.worker.join();
    store.fetched = time;
    store.loading = true;
    store.worker = std::thread([&store] {
        std::vector<ts::Package> packages;
        std::string error;
        const auto community = ts::community();
        try {
            packages = fetch_listing(community, store.stop);
            log(logging::Level::info, std::format("Thunderstore: {} package(s) in {}", packages.size(), community));
        } catch (const std::exception& failure) {
            error = failure.what();
            if (!store.stop) log(logging::Level::warning, "Thunderstore listing could not be read: " + error);
        }
        std::lock_guard lock(store.mutex);
        store.incoming = std::move(packages);
        store.incoming_error = std::move(error);
        store.arrived = true;
        store.loading = false;
    });
}

void collect_listing(ModsPanel& panel) {
    auto& store = panel.store;
    std::lock_guard lock(store.mutex);
    if (!store.arrived) return;
    store.arrived = false;
    if (store.incoming_error.empty()) {
        store.packages = std::move(store.incoming);
        store.loaded = true;
        store.error.clear();
    } else {
        store.error = std::move(store.incoming_error);
    }
    store.incoming.clear();
}

thunderstore::Installed installed_versions(const mods::ModList& list) {
    ts::Installed installed;
    for (const auto& entry : list.entries) installed[entry.mod.name] = entry.mod.version;
    return installed;
}

const thunderstore::Package* package_for(const Store& store, std::string_view folder) {
    for (const auto& package : store.packages)
        if (ts::folder_for(package.full_name) == folder) return &package;
    return nullptr;
}

std::vector<const thunderstore::Package*> updates(const Store& store, const thunderstore::Installed& installed) {
    std::vector<const ts::Package*> result;
    for (const auto& package : store.packages)
        if (ts::update_available(package, installed)) result.push_back(&package);
    return result;
}

void start_store_install(ModsPanel& panel, std::vector<thunderstore::Package> packages) {
    if (panel.installing || packages.empty()) return;
    if (panel.worker.joinable()) panel.worker.join();
    panel.installing = true;
    panel.cancel = false;
    panel.progress = 0;
    panel.message.clear();
    set_activity(panel, "Starting download...");
    const auto root = panel.root;
    panel.worker = std::thread([&panel, root, packages = std::move(packages)] {
        std::string name, error, note;
        std::size_t done = 0;
        for (const auto& package : packages) {
            try {
                const bool update = fs::exists(root / wide(ts::folder_for(package.full_name)));
                name = install_package(panel, root, package);
                ++done;
                log(logging::Level::info, std::format("Mod {} from Thunderstore: {} v{} into Mods\\{}",
                    update ? "updated" : "installed", package.full_name, package.latest().number, name));
                note = packages.size() > 1
                    ? std::format("Updated {} mods. Changes apply the next time Skate starts.", done)
                    : std::format("{} {} v{}. It loads the next time Skate starts.", update ? "Updated" : "Installed",
                                  package.title(), package.latest().number);
            } catch (const std::exception& failure) {
                error = package.title() + ": " + failure.what();
                log(logging::Level::warning, "Thunderstore install of " + package.full_name + " failed: " + failure.what());
                break;
            }
        }
        std::lock_guard lock(panel.mutex);
        panel.finished = true;
        panel.finished_name = name;
        panel.finished_error = error;
        panel.finished_conflict.clear();
        panel.finished_note = note;
        panel.finished_source.clear();
        panel.activity.clear();
        panel.installing = false;
    });
}

ImTextureID package_icon(ModsPanel& panel, const thunderstore::Package& package) {
    const auto& url = package.latest().icon;
    if (url.empty() || !g_renderer) return {};
    auto& icons = panel.icons;
    auto& entry = icons.entries[url];
    entry.used = ImGui::GetFrameCount();
    if (entry.id || entry.failed || entry.queued) return entry.id;
    entry.queued = true;
    {
        std::lock_guard lock(icons.mutex);
        icons.queue.push_back(url);
    }
    if (!icons.worker.joinable()) icons.worker = std::thread([&icons] { icon_worker(icons); });
    icons.wake.notify_one();
    return {};
}

void pump_icons(ModsPanel& panel) {
    auto& icons = panel.icons;
    std::vector<Icons::Decoded> done;
    {
        std::lock_guard lock(icons.mutex);
        done.swap(icons.done);
    }
    if (!g_renderer) return;
    for (auto& decoded : done) {
        auto& entry = icons.entries[decoded.url];
        entry.queued = false;
        if (decoded.pixels.empty()) { entry.failed = true; continue; }
        // Make room by dropping the icons drawn longest ago.
        std::size_t loaded = 0;
        for (const auto& [url, other] : icons.entries) loaded += other.id ? 1 : 0;
        while (loaded >= max_icons) {
            auto oldest = icons.entries.end();
            for (auto it = icons.entries.begin(); it != icons.entries.end(); ++it)
                if (it->second.id && (oldest == icons.entries.end() || it->second.used < oldest->second.used)) oldest = it;
            if (oldest == icons.entries.end() || oldest->second.used >= ImGui::GetFrameCount() - 1) break;
            g_renderer->release_texture(oldest->second.id);
            oldest->second.id = {};
            --loaded;
        }
        entry.id = g_renderer->upload_texture(decoded.pixels, decoded.width, decoded.height);
        if (!entry.id) entry.failed = true;
    }
}

void browse_page(const Fonts& fonts, ModsPanel& panel, float height, bool installing) {
    auto& store = panel.store;
    const auto installed = installed_versions(panel.list);
    pump_icons(panel);

    // ------------------------------------------------ filters
    const float top = ImGui::GetCursorPosY();
    ImGui::SetNextItemWidth(S(300));
    ImGui::InputTextWithHint("##search", "Search mods", store.search.data(), store.search.size());
    ImGui::SameLine();
    std::vector<std::string> categories;
    for (const auto& package : store.packages)
        for (const auto& category : package.categories)
            if (std::find(categories.begin(), categories.end(), category) == categories.end()) categories.push_back(category);
    std::sort(categories.begin(), categories.end());
    ImGui::SetNextItemWidth(S(170));
    if (ImGui::BeginCombo("##category", store.category.empty() ? "All categories" : store.category.c_str())) {
        if (ImGui::Selectable("All categories", store.category.empty())) store.category.clear();
        for (const auto& category : categories)
            if (ImGui::Selectable(category.c_str(), store.category == category)) store.category = category;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(170));
    if (ImGui::BeginCombo("##sort", sort_names[static_cast<std::size_t>(std::clamp(store.sort, 0, 4))])) {
        for (int i = 0; i < static_cast<int>(sort_names.size()); ++i)
            if (ImGui::Selectable(sort_names[static_cast<std::size_t>(i)], store.sort == i)) store.sort = i;
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    const float body = std::max(S(120), height - (ImGui::GetCursorPosY() - top));
    const float list_width = (ImGui::GetContentRegionAvail().x - S(16)) * 0.58f;
    const auto packages = visible_packages(store, installed);

    // ------------------------------------------------ list
    ImGui::BeginChild("##store_list", ImVec2(list_width, body), ImGuiChildFlags_Borders);
    const auto centred = [](const char* text) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
    };
    if (!store.loaded) {
        if (store.loading || store.error.empty()) centred("Loading mods from Thunderstore...");
        else {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::danger), "Thunderstore could not be reached: %s",
                store.error.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Try again")) refresh_listing(panel, ImGui::GetTime(), true);
        }
    } else if (store.packages.empty()) {
        centred("No mods on Thunderstore yet. Made one? Package it with a manifest.json, icon.png and README.md "
                "and upload it to thunderstore.io/c/reskate.");
    } else if (packages.empty()) {
        centred("No mods match your search.");
    }
    const float row = S(68);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(packages.size()), row + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& package = *packages[static_cast<std::size_t>(i)];
            ImGui::PushID(package.full_name.c_str());
            const auto start = ImGui::GetCursorPos();
            if (ImGui::Selectable("##row", store.selected == package.full_name, ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0, row)))
                store.selected = package.full_name;
            const float right = ImGui::GetWindowContentRegionMax().x;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), S(3)));
            ImGui::SetCursorPos(ImVec2(start.x + S(6), start.y + S(10)));
            draw_icon(panel, package, S(48));
            const float text_x = start.x + S(66);
            ImGui::SetCursorPos(ImVec2(text_x, start.y + S(7)));
            ImGui::PushFont(fonts.bold);
            ImGui::TextUnformatted(package.title().c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("by %s", package.owner.c_str());
            const auto found = installed.find(ts::folder_for(package.full_name));
            const char* badge = found == installed.end() ? nullptr
                : ts::update_available(package, installed) ? "UPDATE" : "INSTALLED";
            if (badge) {
                const float width = ImGui::CalcTextSize(badge).x;
                ImGui::SameLine(right - width - S(6));
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(std::string_view(badge) == "UPDATE" ? color::blue : color::good),
                    "%s", badge);
            }
            ImGui::SetCursorPosX(text_x);
            auto line = package.latest().description;
            std::replace_if(line.begin(), line.end(), [](char ch) { return ch == '\n' || ch == '\r' || ch == '\t'; }, ' ');
            ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetWindowPos().x + right - S(6), ImGui::GetCursorScreenPos().y + row), true);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopClipRect();
            ImGui::SetCursorPosX(text_x);
            ImGui::TextDisabled("v%s  /  %s downloads  /  %s", package.latest().number.c_str(),
                count_text(package.downloads).c_str(), date_text(package.date_updated).c_str());
            ImGui::PopStyleVar();
            // An item at the row's end, so the cursor never extends the list on its own.
            ImGui::SetCursorPos(ImVec2(start.x, start.y + row));
            ImGui::Dummy(ImVec2(1, 0));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // ------------------------------------------------ details
    ImGui::SameLine(0, S(16));
    ImGui::BeginChild("##store_details", ImVec2(0, body), ImGuiChildFlags_Borders);
    const ts::Package* selected = nullptr;
    for (const auto& package : store.packages)
        if (package.full_name == store.selected) selected = &package;
    if (!selected) {
        ImGui::Spacing();
        ImGui::TextDisabled(store.packages.empty() ? "" : "Select a mod to see its details.");
    } else {
        const auto& package = *selected;
        const auto& version = package.latest();
        // Icon and name side by side, then the actions, so INSTALL never needs scrolling to.
        draw_icon(panel, package, S(80));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts.heading);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(package.title().c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::TextDisabled("by %s", package.owner.c_str());
        ImGui::EndGroup();
        ImGui::Spacing();
        // Tools (Blender add-ons, utilities) are not game mods: their page has the download.
        const bool tool = package.in_category("Tools") && !package.in_category("Mods");
        if (tool) {
            push_primary_button();
            if (ImGui::Button("GET IT ON THUNDERSTORE")) open_url(package.package_url);
            pop_primary_button();
        } else {
            ImGui::BeginDisabled(installing);
            push_primary_button();
            if (ImGui::Button(action_label(package, installed).c_str())) start_store_install(panel, {package});
            pop_primary_button();
            ImGui::EndDisabled();
            if (!package.package_url.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Thunderstore page")) open_url(package.package_url);
            }
        }
        if (version.website_url.starts_with("https://")) {
            ImGui::SameLine();
            if (ImGui::Button("Website")) open_url(version.website_url);
        }
        ImGui::Spacing();
        const auto field = [&](const char* name, const std::string& value) {
            if (value.empty()) return;
            ImGui::PushFont(fonts.caption);
            ImGui::TextDisabled("%s", name);
            ImGui::PopFont();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopTextWrapPos();
        };
        if (package.deprecated) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::warning), "Deprecated: its author no longer supports it.");
            ImGui::PopTextWrapPos();
        }
        field("DESCRIPTION", version.description);
        const auto found = installed.find(ts::folder_for(package.full_name));
        field("VERSION", "v" + version.number + (found == installed.end() ? std::string()
            : found->second.empty() ? "  (installed)" : "  (installed: v" + found->second + ")"));
        field("DOWNLOADS", count_text(package.downloads));
        field("UPDATED", date_text(package.date_updated));
        if (version.file_size) field("DOWNLOAD SIZE", size_text(version.file_size));
        std::string categories_text;
        for (const auto& category : package.categories) categories_text += (categories_text.empty() ? "" : ", ") + category;
        field("CATEGORIES", categories_text);
    }
    ImGui::EndChild();
}

} // namespace dingosdk::launcher_gui::detail
