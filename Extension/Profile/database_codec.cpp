#include "database_codec.h"
#include <map>

namespace dingosdk::profile::database {
namespace {
void settings(Json& tables, Json& tree, const std::string& scope) {
    require(tree.is_object(), "Invalid settings collection");
    for (auto it=tree.items().begin(); it!=tree.items().end();) {
        if (it->second.is_object()) { settings(tables,it->second,scope+'/'+escape(it->first)); ++it; }
        else { add(tables,"player_settings",Json::array({scope,it->first,it->second.dump()}),2); it=tree.items().erase(it); }
    }
}
void loadout(Json& tables, std::string_view table, const Json& id, Json value) {
    auto format=value.at("recipe_format"), recipes=value.at("recipes");
    value.erase("recipe_format"); value.erase("recipes");
    add(tables,table,Json::array({id,format,recipes.dump(),value.dump()}));
}
Json loadout(const Json& row) {
    auto value=parsed(row[3]); require(value.is_object(),"Invalid loadout extensions");
    value["recipe_format"]=row[1]; value["recipes"]=parsed(row[2]); return value;
}
}
Json profile_tables(Json doc, const Json* previous_tables, const Snapshot* previous) {
    auto tables=empty_tables(profile_schema());
    const auto cached = [&](std::string_view table, Json key, const Json& current, const Json* old) {
        if (!previous_tables || !old || *old != current) return false;
        const auto count=static_cast<unsigned>(key.size());
        const auto identity=storage::row_key(key,count);
        const auto& rows=previous_tables->at(table);
        if (!rows.contains(identity)) return false;
        add(tables,table,rows.at(identity),count); return true;
    };
    add(tables,"profile_meta",Json::array({1,std::to_string(doc.at("revision").get<std::uint64_t>()),
        doc.at("defaults_version"),doc.at("neighborhood_rank_cap"),doc.at("schema_version")}));
    for (auto key : {"revision","defaults_version","neighborhood_rank_cap","schema_version"}) doc.erase(key);
    for (const auto& [id,v] : doc.at("options").items()) add(tables,"player_settings",Json::array({"options",id,v.dump()}),2);
    doc["options"]=Json::object(); settings(tables,doc.at("settings"),"/settings");
    for (const auto kind : {"cosmetic","object"}) {
        const auto section=std::string_view(kind)=="cosmetic"?"customization":"object_dropper";
        if (auto* inventory=field(doc,{section,"inventory"})) {
            for (const auto& [id,v] : inventory->items()) add(tables,"inventory",Json::array({kind,id,v.get<bool>()?1:0}),2);
            *inventory=Json::object();
        }
        if (auto* catalog=field(doc,{section,"catalog"})) {
            const auto* old_catalog = previous ? (std::string_view(kind)=="cosmetic" ?
                field(previous->customization,{"catalog"}) : field(previous->extensions,{"object_dropper","catalog"})) : nullptr;
            for (const auto& [id,v] : catalog->items()) {
                const auto* old=old_catalog && old_catalog->contains(id)?&old_catalog->at(id):nullptr;
                if (!cached("catalog_entries",Json::array({kind,id}),v,old)) add(tables,"catalog_entries",Json::array({kind,id,v.dump()}),2);
            }
            *catalog=Json::object();
        }
    }
    if (auto* outfits=field(doc,{"customization","loadouts"})) {
        const auto* old_outfits=previous?field(previous->customization,{"loadouts"}):nullptr;
        for (const auto& [id,v] : outfits->items()) {
            const auto* old=old_outfits && old_outfits->contains(id)?&old_outfits->at(id):nullptr;
            if (!cached("cosmetic_loadouts",Json::array({id}),v,old)) loadout(tables,"cosmetic_loadouts",id,v);
        }
        *outfits=Json::object();
    }
    if (auto* card=field(doc,{"customization","player_card"})) {
        const auto* old=previous?field(previous->customization,{"player_card"}):nullptr;
        if (!cached("player_card",Json::array({1}),*card,old)) loadout(tables,"player_card",1,*card);
        doc["customization"].erase("player_card");
    }
    auto& progress=doc.at("progress");
    for (const auto& [id,v] : progress.at("quests").items()) add(tables,"quests",Json::array({id,v}));
    progress["quests"]=Json::object();
    for (const auto& [id,v] : progress.at("play_events").items())
        add(tables,"play_events",Json::array({id,v.at("context"),std::to_string(v.at("timestamp").get<std::uint64_t>()),v.at("count")}));
    progress["play_events"]=Json::object();
    for (const auto& [id,v] : progress.at("neighborhood_ranks").items()) add(tables,"neighborhood_ranks",Json::array({id,v}));
    progress["neighborhood_ranks"]=Json::object();
    for (const auto& [id,v] : doc.at("entitlements").items()) add(tables,"entitlements",Json::array({id,v.get<bool>()?1:0}));
    doc["entitlements"]=Json::object();
    if (auto* score=field(doc,{"progress","rip_score"})) {
        auto extras=*score; for (auto key : {"value","cap","level"}) extras.erase(key);
        add(tables,"rip_score",Json::array({1,score->at("value"),score->at("cap"),score->at("level"),extras.dump()}));
        progress.erase("rip_score");
    }
    if (auto* catalog=field(doc,{"challenges","catalog"})) {
        const auto* old_catalog=previous?field(previous->extensions,{"challenges","catalog"}):nullptr;
        for (const auto& [id,v] : catalog->items()) {
            const auto* old=old_catalog && old_catalog->contains(id)?&old_catalog->at(id):nullptr;
            if (!cached("catalog_entries",Json::array({"challenge",id}),v,old)) add(tables,"catalog_entries",Json::array({"challenge",id,v.dump()}),2);
        }
        *catalog=Json::object();
    }
    if (auto* challenges=field(doc,{"challenges","progress"})) {
        for (const auto& [id,v] : challenges->items()) {
            auto extras=v; for (auto key : {"attempt","completed_criteria","receipt"}) extras.erase(key);
            add(tables,"challenge_progress",Json::array({id,v.at("attempt"),extras.dump()}));
            const auto& goals=v.at("completed_criteria");
            for (std::size_t i=0;i<goals.size();++i) add(tables,"challenge_goals",Json::array({id,goals[i],i}),2);
            if (v.contains("receipt")) {
                const auto& receipt=v.at("receipt"); auto extra=receipt;
                for (auto key : {"attempt","status","completed_criteria","grants"}) extra.erase(key);
                add(tables,"challenge_receipts",Json::array({id,receipt.at("attempt"),receipt.at("status"),
                    receipt.at("completed_criteria").dump(),receipt.at("grants").dump(),extra.dump()}));
            }
        }
        *challenges=Json::object();
    }
    // Only extension/policy data and empty structural containers remain here.
    // Ownership, outfits, progress, receipts, and settings live in their tables.
    for (const auto& [key,v] : doc.items()) add(tables,"profile_extensions",Json::array({key,v.dump()}));
    return tables;
}
Json profile_document(const Json& tables) {
    Json doc=Json::object();
    for (const auto& row : tables.at("profile_extensions")) doc[row[0].string()]=parsed(row[1]);
    const auto& meta=singleton(tables,"profile_meta");
    doc["revision"]=uint_text(meta[1]); doc["defaults_version"]=meta[2]; doc["neighborhood_rank_cap"]=meta[3]; doc["schema_version"]=meta[4];
    for (const auto& row : tables.at("player_settings")) {
        const auto& scope=row[0].string();
        if (scope=="options") doc["options"][row[1].string()]=parsed(row[2]);
        else { require(scope=="/settings" || scope.starts_with("/settings/"),"Invalid player setting scope");
            pointer(doc,scope)[row[1].string()]=parsed(row[2]); }
    }
    for (const auto& row : tables.at("inventory")) {
        const auto& kind=row[0].string(); require(kind=="cosmetic" || kind=="object","Invalid inventory kind");
        doc[kind=="cosmetic"?"customization":"object_dropper"]["inventory"][row[1].string()]=row[2].get<int>()!=0;
    }
    for (const auto& row : tables.at("cosmetic_loadouts")) doc["customization"]["loadouts"][row[0].string()]=loadout(row);
    for (const auto& row : tables.at("player_card")) doc["customization"]["player_card"]=loadout(row);
    for (const auto& row : tables.at("quests")) doc["progress"]["quests"][row[0].string()]=row[1];
    for (const auto& row : tables.at("play_events")) doc["progress"]["play_events"][row[0].string()]=
        Json{{"context",row[1]},{"timestamp",uint_text(row[2])},{"count",row[3]}};
    for (const auto& row : tables.at("neighborhood_ranks")) doc["progress"]["neighborhood_ranks"][row[0].string()]=row[1];
    for (const auto& row : tables.at("entitlements")) doc["entitlements"][row[0].string()]=row[1].get<int>()!=0;
    for (const auto& row : tables.at("rip_score")) {
        auto score=parsed(row[4]); score["value"]=row[1]; score["cap"]=row[2]; score["level"]=row[3]; doc["progress"]["rip_score"]=std::move(score);
    }
    for (const auto& row : tables.at("challenge_progress")) {
        auto value=parsed(row[2]); value["attempt"]=row[1]; value["completed_criteria"]=Json::array();
        doc["challenges"]["progress"][row[0].string()]=std::move(value);
    }
    std::map<std::string,std::map<unsigned,std::string>> goals;
    for (const auto& row : tables.at("challenge_goals")) {
        require(doc.at("challenges").at("progress").contains(row[0].string()),"Orphan challenge goal");
        require(goals[row[0].string()].emplace(row[2].get<unsigned>(),row[1].string()).second,"Duplicate challenge goal order");
    }
    for (const auto& [id,entries] : goals) for (const auto& [index,goal] : entries) {
        auto& values=doc["challenges"]["progress"][id]["completed_criteria"];
        require(index==values.size(),"Invalid challenge goal order"); values.push_back(goal);
    }
    for (const auto& row : tables.at("challenge_receipts")) {
        require(doc.at("challenges").at("progress").contains(row[0].string()),"Orphan challenge receipt");
        auto receipt=parsed(row[5]); receipt["attempt"]=row[1]; receipt["status"]=row[2];
        receipt["completed_criteria"]=parsed(row[3]); receipt["grants"]=parsed(row[4]);
        doc["challenges"]["progress"][row[0].string()]["receipt"]=std::move(receipt);
    }
    for (const auto& row : tables.at("catalog_entries")) {
        const auto& kind=row[0].string(); require(kind=="cosmetic" || kind=="object" || kind=="challenge","Invalid catalog kind");
        doc[kind=="cosmetic"?"customization":kind=="object"?"object_dropper":"challenges"]["catalog"][row[1].string()]=parsed(row[2]);
    }
    return doc;
}
}
