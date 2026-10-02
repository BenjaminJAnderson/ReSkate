#include "gui_internal.h"

#include "mod_manager.h"

#include "Engine/Core/Log/logging.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <format>

using Microsoft::WRL::ComPtr;

// The Mods panel: install order, enabled state and installs in the background.
namespace dingosdk::launcher_gui::detail {
namespace {

void save(ModsPanel& panel) {
    try {
        mods::save_mod_order(panel.root, panel.list.entries);
        panel.list.issue.clear();
        panel.message = "Saved. Changes apply the next time Skate starts.";
        panel.message_error = false;
    } catch (const std::exception& failure) {
        panel.message = failure.what();
        panel.message_error = true;
    }
}

// Picks up a finished install: rescan and select the new mod, or ask to replace.
void collect_install(ModsPanel& panel, const launcher_app::Session& session) {
    std::lock_guard lock(panel.mutex);
    if (!panel.finished) return;
    panel.finished = false;
    panel.progress = -1;
    if (!panel.finished_conflict.empty()) {
        panel.conflict_name = panel.finished_conflict;
        panel.conflict_source = panel.finished_source;
        panel.message.clear();
        return;
    }
    scan(panel, session);
    if (!panel.finished_error.empty()) {
        panel.message = panel.finished_error;
        panel.message_error = true;
        return;
    }
    for (std::size_t i = 0; i < panel.list.entries.size(); ++i)
        if (panel.list.entries[i].mod.name == panel.finished_name) panel.selected = static_cast<int>(i);
    panel.message_error = false;
    if (!panel.finished_note.empty()) {
        panel.message = panel.finished_note;   // Thunderstore installs say what they did and log it themselves
        panel.finished_note.clear();
        return;
    }
    panel.message = "Installed " + panel.finished_name + ". It loads the next time Skate starts.";
    logging::write(logging::Level::info, logging::Channel::launcher, "Mod installed: " + panel.finished_name);
}

// INSTALLED and BROWSE, styled like Settings' tabs; a blue count marks updates.
void mods_tabs(ModsPanel& panel, std::size_t updates) {
    static constexpr std::array<const char*, 2> names{"INSTALLED", "BROWSE"};
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        if (i) ImGui::SameLine(0, S(6));
        const bool selected = panel.tab == i;
        if (selected) push_primary_button();
        if (ImGui::Button(names[static_cast<std::size_t>(i)], ImVec2(S(150), S(34)))) panel.tab = i;
        if (selected) pop_primary_button();
        if (i == 0 && updates) {
            const auto corner = ImGui::GetItemRectMax();
            const auto text = std::to_string(updates);
            const ImVec2 centre(corner.x - S(4), ImGui::GetItemRectMin().y + S(4));
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddCircleFilled(centre, S(10), color::blue);
            const auto extent = ImGui::CalcTextSize(text.c_str());
            draw->AddText(ImVec2(centre.x - extent.x * 0.5f, centre.y - extent.y * 0.5f), color::ink, text.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(updates == 1 ? "1 update on Thunderstore" : "%s updates on Thunderstore", text.c_str());
        }
    }
    ImGui::Spacing();
}

// The Windows file (or folder) picker; empty when cancelled.
fs::path pick(HWND owner, bool folder) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
    FILEOPENDIALOGOPTIONS options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    if (!folder) {
        const COMDLG_FILTERSPEC filter[]{{L"Mod archive (*.zip)", L"*.zip"}};
        dialog->SetFileTypes(1, filter);
    }
    dialog->SetTitle(folder ? L"Choose a mod folder to install" : L"Choose a mod .zip to install");
    if (FAILED(dialog->Show(owner))) return {};
    ComPtr<IShellItem> item;
    PWSTR path{};
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}

std::string summary(const mods::Mod& mod, bool excluded = false) {
    std::vector<std::string> parts;
    if (!mod.version.empty()) parts.push_back("v" + mod.version);
    if (!mod.author.empty()) parts.push_back("by " + mod.author);
    if (!mod.levels.empty()) parts.push_back(mod.levels.size() == 1 ? "1 map" : std::to_string(mod.levels.size()) + " maps");
    else if (mod.provides_layout) parts.push_back("game data");
    if (!mod.park_maps.empty()) parts.push_back(mod.park_maps.size() == 1 ? "1 park" : std::to_string(mod.park_maps.size()) + " parks");
    if (!mod.provides_layout && !mod.provides_levels && mod.park_maps.empty()) parts.push_back("nothing to load");
    if (excluded) parts.insert(parts.begin(), "NOT LOADED: could not be merged");
    if (!mod.outdated.empty()) parts.insert(parts.begin(), "OUTDATED: update it for this game version");
    std::string text;
    for (const auto& part : parts) text += (text.empty() ? "" : "  /  ") + part;
    return text;
}

// The INSTALLED page body: install order and enabled state, and the selected mod's details.
void installed_page(const Fonts& fonts, ModsPanel& panel, const thunderstore::Installed& installed, float body,
                    bool installing) {
    auto& entries = panel.list.entries;
    const float list_width = (ImGui::GetContentRegionAvail().x - S(16)) * 0.58f;

    // ------------------------------------------------ list
    ImGui::BeginChild("##mod_list", ImVec2(list_width, body), ImGuiChildFlags_Borders);
    bool changed = false;
    int move_from = -1, move_to = -1;
    if (entries.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled(panel.list.present ? "No mods installed yet." : "No Mods folder yet.");
        ImGui::TextDisabled("Use Install below, or drop a mod .zip on the window.");
    }
    ImGui::BeginDisabled(installing);
    const float arrows = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        auto& entry = entries[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Checkbox("##enabled", &entry.enabled)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(entry.enabled ? "Enabled: loads when Skate starts" : "Disabled");
        ImGui::SameLine();
        const auto label = std::to_string(i + 1) + ".  " + entry.mod.title;
        ImGui::PushFont(fonts.bold);
        if (!entry.enabled) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::muted));
        if (ImGui::Selectable(label.c_str(), panel.selected == i, ImGuiSelectableFlags_AllowOverlap,
                ImVec2(ImGui::GetContentRegionAvail().x - arrows - S(8), 0)))
            panel.selected = i;
        if (!entry.enabled) ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - arrows);
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { move_from = i; move_to = i - 1; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 == static_cast<int>(entries.size()));
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { move_from = i; move_to = i + 1; }
        ImGui::EndDisabled();
        const bool left_out = panel.list.excluded.contains(entry.mod.name);
        const auto* package = package_for(panel.store, entry.mod.name);
        const bool update = package && thunderstore::update_available(*package, installed);
        if (const auto detail = summary(entry.mod, left_out); !detail.empty() || update) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            if (update) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::blue), "UPDATE v%s", package->latest().number.c_str());
                if (!detail.empty()) ImGui::SameLine();
            }
            if (!detail.empty()) ImGui::TextDisabled("%s", detail.c_str());
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    if (move_from >= 0) {
        std::swap(entries[static_cast<std::size_t>(move_from)], entries[static_cast<std::size_t>(move_to)]);
        if (panel.selected == move_from) panel.selected = move_to;
        else if (panel.selected == move_to) panel.selected = move_from;
        changed = true;
    }
    if (changed) save(panel);

    // ------------------------------------------------ details
    ImGui::SameLine(0, S(16));
    ImGui::BeginChild("##mod_details", ImVec2(0, body), ImGuiChildFlags_Borders);
    if (panel.selected >= 0 && panel.selected < static_cast<int>(entries.size())) {
        const auto& entry = entries[static_cast<std::size_t>(panel.selected)];
        const auto& mod = entry.mod;
        ImGui::PushFont(fonts.heading);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(mod.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::TextDisabled("%s", entry.enabled ? "Enabled" : "Disabled");
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
        if (!mod.outdated.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::danger),
                "Outdated: this mod does not load. It was made for another version of Skate (%s). Get an updated "
                "version, or rebuild it with the latest ReSkate Studio.", mod.outdated.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }
        if (const auto left_out = panel.list.excluded.find(mod.name); left_out != panel.list.excluded.end()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::danger),
                "Not loaded: the game could not merge this mod cleanly, so none of it is used. Reinstall the whole "
                "mod folder, or rebuild it with a current ReSkate Studio.");
            if (!left_out->second.empty()) ImGui::TextDisabled("%s", left_out->second.front().c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }
        field("AUTHOR", mod.author);
        field("VERSION", mod.version);
        field("DESCRIPTION", mod.description);
        field("FOLDER", "Mods\\" + mod.name);
        const auto* package = package_for(panel.store, mod.name);
        const bool update = package && thunderstore::update_available(*package, installed);
        if (package)
            field("THUNDERSTORE", package->full_name + (update ? "  (v" + package->latest().number + " available)"
                                                                : std::string("  (up to date)")));
        if (!mod.tool.empty() || !mod.built.empty())
            field("BUILT WITH", mod.tool + (mod.built.empty() ? "" : (mod.tool.empty() ? "" : ", ") + mod.built));
        std::string levels;
        for (const auto& level : mod.levels) {
            const auto slash = level.rfind('/');
            levels += (levels.empty() ? "" : "\n") + (slash == std::string::npos ? level : level.substr(slash + 1));
        }
        field("MAPS", levels);
        std::string parks;
        for (const auto& map : mod.park_maps) parks += (parks.empty() ? "" : ", ") + map;
        field("PARKS", parks);
        if (!mod.provides_layout && !mod.provides_levels && mod.park_maps.empty())
            field("NOTE", "This folder has no layout.toc or reskate-levels.json, so the game has nothing to load from it.");
        ImGui::Spacing();
        if (update) {
            ImGui::BeginDisabled(installing);
            push_primary_button();
            if (ImGui::Button(("UPDATE TO v" + package->latest().number).c_str())) start_store_install(panel, {*package});
            pop_primary_button();
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Open folder")) open_path(mod.directory);
        if (package && !package->package_url.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Thunderstore page"))
                open_url(package->package_url);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(installing);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::danger));
        if (ImGui::Button("Remove")) panel.confirm_remove = mod.name;
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("Select a mod to see its details.");
        if (!panel.list.missing.empty()) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0);
            std::string missing;
            for (const auto& name : panel.list.missing) missing += (missing.empty() ? "" : ", ") + name;
            ImGui::TextDisabled("mods.json also lists folders that are not installed: %s", missing.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();
}

} // namespace

void scan(ModsPanel& panel, const launcher_app::Session& session) {
    panel.root = launcher_mods::mods_root(session.paths.directory);
    panel.list = mods::scan_mods(panel.root.parent_path());
    panel.scanned = true;
    if (panel.selected >= static_cast<int>(panel.list.entries.size())) panel.selected = -1;
}

void start_install(ModsPanel& panel, const fs::path& source, bool replace) {
    if (panel.installing) return;
    if (panel.worker.joinable()) panel.worker.join();
    panel.installing = true;
    panel.cancel = false;
    panel.progress = 0;
    panel.message = "Installing " + utf8(source.filename().wstring()) + "...";
    panel.message_error = false;
    const auto root = panel.root;
    panel.worker = std::thread([&panel, root, source, replace] {
        std::string name, error, conflict;
        try {
            name = launcher_mods::install(root, source, replace,
                [&panel](float fraction) { panel.progress = fraction; }, panel.cancel);
        } catch (const launcher_mods::AlreadyInstalled& existing) {
            conflict = existing.name;
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        std::lock_guard lock(panel.mutex);
        panel.finished = true;
        panel.finished_name = name;
        panel.finished_error = error;
        panel.finished_conflict = conflict;
        panel.finished_source = source;
        panel.installing = false;
    });
}

void mods_window(Launcher& launcher, const Fonts& fonts, ImVec2 size, Ui& ui, ModsPanel& panel, HWND window) {
    const auto& session = launcher.session();
    if (!panel.scanned) scan(panel, session);
    collect_install(panel, session);
    refresh_listing(panel, ImGui::GetTime());
    const auto frame = begin_panel("##mods_panel", size, ImVec2(S(960), S(620)));
    const bool installing = panel.installing;
    auto& entries = panel.list.entries;
    const auto installed = installed_versions(panel.list);
    const auto pending = updates(panel.store, installed);
    const bool browsing = panel.tab == 1;

    panel_title(fonts, "MODS");
    mods_tabs(panel, pending.size());
    ImGui::PushTextWrapPos(0);
    if (browsing)
        ImGui::TextDisabled("Mods published on Thunderstore (thunderstore.io/c/reskate). Installed mods load the next "
                            "time Skate starts; updates appear here and on the INSTALLED tab.");
    else
        ImGui::TextDisabled("Mods load top to bottom: where two mods change the same thing, the higher one wins. "
                            "Changes apply the next time Skate starts. Drop a .zip or a mod folder on this window to install it.");
    ImGui::PopTextWrapPos();
    if (launcher.game())
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::warning), "Skate is running: restart it to apply changes.");
    if (!panel.list.issue.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::danger),
            "mods.json could not be read (%s), so the game loads no mods. Any change here rewrites it.",
            panel.list.issue.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    const float footer = ImGui::GetFrameHeightWithSpacing() * 2 + S(30);
    const float body = std::max(S(120), frame.y - ImGui::GetCursorPosY() - footer);

    if (browsing) browse_page(fonts, panel, body, installing);
    else installed_page(fonts, panel, installed, body, installing);

    // ------------------------------------------------ status and actions
    ImGui::Spacing();
    if (installing) {
        std::string activity;
        {
            std::lock_guard lock(panel.mutex);
            activity = panel.activity;
        }
        const float fraction = std::clamp(panel.progress.load(), 0.0f, 1.0f);
        const auto overlay = activity.empty() ? std::string() : std::format("{}  {:.0f}%", activity, fraction * 100);
        ImGui::ProgressBar(fraction, ImVec2(-S(120), 0), overlay.empty() ? nullptr : overlay.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(-1, 0))) panel.cancel = true;
    } else if (!panel.message.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(panel.message_error ? color::danger : color::good), "%s",
            panel.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorPosY(frame.y - S(24) - ImGui::GetFrameHeight());
    ImGui::BeginDisabled(installing);
    if (browsing) {
        if (ImGui::Button("Open Thunderstore")) open_url(utf8(thunderstore::community_page(thunderstore::community())));
        ImGui::SameLine();
        ImGui::BeginDisabled(panel.store.loading);
        if (ImGui::Button("Refresh")) { scan(panel, session); refresh_listing(panel, ImGui::GetTime(), true); panel.message.clear(); }
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Install .zip"))
            if (const auto path = pick(window, false); !path.empty()) start_install(panel, path, false);
        ImGui::SameLine();
        if (ImGui::Button("Install folder"))
            if (const auto path = pick(window, true); !path.empty()) start_install(panel, path, false);
        ImGui::SameLine();
        if (ImGui::Button("Open Mods folder")) open_path(panel.root);
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) { scan(panel, session); panel.message.clear(); }
        if (!pending.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(std::format("Update all ({})", pending.size()).c_str())) {
                std::vector<thunderstore::Package> packages;
                for (const auto* package : pending) packages.push_back(*package);
                start_store_install(panel, std::move(packages));
            }
        }
    }
    ImGui::SameLine(frame.x - S(28) - S(110));
    push_primary_button();
    if (ImGui::Button("DONE", ImVec2(S(110), 0))) { ui.mods = false; panel.message.clear(); panel.scanned = false; }
    pop_primary_button();
    ImGui::EndDisabled();

    // ------------------------------------------------ confirmations
    if (!panel.confirm_remove.empty() && !ImGui::IsPopupOpen("Remove mod")) ImGui::OpenPopup("Remove mod");
    if (!panel.conflict_name.empty() && !ImGui::IsPopupOpen("Replace mod")) ImGui::OpenPopup("Replace mod");
    ImGui::SetNextWindowSize(ImVec2(S(440), 0));
    if (ImGui::BeginPopupModal("Remove mod", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(0);
        ImGui::Text("Remove \"%s\"? Its folder goes to the Recycle Bin.", panel.confirm_remove.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(S(110), 0))) { panel.confirm_remove.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        push_primary_button();
        if (ImGui::Button("REMOVE", ImVec2(S(110), 0))) {
            const auto name = panel.confirm_remove;
            try {
                launcher_mods::remove(panel.root, name);
                std::erase_if(entries, [&](const auto& entry) { return entry.mod.name == name; });
                save(panel);
                panel.message = "Removed " + name + ". It is in the Recycle Bin if you want it back.";
                panel.selected = -1;
                logging::write(logging::Level::info, logging::Channel::launcher, "Mod removed: " + name);
            } catch (const std::exception& failure) {
                panel.message = failure.what();
                panel.message_error = true;
            }
            const auto message = panel.message;
            const bool error = panel.message_error;
            scan(panel, session);
            panel.message = message;
            panel.message_error = error;
            panel.confirm_remove.clear();
            ImGui::CloseCurrentPopup();
        }
        pop_primary_button();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize(ImVec2(S(440), 0));
    if (ImGui::BeginPopupModal("Replace mod", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(0);
        ImGui::Text("\"%s\" is already installed. Replace it? The installed copy goes to the Recycle Bin.",
            panel.conflict_name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(S(110), 0))) {
            panel.conflict_name.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        push_primary_button();
        if (ImGui::Button("REPLACE", ImVec2(S(110), 0))) {
            const auto source = panel.conflict_source;
            panel.conflict_name.clear();
            ImGui::CloseCurrentPopup();
            start_install(panel, source, true);
        }
        pop_primary_button();
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace dingosdk::launcher_gui::detail
