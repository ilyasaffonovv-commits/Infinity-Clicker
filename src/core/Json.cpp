#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace infclick::json {

namespace {
const std::string kEmptyString;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;
const Value kNull;
} // namespace

const std::string& Value::asString() const { return isString() ? s_ : kEmptyString; }

const Value::Array& Value::items() const { return isArray() ? *a_ : kEmptyArray; }

Value::Array& Value::items()
{
    ensureArray();
    return *a_;
}

void Value::push(Value v)
{
    ensureArray();
    a_->push_back(std::move(v));
}

size_t Value::size() const
{
    if (isArray()) return a_->size();
    if (isObject()) return o_->size();
    return 0;
}

const Value::Object& Value::members() const { return isObject() ? *o_ : kEmptyObject; }

const Value* Value::find(std::string_view key) const
{
    if (!isObject()) return nullptr;
    for (const auto& [k, v] : *o_)
        if (k == key) return &v;
    return nullptr;
}

const Value& Value::operator[](std::string_view key) const
{
    const Value* v = find(key);
    return v ? *v : kNull;
}

Value& Value::set(std::string key, Value v)
{
    ensureObject();
    for (auto& [k, existing] : *o_) {
        if (k == key) {
            existing = std::move(v);
            return existing;
        }
    }
    o_->emplace_back(std::move(key), std::move(v));
    return o_->back().second;
}

bool Value::getBool(std::string_view k, bool def) const
{
    const Value* v = find(k);
    return v ? v->asBool(def) : def;
}
double Value::getNumber(std::string_view k, double def) const
{
    const Value* v = find(k);
    return v ? v->asNumber(def) : def;
}
int64_t Value::getInt(std::string_view k, int64_t def) const
{
    const Value* v = find(k);
    return v ? v->asInt(def) : def;
}
std::string Value::getString(std::string_view k, const std::string& def) const
{
    const Value* v = find(k);
    return v ? v->asString(def) : def;
}

void Value::ensureObject()
{
    if (type_ != Type::Object) {
        *this = Value(Object{});
    } else if (o_.use_count() > 1) {
        o_ = std::make_shared<Object>(*o_); // copy-on-write
    }
}

void Value::ensureArray()
{
    if (type_ != Type::Array) {
        *this = Value(Array{});
    } else if (a_.use_count() > 1) {
        a_ = std::make_shared<Array>(*a_);
    }
}

// ---------------------------------------------------------------- parser

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    ParseResult run()
    {
        ParseResult r;
        skipWs();
        if (!parseValue(r.value, 0)) {
            r.error = err_.empty() ? "syntax error" : err_;
            r.errorOffset = p_;
            return r;
        }
        skipWs();
        if (p_ != t_.size()) {
            r.error = "trailing characters";
            r.errorOffset = p_;
            return r;
        }
        r.ok = true;
        return r;
    }

private:
    bool fail(const char* msg)
    {
        if (err_.empty()) err_ = msg;
        return false;
    }

    void skipWs()
    {
        while (p_ < t_.size()) {
            char c = t_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++p_;
            } else if (c == '/' && p_ + 1 < t_.size() && t_[p_ + 1] == '/') { // tolerate // comments
                while (p_ < t_.size() && t_[p_] != '\n') ++p_;
            } else {
                break;
            }
        }
    }

    bool parseValue(Value& out, int depth)
    {
        if (depth > 64) return fail("nesting too deep");
        if (p_ >= t_.size()) return fail("unexpected end");
        char c = t_[p_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = Value(std::move(s));
            return true;
        }
        if (c == 't') return literal("true", Value(true), out);
        if (c == 'f') return literal("false", Value(false), out);
        if (c == 'n') return literal("null", Value(), out);
        return parseNumber(out);
    }

    bool literal(const char* word, Value v, Value& out)
    {
        size_t n = std::strlen(word);
        if (t_.substr(p_, n) != word) return fail("invalid literal");
        p_ += n;
        out = std::move(v);
        return true;
    }

    bool parseNumber(Value& out)
    {
        size_t start = p_;
        if (p_ < t_.size() && (t_[p_] == '-' || t_[p_] == '+')) ++p_;
        bool digits = false;
        while (p_ < t_.size() && ((t_[p_] >= '0' && t_[p_] <= '9') || t_[p_] == '.' || t_[p_] == 'e' ||
                                  t_[p_] == 'E' || ((t_[p_] == '-' || t_[p_] == '+') && p_ > start &&
                                                    (t_[p_ - 1] == 'e' || t_[p_ - 1] == 'E')))) {
            if (t_[p_] >= '0' && t_[p_] <= '9') digits = true;
            ++p_;
        }
        if (!digits) return fail("invalid number");
        std::string tmp(t_.substr(start, p_ - start));
        char* end = nullptr;
        double v = std::strtod(tmp.c_str(), &end);
        if (end == tmp.c_str() || !std::isfinite(v)) return fail("invalid number");
        out = Value(v);
        return true;
    }

    static void appendUtf8(std::string& s, uint32_t cp)
    {
        if (cp < 0x80) {
            s += char(cp);
        } else if (cp < 0x800) {
            s += char(0xC0 | (cp >> 6));
            s += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += char(0xE0 | (cp >> 12));
            s += char(0x80 | ((cp >> 6) & 0x3F));
            s += char(0x80 | (cp & 0x3F));
        } else {
            s += char(0xF0 | (cp >> 18));
            s += char(0x80 | ((cp >> 12) & 0x3F));
            s += char(0x80 | ((cp >> 6) & 0x3F));
            s += char(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(uint32_t& v)
    {
        if (p_ + 4 > t_.size()) return fail("bad \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return fail("bad hex digit");
        }
        return true;
    }

    bool parseString(std::string& out)
    {
        ++p_; // opening quote
        while (p_ < t_.size()) {
            char c = t_[p_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (p_ >= t_.size()) break;
                char e = t_[p_++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p_ + 6 <= t_.size() && t_[p_] == '\\' && t_[p_ + 1] == 'u') {
                        p_ += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        return fail("unterminated string");
    }

    bool parseArray(Value& out, int depth)
    {
        ++p_;
        Value::Array arr;
        skipWs();
        if (p_ < t_.size() && t_[p_] == ']') {
            ++p_;
            out = Value(std::move(arr));
            return true;
        }
        for (;;) {
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (p_ >= t_.size()) return fail("unterminated array");
            if (t_[p_] == ',') {
                ++p_;
                skipWs();
                if (p_ < t_.size() && t_[p_] == ']') { ++p_; break; } // tolerate trailing comma
                continue;
            }
            if (t_[p_] == ']') { ++p_; break; }
            return fail("expected , or ]");
        }
        out = Value(std::move(arr));
        return true;
    }

    bool parseObject(Value& out, int depth)
    {
        ++p_;
        Value::Object obj;
        skipWs();
        if (p_ < t_.size() && t_[p_] == '}') {
            ++p_;
            out = Value(std::move(obj));
            return true;
        }
        for (;;) {
            skipWs();
            if (p_ >= t_.size() || t_[p_] != '"') return fail("expected key");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p_ >= t_.size() || t_[p_] != ':') return fail("expected :");
            ++p_;
            skipWs();
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            obj.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (p_ >= t_.size()) return fail("unterminated object");
            if (t_[p_] == ',') {
                ++p_;
                skipWs();
                if (p_ < t_.size() && t_[p_] == '}') { ++p_; break; }
                continue;
            }
            if (t_[p_] == '}') { ++p_; break; }
            return fail("expected , or }");
        }
        out = Value(std::move(obj));
        return true;
    }

    std::string_view t_;
    size_t p_ = 0;
    std::string err_;
};

void writeString(std::string& o, const std::string& s)
{
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            } else {
                o += char(c);
            }
        }
    }
    o += '"';
}

void writeNumber(std::string& o, double v)
{
    char buf[40];
    if (std::isfinite(v) && std::floor(v) == v && std::fabs(v) < 9.007199254740992e15) {
        std::snprintf(buf, sizeof buf, "%lld", (long long)v);
    } else if (std::isfinite(v)) {
        std::snprintf(buf, sizeof buf, "%.10g", v);
    } else {
        std::snprintf(buf, sizeof buf, "0");
    }
    o += buf;
}

void write(std::string& o, const Value& v, bool pretty, int indent)
{
    auto nl = [&](int ind) {
        if (!pretty) return;
        o += '\n';
        o.append(size_t(ind) * 2, ' ');
    };
    switch (v.type()) {
    case Value::Type::Null: o += "null"; break;
    case Value::Type::Bool: o += v.asBool() ? "true" : "false"; break;
    case Value::Type::Number: writeNumber(o, v.asNumber()); break;
    case Value::Type::String: writeString(o, v.asString()); break;
    case Value::Type::Array: {
        const auto& a = v.items();
        if (a.empty()) { o += "[]"; break; }
        // Short arrays of scalars stay on one line.
        bool flat = a.size() <= 16;
        for (const auto& e : a) flat = flat && !e.isArray() && !e.isObject();
        o += '[';
        for (size_t i = 0; i < a.size(); ++i) {
            if (i) o += flat && pretty ? ", " : ",";
            if (!flat) nl(indent + 1);
            write(o, a[i], pretty, indent + 1);
        }
        if (!flat) nl(indent);
        o += ']';
        break;
    }
    case Value::Type::Object: {
        const auto& m = v.members();
        if (m.empty()) { o += "{}"; break; }
        o += '{';
        for (size_t i = 0; i < m.size(); ++i) {
            if (i) o += ',';
            nl(indent + 1);
            writeString(o, m[i].first);
            o += pretty ? ": " : ":";
            write(o, m[i].second, pretty, indent + 1);
        }
        nl(indent);
        o += '}';
        break;
    }
    }
}

} // namespace

ParseResult parse(std::string_view text)
{
    // Skip UTF-8 BOM if present.
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.remove_prefix(3);
    return Parser(text).run();
}

std::string stringify(const Value& v, bool pretty)
{
    std::string o;
    write(o, v, pretty, 0);
    if (pretty) o += '\n';
    return o;
}

} // namespace infclick::json
