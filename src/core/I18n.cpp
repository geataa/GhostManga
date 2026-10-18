#include "core/I18n.h"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <array>
#include <cwctype>

namespace gm::i18n {
namespace {
struct Message { int id; const wchar_t* text[15]; };
const Message kMessages[] = {
#include "core/Translations.inc"
};
std::atomic<Lang> g_language{Lang::Tr};
using Lookup=std::unordered_map<std::wstring,const Message*>;
const Lookup& Sources() {
    static const Lookup values=[](){Lookup out;for(const auto& message:kMessages){out.emplace(message.text[0],&message);out.emplace(message.text[1],&message);}return out;}();return values;
}
const Lookup& Localized(Lang language) {
    static const std::array<Lookup,15> values=[](){std::array<Lookup,15> out;for(int i=0;i<15;++i)for(const auto& message:kMessages)out[i].emplace(message.text[i],&message);return out;}();return values[(int)language-1];
}
bool Decoration(wchar_t c) {return iswspace(c) || c==L'+' || c==L'‹' || c==L'›' || (c>=0x2190 && c<=0x2bff) || (c>=0xd800 && c<=0xdfff) || c==0xfe0f;}
std::wstring Convert(const std::wstring& source,bool english) {
    const auto lang=g_language.load();const int index=english?1:(int)lang-1;
    const auto& sources=Sources();const auto& local=Localized(lang);
    auto find=[&](const std::wstring& key)->const Message*{auto it=sources.find(key);if(it!=sources.end())return it->second;it=local.find(key);return it==local.end()?nullptr:it->second;};
    if(const auto* message=find(source))return message->text[index];
    size_t start=0,end=source.size();while(start<end && Decoration(source[start]))++start;while(end>start && iswspace(source[end-1]))--end;
    const auto body=source.substr(start,end-start);
    if(const auto* message=find(body))return source.substr(0,start)+message->text[index]+source.substr(end);
    return source;
}
}
void Set(Lang preferred) {
    if(preferred>Lang::Auto && preferred<Lang::Count_){g_language=preferred;return;}
    Lang detected=Lang::En;
    switch(PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_TURKISH:detected=Lang::Tr;break;case LANG_GERMAN:detected=Lang::De;break;
    case LANG_FRENCH:detected=Lang::Fr;break;case LANG_SPANISH:detected=Lang::Es;break;
    case LANG_ITALIAN:detected=Lang::It;break;case LANG_PORTUGUESE:detected=Lang::Pt;break;
    case LANG_RUSSIAN:detected=Lang::Ru;break;case LANG_POLISH:detected=Lang::Pl;break;
    case LANG_DUTCH:detected=Lang::Nl;break;case LANG_JAPANESE:detected=Lang::Ja;break;
    case LANG_KOREAN:detected=Lang::Ko;break;case LANG_CHINESE:detected=Lang::Zh;break;
    case LANG_INDONESIAN:detected=Lang::Id;break;case LANG_AZERI:detected=Lang::Az;break;
    }
    g_language=detected;
}
Lang Current(){return g_language.load();}
Lang Next(){const int next=(int)Current()+1;return next>=(int)Lang::Count_?Lang::Tr:(Lang)next;}
bool IsTurkish(){return Current()==Lang::Tr;}
const wchar_t* T(S id) {
    const int i=(int)id;if(i<0 || i>=(int)S::Count_)return L"?";
    return kMessages[i].text[(int)Current()-1];
}
std::wstring Text(const std::wstring& source){return Convert(source,false);}
std::wstring English(const std::wstring& source){return Convert(source,true);}
const wchar_t* Literal(const wchar_t* source) {
    static std::mutex mutex;static std::unordered_map<std::wstring,std::wstring> cache;
    const std::wstring key=std::to_wstring((int)Current())+L"\x1f"+source;
    std::lock_guard<std::mutex> lock(mutex);auto it=cache.find(key);if(it==cache.end())it=cache.emplace(key,Text(source)).first;return it->second.c_str();
}
} // namespace gm::i18n
