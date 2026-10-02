#include "json.h"
#include <cmath>
#include <rapidjson/error/en.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>

namespace dingosdk {
Json::Json(std::initializer_list<Json> values) {
    const bool fields = std::all_of(values.begin(), values.end(), [](const Json& entry) {
        return entry.is_array() && entry.size() == 2 && entry[0].is_string();
    });
    if (fields) {
        data_ = Object{};
        for (const auto& entry : values) items().emplace(entry[0].string(), entry[1]);
    } else data_ = Array(values);
}

bool Json::operator==(const Json& other) const {
    if (data_.index() == other.data_.index()) return data_ == other.data_;
    if (!is_number() || !other.is_number()) return false;
    if (is_number_float() || other.is_number_float()) return get<double>() == other.get<double>();
    if (is_number_unsigned()) return std::cmp_equal(get<std::uint64_t>(), other.get<std::int64_t>());
    return std::cmp_equal(get<std::int64_t>(), other.get<std::uint64_t>());
}
bool Json::operator<(const Json& other) const {
    if (!is_number() || !other.is_number()) throw std::runtime_error("JSON ordering requires numbers");
    if (is_number_float() || other.is_number_float()) return get<double>() < other.get<double>();
    if (is_number_unsigned()) {
        if (other.is_number_unsigned()) return get<std::uint64_t>() < other.get<std::uint64_t>();
        return std::cmp_less(get<std::uint64_t>(), other.get<std::int64_t>());
    }
    if (other.is_number_unsigned()) return std::cmp_less(get<std::int64_t>(), other.get<std::uint64_t>());
    return get<std::int64_t>() < other.get<std::int64_t>();
}

namespace {
struct ProfileHandler : rapidjson::BaseReaderHandler<rapidjson::UTF8<>, ProfileHandler> {
    struct Frame { Json* value; std::string key; };
    Json root;
    std::vector<Frame> stack;
    JsonLimits limits;
    std::size_t events{};

    explicit ProfileHandler(JsonLimits bounds) : limits(bounds) {}
    void event(std::size_t depth) {
        if (depth > limits.depth || ++events > limits.events)
            throw std::runtime_error("Local profile JSON is too complex");
    }
    Json& add(Json value) {
        if (stack.empty()) { root = std::move(value); return root; }
        auto& frame = stack.back();
        if (frame.value->is_object()) {
            auto& slot = (*frame.value)[frame.key]; slot = std::move(value); return slot;
        }
        frame.value->push_back(std::move(value));
        return frame.value->at(frame.value->size() - 1);
    }
    bool scalar(Json value) { event(stack.size()); add(std::move(value)); return true; }
    bool Null() { return scalar(nullptr); }
    bool Bool(bool value) { return scalar(value); }
    bool Int(int value) { return scalar(value); }
    bool Uint(unsigned value) { return scalar(value); }
    bool Int64(std::int64_t value) { return scalar(value); }
    bool Uint64(std::uint64_t value) { return scalar(value); }
    bool Double(double value) { return scalar(value); }
    bool String(const char* text, rapidjson::SizeType length, bool) {
        return scalar(std::string(text, length));
    }
    bool Key(const char* text, rapidjson::SizeType length, bool) {
        event(stack.size());
        auto& frame = stack.back();
        frame.key.assign(text, length);
        if (frame.value->contains(frame.key)) throw std::runtime_error("Duplicate local profile JSON key");
        return true;
    }
    bool start(Json value) {
        event(stack.size());
        auto& node = add(std::move(value)); stack.push_back({&node, {}}); return true;
    }
    bool StartObject() { return start(Json::object()); }
    bool StartArray() { return start(Json::array()); }
    bool end() { stack.pop_back(); event(stack.size()); return true; }
    bool EndObject(rapidjson::SizeType) { return end(); }
    bool EndArray(rapidjson::SizeType) { return end(); }
};

template<class Writer> bool write_json(Writer& writer, const Json& value) {
    if (value.is_null()) return writer.Null();
    if (value.is_boolean()) return writer.Bool(value.get<bool>());
    if (value.is_number_unsigned()) return writer.Uint64(value.get<std::uint64_t>());
    if (value.is_number_integer()) return writer.Int64(value.get<std::int64_t>());
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        return std::isfinite(number) ? writer.Double(number) : writer.Null();
    }
    if (value.is_string()) {
        const auto& text = value.string();
        return writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
    }
    if (value.is_object()) {
        if (!writer.StartObject()) return false;
        for (const auto& [key, field] : value.items()) {
            if (!writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size())) || !write_json(writer, field)) return false;
        }
        return writer.EndObject();
    }
    if (!writer.StartArray()) return false;
    for (const auto& item : value) if (!write_json(writer, item)) return false;
    return writer.EndArray();
}
}

Json Json::parse(std::string_view text, JsonLimits limits) {
    if (text.size() > limits.bytes) throw std::runtime_error("Local profile exceeds size limit");
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    rapidjson::MemoryStream stream(text.empty() ? "" : text.data(), text.size());
    rapidjson::Reader reader;
    ProfileHandler handler(limits);
    constexpr unsigned flags = rapidjson::kParseValidateEncodingFlag | rapidjson::kParseFullPrecisionFlag |
        rapidjson::kParseIterativeFlag;
    const auto result = reader.Parse<flags>(stream, handler);
    if (!result) throw std::runtime_error("Invalid JSON at byte " + std::to_string(result.Offset()) +
        ": " + rapidjson::GetParseError_En(result.Code()));
    // MemoryStream uses NUL as its sentinel. A literal NUL in a file must not
    // hide a second document or trailing garbage after an otherwise valid root.
    if (stream.Tell() != text.size()) throw std::runtime_error("Unexpected data after JSON root");
    return std::move(handler.root);
}

std::string Json::dump(int indent) const {
    rapidjson::StringBuffer buffer;
    bool ok{};
    if (indent < 0) {
        rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::UTF8<>,
            rapidjson::CrtAllocator, rapidjson::kWriteValidateEncodingFlag> writer(buffer);
        ok = write_json(writer, *this);
    } else {
        rapidjson::PrettyWriter<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::UTF8<>,
            rapidjson::CrtAllocator, rapidjson::kWriteValidateEncodingFlag> writer(buffer);
        writer.SetIndent(' ', static_cast<unsigned>(indent));
        ok = write_json(writer, *this);
    }
    if (!ok) throw std::runtime_error("Cannot encode JSON string as UTF-8");
    return {buffer.GetString(), buffer.GetSize()};
}
}
