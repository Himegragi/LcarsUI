#include "lcars/fonts.hpp"
#include "font_files.hpp"
#include <windows.h>
#include <dwrite_3.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <array>
#include <map>
#include <set>

namespace lcars {
using Microsoft::WRL::ComPtr;
namespace {
std::u16string name_of(IDWriteLocalizedStrings* names, const wchar_t* locale, bool fallback) {
    UINT32 index=0, length=0; BOOL found=FALSE;
    if (!names || FAILED(names->FindLocaleName(locale,&index,&found)) || (!found && !fallback)) return {};
    if (!found) index=0;
    if (FAILED(names->GetStringLength(index,&length))) return {};
    std::wstring value(length+1,L'\0');
    if (FAILED(names->GetString(index,value.data(),length+1))) return {};
    value.resize(length); return {value.begin(),value.end()};
}
detail::FontFile file_of(IDWriteFontFaceReference* face) {
    ComPtr<IDWriteFontFile> file; ComPtr<IDWriteFontFileLoader> loader; ComPtr<IDWriteLocalFontFileLoader> local;
    const void* key=nullptr; UINT32 key_size=0,length=0;
    if (face->GetLocality()!=DWRITE_LOCALITY_LOCAL || FAILED(face->GetFontFile(&file)) || FAILED(file->GetLoader(&loader)) ||
        FAILED(loader.As(&local)) || FAILED(file->GetReferenceKey(&key,&key_size)) ||
        FAILED(local->GetFilePathLengthFromKey(key,key_size,&length))) return {};
    std::wstring path(length+1,L'\0');
    if (FAILED(local->GetFilePathFromKey(key,key_size,path.data(),length+1))) return {};
    path.resize(length);
    detail::FontFile result{path,face->GetFontFaceIndex()};
    ComPtr<IDWriteFontFaceReference1> variable;
    if(SUCCEEDED(face->QueryInterface(IID_PPV_ARGS(&variable)))) {
        UINT32 count=variable->GetFontAxisValueCount();
        std::vector<DWRITE_FONT_AXIS_VALUE> values(count);
        if(count && SUCCEEDED(variable->GetFontAxisValues(values.data(),count)))
            for(const auto& axis:values) result.axes.push_back({static_cast<std::uint32_t>(axis.axisTag),axis.value});
    }
    return result;
}
bool usable(const detail::FontFile& file, bool& eligible) {
    eligible=false;
    HANDLE f=CreateFileW(file.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; HANDLE mapping=nullptr; void* data=nullptr; FT_Library lib=nullptr; FT_Face face=nullptr; bool ok=false;
    if(GetFileSizeEx(f,&size) && size.QuadPart>0 && size.QuadPart<=LONG_MAX &&
       (mapping=CreateFileMappingW(f,nullptr,PAGE_READONLY,0,0,nullptr)) && (data=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0)) &&
       !FT_Init_FreeType(&lib) && !FT_New_Memory_Face(lib,static_cast<const FT_Byte*>(data),static_cast<FT_Long>(size.QuadPart),file.face_index,&face)) {
        ok=true; eligible=face->charmap && face->charmap->encoding!=FT_ENCODING_MS_SYMBOL;
        for (unsigned c='0';c<='9' && eligible;++c) eligible=FT_Get_Char_Index(face,c)!=0;
        for (unsigned c='A';c<='Z' && eligible;++c) eligible=FT_Get_Char_Index(face,c)!=0;
        for (unsigned c='a';c<='z' && eligible;++c) eligible=FT_Get_Char_Index(face,c)!=0;
    }
    if(face) FT_Done_Face(face); if(lib) FT_Done_FreeType(lib); if(data) UnmapViewOfFile(data); if(mapping) CloseHandle(mapping); CloseHandle(f);
    return ok;
}
// styled[family][slot]: the family's real face for bold (slot bit 0) and italic (slot bit 1) text; a slot is empty
// when the family has no such file, and then the text backend synthesizes the look from the regular face.
struct Snapshot { std::vector<FontFamily> families; std::map<std::u16string,detail::FontFile> files; std::map<std::u16string,std::array<detail::FontFile,4>> styled; std::set<std::u16string> valid; bool available=false; HRESULT error=S_OK; };
const Snapshot& snapshot() {
 static const Snapshot value=[] {
    Snapshot out; ComPtr<IDWriteFactory6> factory; ComPtr<IDWriteFontSet1> set;
    out.error=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory6),reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    if(SUCCEEDED(out.error)) out.error=factory->GetSystemFontSet(FALSE,&set);
    if(FAILED(out.error)) {
#ifndef NDEBUG
        OutputDebugStringW(L"font.catalog_unavailable\n");
#endif
        return out;
    }
    out.available=true;
    std::map<std::u16string,int> scores;
    std::map<std::u16string,std::array<int,4>> styled_scores;
    std::set<std::u16string> listed;
    for(UINT32 i=0;i<set->GetFontCount();++i) {
        ComPtr<IDWriteFontFaceReference> ref; ComPtr<IDWriteLocalizedStrings> names; BOOL exists=FALSE;
        if(FAILED(set->GetFontFaceReference(i,&ref))) continue;
        if(FAILED(set->GetPropertyValues(i,DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME,&exists,&names)) || !exists) {
            names.Reset(); exists=FALSE;
            if(FAILED(set->GetPropertyValues(i,DWRITE_FONT_PROPERTY_ID_WIN32_FAMILY_NAME,&exists,&names)) || !exists) continue;
        }
        auto key=name_of(names.Get(),L"en-us",true); if(key.empty()) continue;
        auto file=file_of(ref.Get()); if(file.path.empty()) continue;
        out.files.try_emplace(key,file);
        ComPtr<IDWriteFontFace3> dwface;
        if(FAILED(ref->CreateFontFace(&dwface))) continue;
        {  // the family's real Bold / Italic / Bold Italic faces: the nearest upright-normal-stretch weight to 700 or 400
            const int weight=static_cast<int>(dwface->GetWeight());
            const bool slanted=dwface->GetStyle()!=DWRITE_FONT_STYLE_NORMAL, normal_width=dwface->GetStretch()==DWRITE_FONT_STRETCH_NORMAL;
            for(unsigned slot=1;slot<4;++slot) {
                const bool want_bold=slot&1, want_italic=(slot&2)!=0;
                if(!normal_width || want_bold!=(weight>=600) || want_italic!=slanted) continue;
                const int slot_score=std::abs(weight-(want_bold?700:400));
                auto [held_it,fresh]=styled_scores.try_emplace(key); if(fresh) held_it->second.fill(1<<30); auto& held=held_it->second;
                if(slot_score>=held[slot]) continue;
                out.styled[key][slot]=file; held[slot]=slot_score;
            }
        }
        int score=std::abs(static_cast<int>(dwface->GetWeight())-400) +
                  1000*std::abs(static_cast<int>(dwface->GetStretch())-static_cast<int>(DWRITE_FONT_STRETCH_NORMAL)) +
                  (dwface->GetStyle()==DWRITE_FONT_STYLE_NORMAL?0:10000);
        auto prior=scores.find(key);
        if(prior!=scores.end() && score>=prior->second && listed.count(key)) continue;
        bool eligible=false; if(!usable(file,eligible)) continue;
        out.valid.insert(key);
        if(prior==scores.end() || score<prior->second) { out.files[key]=file; scores[key]=score; }
        if(eligible && !dwface->IsSymbolFont() && listed.insert(key).second) { auto display=name_of(names.Get(),L"zh-tw",false); out.families.push_back({key,display.empty()?key:display}); }
    }
    std::sort(out.families.begin(),out.families.end(),[](const auto& a,const auto& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT,NORM_IGNORECASE,reinterpret_cast<const wchar_t*>(a.display_name.c_str()),-1,reinterpret_cast<const wchar_t*>(b.display_name.c_str()),-1,nullptr,nullptr,0)==CSTR_LESS_THAN;
    });
    return out;
 }(); return value;
}
}
bool FontCatalog::contains(std::u16string_view key) const noexcept {
 return std::any_of(families_.begin(),families_.end(),[&](const auto& f){return f.key==key;});
}
FontResolution FontCatalog::resolve(const PresentationSettings& settings) const {
    FontResolution result;
    result.requested=settings.font_family;
    result.requested_available=settings.font_family.empty() || contains(settings.font_family);
    const auto& valid=snapshot().valid;
    auto append=[&](std::u16string_view name) {
        if(valid.count(std::u16string(name)) && std::find(result.fallback.begin(),result.fallback.end(),name)==result.fallback.end())
            result.fallback.emplace_back(name);
    };
    if(result.requested_available && !settings.font_family.empty()) append(settings.font_family);
    for(auto name:{u"Segoe UI",u"Microsoft JhengHei",u"MingLiU",u"MingLiU-ExtB",u"Microsoft YaHei",u"SimSun",u"Yu Gothic",u"Malgun Gothic",u"Segoe UI Symbol",u"Segoe UI Emoji"}) append(name);
    if(!result.fallback.empty()) { result.primary=result.fallback.front(); result.fallback.erase(result.fallback.begin()); }
    return result;
}
namespace detail {
long system_font_catalog_error() { return snapshot().error; }
FontFile system_styled_font_file(std::u16string_view family, bool bold, bool italic) {
 const unsigned slot=(bold?1u:0u)|(italic?2u:0u);
 if(slot==0) return system_font_file(family);
 const auto& state=snapshot();
 if(state.available) {
    auto it=state.styled.find(std::u16string(family));
    return it==state.styled.end()?FontFile{}:it->second[slot];
 }
 // Without the font catalog only the two families the framework names first have well-known file names.
 static const std::map<std::u16string,std::array<std::wstring,4>> names={
    {u"Segoe UI",{L"",L"segoeuib.ttf",L"segoeuii.ttf",L"segoeuiz.ttf"}},{u"Microsoft JhengHei",{L"",L"msjhbd.ttc",L"",L""}}};
 auto found=names.find(std::u16string(family)); if(found==names.end() || found->second[slot].empty()) return {};
 PWSTR folder=nullptr; if(FAILED(SHGetKnownFolderPath(FOLDERID_Fonts,0,nullptr,&folder))) return {};
 std::filesystem::path path(folder); CoTaskMemFree(folder); path/=found->second[slot];
 std::error_code error;
 return std::filesystem::exists(path,error)?FontFile{path,0}:FontFile{};
}
FontFile system_font_file(std::u16string_view family) {
 const auto& state=snapshot(); auto it=state.files.find(std::u16string(family));
 if(it!=state.files.end()) return it->second;
 if(state.available) return {};
 static const std::map<std::u16string,std::wstring> names={
    {u"Segoe UI",L"segoeui.ttf"},{u"Microsoft JhengHei",L"msjh.ttc"},{u"MingLiU",L"mingliu.ttc"},
    {u"MingLiU-ExtB",L"mingliub.ttc"},{u"Microsoft YaHei",L"msyh.ttc"},{u"SimSun",L"simsun.ttc"},
    {u"Yu Gothic",L"YuGothR.ttc"},{u"Malgun Gothic",L"malgun.ttf"},{u"Segoe UI Symbol",L"seguisym.ttf"},{u"Segoe UI Emoji",L"seguiemj.ttf"}};
 auto filename=names.find(std::u16string(family)); if(filename==names.end()) return {};
 PWSTR folder=nullptr; if(FAILED(SHGetKnownFolderPath(FOLDERID_Fonts,0,nullptr,&folder))) return {};
 std::filesystem::path path(folder); CoTaskMemFree(folder); path/=filename->second;
 std::error_code error;
 return std::filesystem::exists(path,error)?FontFile{path,0}:FontFile{};
}
}
const FontCatalog& system_font_catalog() {
 static const FontCatalog catalog=[] { FontCatalog result; result.families_=snapshot().families; return result; }(); return catalog;
}
}
