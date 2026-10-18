#pragma once
//
// Kucuk JSON okuyucu ve yazici: AMR yedek dosyasi ve kutuphane.json icin.
// Metinler UTF-8 std::string olarak tutulur. Nesne uyelerinin sirasi korunur.
//
// Okuyucu hosgoruludur ama sinirsiz degildir: ic ice derinlik 256 ile
// sinirli (bozuk ya da kotu niyetli dosya yigini tasirmasin).
//
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gm::json {

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

struct Member;

class Value {
public:
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<Member> obj;

    bool IsNull() const { return type == Type::Null; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // Nesnede anahtar ara; yoksa (ya da nesne degilse) nullptr.
    const Value* Get(std::string_view key) const;

    // Hosgorulu okuma: tip uymuyorsa varsayilan. Sayi yerine "12" gibi metin
    // ya da true/false gelirse makul donusum yapilir (AMR alanlari karisik).
    std::string AsString(std::string_view def = {}) const;
    double AsNumber(double def = 0.0) const;
    int64_t AsInt(int64_t def = 0) const;
    bool AsBool(bool def = false) const;

    // Yazma kolayliklari
    static Value Str(std::string s);
    static Value Num(double d);
    static Value Bool(bool v);
    static Value Arr();
    static Value Obj();
    Value& Add(std::string key, Value v);   // nesneye uye ekle
    Value& Push(Value v);                   // diziye eleman ekle
};

struct Member {
    std::string key;
    Value value;
};

// Hata varsa false. err gunluk icin ayrinti ("satir 3, sutun 14: ..."),
// errLine/errCol kullaniciya gosterilecek konum (1'den baslar).
// Deger sayisi da sinirli (kMaxValues): 64 MB'lik "[0,0,0,...]" DOM'da
// gigabaytlara sismesin.
inline constexpr size_t kMaxValues = 4'000'000;
bool Parse(std::string_view text, Value& out, std::string* err, size_t* errLine = nullptr, size_t* errCol = nullptr);

// Okunakli cikti (2 bosluk girinti). Yalniz skaler iceren diziler tek satir.
std::string Dump(const Value& v);

} // namespace gm::json
