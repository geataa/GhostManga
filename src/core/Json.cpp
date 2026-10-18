#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gm::json {
namespace {

constexpr int kMaxDepth = 256;

// Not: std::from_chars / to_chars (double) exe'ye ~240 KB tablo ekliyor.
// strtod ve %g yeterli; uygulama setlocale cagirmadigi icin CRT "C"
// yerel ayarinda kalir, ondalik ayirac daima nokta.
bool ToDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double d = strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(d)) return false;
    out = d;
    return true;
}

void AppendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(std::string_view t) : m_t(t) {}

    bool Run(Value& out, std::string* err, size_t* errLine, size_t* errCol) {
        SkipWs();
        if (!ParseValue(out, 0)) return Fail(err, errLine, errCol);
        SkipWs();
        if (m_p != m_t.size()) {
            m_msg = "belgenin sonunda fazladan veri";
            return Fail(err, errLine, errCol);
        }
        return true;
    }

private:
    bool Fail(std::string* err, size_t* errLine, size_t* errCol) {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < m_p && i < m_t.size(); ++i) {
            if (m_t[i] == '\n') { ++line; col = 1; }
            else if (((uint8_t)m_t[i] & 0xC0) != 0x80) ++col;  // UTF-8 devam baytlarini sayma
        }
        if (errLine) *errLine = line;
        if (errCol) *errCol = col;
        if (err) {
            char buf[96];
            snprintf(buf, sizeof(buf), "satir %zu, sutun %zu: ", line, col);
            *err = buf + (m_msg.empty() ? std::string("gecersiz JSON") : m_msg);
        }
        return false;
    }

    bool Error(const char* msg) {
        if (m_msg.empty()) m_msg = msg;
        return false;
    }

    bool Count() {
        if (++m_values > kMaxValues) return Error("cok fazla oge");
        return true;
    }

    void SkipWs() {
        while (m_p < m_t.size()) {
            const char c = m_t[m_p];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_p;
            else break;
        }
    }

    bool Lit(const char* word) {
        const size_t n = strlen(word);
        if (m_t.compare(m_p, n, word) != 0) return Error("beklenmeyen karakter");
        m_p += n;
        return true;
    }

    bool ParseValue(Value& v, int depth) {
        if (depth > kMaxDepth) return Error("ic ice derinlik cok fazla");
        if (m_p >= m_t.size()) return Error("belge beklenmedik yerde bitti");
        if (!Count()) return false;
        switch (m_t[m_p]) {
        case '{': return ParseObject(v, depth);
        case '[': return ParseArray(v, depth);
        case '"': v.type = Type::String; return ParseString(v.str);
        case 't': v.type = Type::Bool; v.b = true; return Lit("true");
        case 'f': v.type = Type::Bool; v.b = false; return Lit("false");
        case 'n': v.type = Type::Null; return Lit("null");
        default: return ParseNumber(v);
        }
    }

    bool ParseObject(Value& v, int depth) {
        v.type = Type::Object;
        ++m_p; // {
        SkipWs();
        if (m_p < m_t.size() && m_t[m_p] == '}') { ++m_p; return true; }
        for (;;) {
            SkipWs();
            if (m_p >= m_t.size() || m_t[m_p] != '"') return Error("anahtar bekleniyordu");
            Member m;
            if (!ParseString(m.key)) return false;
            SkipWs();
            if (m_p >= m_t.size() || m_t[m_p] != ':') return Error("':' bekleniyordu");
            ++m_p;
            SkipWs();
            if (!ParseValue(m.value, depth + 1)) return false;
            v.obj.push_back(std::move(m));
            SkipWs();
            if (m_p >= m_t.size()) return Error("belge beklenmedik yerde bitti");
            if (m_t[m_p] == ',') { ++m_p; continue; }
            if (m_t[m_p] == '}') { ++m_p; return true; }
            return Error("',' ya da '}' bekleniyordu");
        }
    }

    bool ParseArray(Value& v, int depth) {
        v.type = Type::Array;
        ++m_p; // [
        SkipWs();
        if (m_p < m_t.size() && m_t[m_p] == ']') { ++m_p; return true; }
        for (;;) {
            SkipWs();
            Value e;
            if (!ParseValue(e, depth + 1)) return false;
            v.arr.push_back(std::move(e));
            SkipWs();
            if (m_p >= m_t.size()) return Error("belge beklenmedik yerde bitti");
            if (m_t[m_p] == ',') { ++m_p; continue; }
            if (m_t[m_p] == ']') { ++m_p; return true; }
            return Error("',' ya da ']' bekleniyordu");
        }
    }

    bool Hex4(uint32_t& out) {
        if (m_p + 4 > m_t.size()) return Error("eksik \\u kacisi");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_t[m_p++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (uint32_t)(c - 'A' + 10);
            else return Error("gecersiz \\u kacisi");
        }
        return true;
    }

    bool ParseString(std::string& s) {
        ++m_p; // "
        s.clear();
        for (;;) {
            // Kacissiz parcayi tek seferde kopyala
            const size_t start = m_p;
            while (m_p < m_t.size() && m_t[m_p] != '"' && m_t[m_p] != '\\') ++m_p;
            s.append(m_t.data() + start, m_p - start);
            if (m_p >= m_t.size()) return Error("kapanmamis metin");
            if (m_t[m_p] == '"') { ++m_p; return true; }

            ++m_p; // ters bolu
            if (m_p >= m_t.size()) return Error("kapanmamis metin");
            const char e = m_t[m_p++];
            switch (e) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // Vekil cifti: ardindan \uDC00-\uDFFF gelmeli
                    uint32_t lo = 0;
                    if (m_p + 6 <= m_t.size() && m_t[m_p] == '\\' && m_t[m_p + 1] == 'u') {
                        const size_t save = m_p;
                        m_p += 2;
                        if (Hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            m_msg.clear();
                            m_p = save;
                            cp = 0xFFFD;
                        }
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    cp = 0xFFFD; // tek basina alt vekil
                }
                AppendUtf8(s, cp);
                break;
            }
            default: return Error("gecersiz kacis dizisi");
            }
        }
    }

    bool ParseNumber(Value& v) {
        const size_t start = m_p;
        if (m_p < m_t.size() && m_t[m_p] == '-') ++m_p;
        while (m_p < m_t.size()) {
            const char c = m_t[m_p];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++m_p;
            else break;
        }
        if (m_p == start) return Error("beklenmeyen karakter");
        double d = 0.0;
        if (!ToDouble(std::string(m_t.data() + start, m_p - start), d)) {
            m_p = start;
            return Error("gecersiz sayi");
        }
        v.type = Type::Number;
        v.num = d;
        return true;
    }

    std::string_view m_t;
    size_t m_p = 0;
    std::string m_msg;
    size_t m_values = 0;
};

void DumpString(std::string& o, const std::string& s) {
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
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else {
                o += (char)c;
            }
        }
    }
    o += '"';
}

void DumpNumber(std::string& o, double d) {
    if (!std::isfinite(d)) { o += "null"; return; }
    if (d == std::floor(d) && std::fabs(d) < 9007199254740992.0) {
        o += std::to_string((long long)d);
        return;
    }
    // En kisa gidis-donuslu yazim: once 15 hane, geri okunamazsa 17.
    char buf[40];
    snprintf(buf, sizeof(buf), "%.15g", d);
    double back = 0.0;
    if (!ToDouble(buf, back) || back != d) snprintf(buf, sizeof(buf), "%.17g", d);
    o += buf;
}

bool IsScalar(const Value& v) { return v.type != Type::Array && v.type != Type::Object; }

void DumpValue(std::string& o, const Value& v, int indent) {
    auto nl = [&](int n) {
        o += '\n';
        o.append((size_t)n * 2, ' ');
    };
    switch (v.type) {
    case Type::Null: o += "null"; break;
    case Type::Bool: o += v.b ? "true" : "false"; break;
    case Type::Number: DumpNumber(o, v.num); break;
    case Type::String: DumpString(o, v.str); break;
    case Type::Array: {
        if (v.arr.empty()) { o += "[]"; break; }
        bool flat = true;
        for (const Value& e : v.arr) flat = flat && IsScalar(e);
        o += '[';
        for (size_t i = 0; i < v.arr.size(); ++i) {
            if (i) o += flat ? ", " : ",";
            if (!flat) nl(indent + 1);
            DumpValue(o, v.arr[i], indent + 1);
        }
        if (!flat) nl(indent);
        o += ']';
        break;
    }
    case Type::Object: {
        if (v.obj.empty()) { o += "{}"; break; }
        o += '{';
        for (size_t i = 0; i < v.obj.size(); ++i) {
            if (i) o += ',';
            nl(indent + 1);
            DumpString(o, v.obj[i].key);
            o += ": ";
            DumpValue(o, v.obj[i].value, indent + 1);
        }
        nl(indent);
        o += '}';
        break;
    }
    }
}

} // namespace

const Value* Value::Get(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const Member& m : obj)
        if (m.key == key) return &m.value;
    return nullptr;
}

std::string Value::AsString(std::string_view def) const {
    switch (type) {
    case Type::String: return str;
    case Type::Number: {
        std::string o;
        DumpNumber(o, num);
        return o;
    }
    default: return std::string(def);
    }
}

double Value::AsNumber(double def) const {
    switch (type) {
    case Type::Number: return std::isfinite(num) ? num : def;
    case Type::Bool: return b ? 1.0 : 0.0;
    case Type::String: {
        double d = 0.0;
        return ToDouble(str, d) ? d : def;
    }
    default: return def;
    }
}

int64_t Value::AsInt(int64_t def) const {
    const double d = AsNumber(NAN);
    if (!std::isfinite(d) || std::fabs(d) > 9.2e18) return def;
    return (int64_t)d;
}

bool Value::AsBool(bool def) const {
    switch (type) {
    case Type::Bool: return b;
    case Type::Number: return num != 0.0;
    case Type::String:
        if (str == "true" || str == "1") return true;
        if (str == "false" || str == "0" || str.empty()) return false;
        return def;
    default: return def;
    }
}

Value Value::Str(std::string s) { Value v; v.type = Type::String; v.str = std::move(s); return v; }
Value Value::Num(double d) { Value v; v.type = Type::Number; v.num = d; return v; }
Value Value::Bool(bool b) { Value v; v.type = Type::Bool; v.b = b; return v; }
Value Value::Arr() { Value v; v.type = Type::Array; return v; }
Value Value::Obj() { Value v; v.type = Type::Object; return v; }

Value& Value::Add(std::string key, Value v) {
    obj.push_back(Member{ std::move(key), std::move(v) });
    return obj.back().value;
}

Value& Value::Push(Value v) {
    arr.push_back(std::move(v));
    return arr.back();
}

bool Parse(std::string_view text, Value& out, std::string* err, size_t* errLine, size_t* errCol) {
    out = Value();
    Parser p(text);
    if (!p.Run(out, err, errLine, errCol)) {
        out = Value();
        return false;
    }
    return true;
}

std::string Dump(const Value& v) {
    std::string o;
    DumpValue(o, v, 0);
    o += '\n';
    return o;
}

} // namespace gm::json
