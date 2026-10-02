#include "database_codec.h"

namespace dingosdk::profile::database {
namespace {
constexpr SaveTable profile_definitions[]{
    {"profile_meta","id,revision,defaults_version,neighborhood_rank_cap,document_schema",1,
     "CREATE TABLE profile_meta(id INTEGER PRIMARY KEY CHECK(id=1),revision TEXT NOT NULL,defaults_version INTEGER NOT NULL CHECK(defaults_version BETWEEN 0 AND 4294967295),neighborhood_rank_cap INTEGER NOT NULL CHECK(neighborhood_rank_cap BETWEEN 1 AND 10000),document_schema INTEGER NOT NULL CHECK(document_schema=1)) STRICT;"},
    {"player_settings","scope,name,value_json",2,
     "CREATE TABLE player_settings(scope TEXT NOT NULL,name TEXT NOT NULL,value_json TEXT NOT NULL CHECK(json_valid(value_json)),PRIMARY KEY(scope,name)) STRICT, WITHOUT ROWID;"},
    {"inventory","kind,item_id,owned",2,
     "CREATE TABLE inventory(kind TEXT NOT NULL CHECK(kind IN ('cosmetic','object')),item_id TEXT NOT NULL,owned INTEGER NOT NULL CHECK(owned IN (0,1)),PRIMARY KEY(kind,item_id)) STRICT, WITHOUT ROWID;"},
    {"cosmetic_loadouts","preset_id,recipe_format,recipes_json,extensions_json",1,
     "CREATE TABLE cosmetic_loadouts(preset_id TEXT PRIMARY KEY NOT NULL,recipe_format INTEGER NOT NULL CHECK(recipe_format=1),recipes_json TEXT NOT NULL CHECK(json_valid(recipes_json)),extensions_json TEXT NOT NULL CHECK(json_valid(extensions_json))) STRICT, WITHOUT ROWID;"},
    {"player_card","id,recipe_format,recipes_json,extensions_json",1,
     "CREATE TABLE player_card(id INTEGER PRIMARY KEY CHECK(id=1),recipe_format INTEGER NOT NULL CHECK(recipe_format=1),recipes_json TEXT NOT NULL CHECK(json_valid(recipes_json)),extensions_json TEXT NOT NULL CHECK(json_valid(extensions_json))) STRICT;"},
    {"quests","quest_id,state",1,
     "CREATE TABLE quests(quest_id TEXT PRIMARY KEY NOT NULL,state INTEGER NOT NULL CHECK(state BETWEEN 0 AND 6)) STRICT, WITHOUT ROWID;"},
    {"play_events","event_id,context,timestamp,event_count",1,
     "CREATE TABLE play_events(event_id TEXT PRIMARY KEY NOT NULL,context TEXT NOT NULL,timestamp TEXT NOT NULL,event_count INTEGER NOT NULL CHECK(event_count BETWEEN 0 AND 2147483647)) STRICT, WITHOUT ROWID;"},
    {"entitlements","entitlement_id,owned",1,
     "CREATE TABLE entitlements(entitlement_id TEXT PRIMARY KEY NOT NULL,owned INTEGER NOT NULL CHECK(owned IN (0,1))) STRICT, WITHOUT ROWID;"},
    {"neighborhood_ranks","neighborhood_id,rank",1,
     "CREATE TABLE neighborhood_ranks(neighborhood_id TEXT PRIMARY KEY NOT NULL,rank INTEGER NOT NULL CHECK(rank BETWEEN 0 AND 10000)) STRICT, WITHOUT ROWID;"},
    {"rip_score","id,score,cap,level,extensions_json",1,
     "CREATE TABLE rip_score(id INTEGER PRIMARY KEY CHECK(id=1),score INTEGER NOT NULL CHECK(score>=0),cap INTEGER NOT NULL CHECK(cap BETWEEN 1 AND 1000000000 AND score<=cap),level INTEGER NOT NULL CHECK(level BETWEEN 1 AND 10000),extensions_json TEXT NOT NULL CHECK(json_valid(extensions_json))) STRICT;"},
    {"challenge_progress","challenge_id,attempt,extensions_json",1,
     "CREATE TABLE challenge_progress(challenge_id TEXT PRIMARY KEY NOT NULL,attempt INTEGER NOT NULL CHECK(attempt BETWEEN 1 AND 4294967295),extensions_json TEXT NOT NULL CHECK(json_valid(extensions_json))) STRICT, WITHOUT ROWID;"},
    {"challenge_goals","challenge_id,goal_id,sort_order",2,
     "CREATE TABLE challenge_goals(challenge_id TEXT NOT NULL,goal_id TEXT NOT NULL,sort_order INTEGER NOT NULL CHECK(sort_order BETWEEN 0 AND 255),PRIMARY KEY(challenge_id,goal_id),FOREIGN KEY(challenge_id) REFERENCES challenge_progress(challenge_id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED) STRICT, WITHOUT ROWID;"},
    {"challenge_receipts","challenge_id,attempt,status,goals_json,grants_json,extensions_json",1,
     "CREATE TABLE challenge_receipts(challenge_id TEXT PRIMARY KEY NOT NULL,attempt INTEGER NOT NULL CHECK(attempt BETWEEN 1 AND 4294967295),status TEXT NOT NULL CHECK(status='committed'),goals_json TEXT NOT NULL CHECK(json_valid(goals_json)),grants_json TEXT NOT NULL CHECK(json_valid(grants_json)),extensions_json TEXT NOT NULL CHECK(json_valid(extensions_json)),FOREIGN KEY(challenge_id) REFERENCES challenge_progress(challenge_id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED) STRICT, WITHOUT ROWID;"},
    {"catalog_entries","catalog,item_id,definition_json",2,
     "CREATE TABLE catalog_entries(catalog TEXT NOT NULL CHECK(catalog IN ('cosmetic','object','challenge')),item_id TEXT NOT NULL,definition_json TEXT NOT NULL CHECK(json_valid(definition_json)),PRIMARY KEY(catalog,item_id)) STRICT, WITHOUT ROWID;"},
    {"profile_extensions","section,value_json",1,
     "CREATE TABLE profile_extensions(section TEXT PRIMARY KEY NOT NULL,value_json TEXT NOT NULL CHECK(json_valid(value_json))) STRICT, WITHOUT ROWID;"}
};
constexpr SaveTable placement_definitions[]{
    {"placement_meta","id,revision,document_schema",1,
     "CREATE TABLE placement_meta(id INTEGER PRIMARY KEY CHECK(id=1),revision TEXT NOT NULL,document_schema INTEGER NOT NULL CHECK(document_schema=1)) STRICT;"},
    {"maps","map_id",1,
     "CREATE TABLE maps(map_id TEXT PRIMARY KEY NOT NULL) STRICT, WITHOUT ROWID;"},
    {"placed_objects","map_id,object_id,sort_order,item_id,x,y,z,qx,qy,qz,qw",2,
     "CREATE TABLE placed_objects(map_id TEXT NOT NULL,object_id TEXT NOT NULL,sort_order INTEGER NOT NULL CHECK(sort_order BETWEEN 0 AND 1023),item_id TEXT NOT NULL,x REAL NOT NULL,y REAL NOT NULL,z REAL NOT NULL,qx REAL NOT NULL,qy REAL NOT NULL,qz REAL NOT NULL,qw REAL NOT NULL,PRIMARY KEY(map_id,object_id),FOREIGN KEY(map_id) REFERENCES maps(map_id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED) STRICT, WITHOUT ROWID;"},
    {"placement_extensions","section,value_json",1,
     "CREATE TABLE placement_extensions(section TEXT PRIMARY KEY NOT NULL,value_json TEXT NOT NULL CHECK(json_valid(value_json))) STRICT, WITHOUT ROWID;"}
};
}
SaveSchema profile_schema() { return {"profile", profile_definitions}; }
SaveSchema placement_schema() { return {"placements", placement_definitions}; }
}
