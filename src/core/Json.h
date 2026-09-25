#pragma once
// Minimal, dependency-free JSON value + parser + pretty printer.
// Objects keep insertion order so saved profiles stay human-readable/diffable.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infclick::json {

class Value {
public:
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
    using Array  = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(int v) : type_(Type::Number), n_(v) {}
    Value(unsigned v) : type_(Type::Number), n_(v) {}
    Value(int64_t v) : type_(Type::Number), n_(double(v)) {}
    Value(uint64_t v) : type_(Type::Number), n_(double(v)) {}
    Value(double v) : type_(Type::Number), n_(v) {}
    Value(const char* s) : type_(Type::String), s_(s) {}
    Value(std::string s) : type_(Type::String), s_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), a_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : type_(Type::Object), o_(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? b_ : def; }
    double asNumber(double def = 0) const { return isNumber() ? n_ : def; }
    int64_t asInt(int64_t def = 0) const { return isNumber() ? int64_t(n_) : def; }
    const std::string& asString() const;
    std::string asString(const std::string& def) const { return isString() ? s_ : def; }

    // Array access
    const Array& items() const;
    Array& items();
    void push(Value v);
    size_t size() const;

    // Object access
    const Object& members() const;
    const Value* find(std::string_view key) const;
    const Value& operator[](std::string_view key) const; // returns Null value if missing
    Value& set(std::string key, Value v);                 // insert or replace

    // Convenience getters with defaults
    bool getBool(std::string_view k, bool def) const;
    double getNumber(std::string_view k, double def) const;
    int64_t getInt(std::string_view k, int64_t def) const;
    std::string getString(std::string_view k, const std::string& def) const;

private:
    void ensureObject();
    void ensureArray();

    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::shared_ptr<Array> a_;
    std::shared_ptr<Object> o_;
};

struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;
    size_t errorOffset = 0;
};

ParseResult parse(std::string_view text);
std::string stringify(const Value& v, bool pretty = true);

} // namespace infclick::json
