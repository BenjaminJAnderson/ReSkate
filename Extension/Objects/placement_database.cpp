#include "Extension/Profile/database_codec.h"
#include <map>
namespace dingosdk::profile::database {
Json placement_tables(Json document) {
    auto tables=empty_tables(placement_schema());
    add(tables,"placement_meta",Json::array({1,std::to_string(document.at("revision").get<std::uint64_t>()),document.at("schema_version")}));
    for (const auto& [map,objects] : document.at("maps").items()) {
        add(tables,"maps",Json::array({map}));
        for (std::size_t i=0;i<objects.size();++i) {
            const auto& object=objects[i]; const auto& p=object.at("position"); const auto& q=object.at("rotation");
            add(tables,"placed_objects",Json::array({map,std::to_string(object.at("id").get<std::uint64_t>()),i,object.at("item"),p[0],p[1],p[2],q[0],q[1],q[2],q[3]}),2);
        }
    }
    for (auto key : {"revision","schema_version","maps"}) document.erase(key);
    for (const auto& [key,value] : document.items()) add(tables,"placement_extensions",Json::array({key,value.dump()}));
    return tables;
}
Json placement_document(const Json& tables) {
    Json doc=Json::object();
    for (const auto& row : tables.at("placement_extensions")) doc[row[0].string()]=parsed(row[1]);
    const auto& meta=singleton(tables,"placement_meta"); doc["revision"]=uint_text(meta[1]); doc["schema_version"]=meta[2];
    doc["maps"]=Json::object();
    for (const auto& row : tables.at("maps")) doc["maps"][row[0].string()]=Json::array();
    std::map<std::string,std::map<unsigned,Json>> ordered;
    for (const auto& row : tables.at("placed_objects")) {
        require(doc.at("maps").contains(row[0].string()),"Orphan placed object");
        Json object{{"id",uint_text(row[1])},{"item",row[3]},
            {"position",Json::array({row[4],row[5],row[6]})},{"rotation",Json::array({row[7],row[8],row[9],row[10]})}};
        require(ordered[row[0].string()].emplace(row[2].get<unsigned>(),std::move(object)).second,"Duplicate placed object order");
    }
    for (const auto& [map,objects] : ordered) for (const auto& [index,object] : objects) {
        auto& values=doc["maps"][map]; require(index==values.size(),"Invalid placed object order"); values.push_back(object);
    }
    return doc;
}
}
