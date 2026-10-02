#include "logging_internal.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <atomic>

namespace dingosdk::logging {
void event(Channel channel, std::string_view json, Level level) noexcept {
    event(default_context(channel), channel, json, level);
}
void event(Context context, Channel channel, std::string_view json, Level level) noexcept {
    detail::PreserveError preserve;
    try {
        if (json.empty()) return;
        if (json.size() > 256 * 1024) {
            write(Level::warning, context, channel, "Diagnostic event exceeded the 256 KiB limit."); return;
        }
        rapidjson::Document document;
        document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(json.data(), json.size());
        if (document.HasParseError() || !document.IsObject()) {
            write(Level::warning, context, channel, "Malformed diagnostic event was discarded."); return;
        }
        const auto event = document.FindMember("event");
        const std::string_view id = event != document.MemberEnd() && event->value.IsString()
            ? std::string_view(event->value.GetString(), event->value.GetStringLength()) : "observation";
        // Legacy native observers use structured events. Preserve their severity
        // during migration, including errors produced through generic callbacks.
        if (id.ends_with("_failed") || id == "bootstrap_failed") level = std::max(level, Level::error);
        else if (id.ends_with("_rejected") || id.ends_with("_mismatch")) level = std::max(level, Level::warning);
        const std::pair<std::string_view, std::string_view> activities[]{
            {"local_profile_initialized", "Local profile services initialized"},
            {"local_profile_hydrated", "Saved profile applied to the game"},
            {"local_profile_mission_saved", "Mission progress saved"},
            {"local_location_travel_request", "Travel requested"},
            {"local_object_browser_ready", "Object browser opened"},
            {"local_object_categories_published", "Object browser categories populated"},
            {"controller_binding_saved", "Controller binding saved"},
            {"local_player_card_saved", "Player card saved"}
        };
        const auto activity = std::find_if(std::begin(activities), std::end(activities),
            [&](const auto& entry) { return entry.first == id; });
        if (activity != std::end(activities)) level = std::max(level, Level::info);
        if (id == "launcher_readiness") {
            const auto ready = document.FindMember("ready");
            if (ready != document.MemberEnd() && ready->value.IsBool() && !ready->value.GetBool())
                level = std::max(level, Level::error);
        }
        if (id == "local_profile_event_saved") {
            // Aggregate automatic saves: first success, then at most one update
            // per 30 seconds when saves continue. No timer or background thread.
            static std::atomic<ULONGLONG> next{};
            static std::atomic<std::uint64_t> saves{};
            ++saves;
            const auto now = GetTickCount64();
            auto due = next.load();
            if (now >= due && next.compare_exchange_strong(due, now + 30000))
                log(Level::info, context, Channel::profile, "Profile changes saved ({} updates).", saves.exchange(0));
        }
        const auto error = document.FindMember("error");
        if (error != document.MemberEnd() && ((error->value.IsBool() && error->value.GetBool()) ||
            (error->value.IsString() && error->value.GetStringLength()))) level = std::max(level, Level::error);
        if (!enabled(level)) return;
        std::string message(activity != std::end(activities) ? activity->second : id);
        std::replace(message.begin(), message.end(), '_', ' ');
        if (!message.empty() && message[0] >= 'a' && message[0] <= 'z') message[0] -= 'a' - 'A';
        bool first = true;
        for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member) {
            if (std::string_view(member->name.GetString(), member->name.GetStringLength()) == "event") continue;
            message += first ? ": " : ", "; first = false;
            message.append(member->name.GetString(), member->name.GetStringLength()); message += '=';
            const auto& value = member->value;
            if (value.IsString()) message.append(value.GetString(), std::min<rapidjson::SizeType>(value.GetStringLength(), 512));
            else if (value.IsArray()) message += std::format("[{} items]", value.Size());
            else if (value.IsObject()) message += std::format("[{} fields]", value.MemberCount());
            else {
                rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                value.Accept(writer); message.append(buffer.GetString(), buffer.GetSize());
            }
            if (message.size() > 8000) { message += "... [truncated]"; break; }
        }
        write(level, context, channel, message);
        log(Level::trace, context, channel, "Native event: {}", json);
    } catch (...) { write(Level::error, context, channel, "Could not record a diagnostic event."); }
}
}
