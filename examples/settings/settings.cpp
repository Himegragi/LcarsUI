#include "core_internal.hpp"
#include "settings.hpp"
#include <atomic>
#include <charconv>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace settings {
using namespace lcars;
namespace {
std::vector<Control> declarations() {
    return {
        {"nav.general", Kind::navigation, u"一般設定", "general", {}},
        {"nav.preferences", Kind::navigation, u"使用偏好", "preferences", {}},
        {"nav.profiles", Kind::navigation, u"設定範本", "profiles", {}},
        {"search", Kind::text, u"搜尋設定", "search", {}},
        {"profile.name", Kind::text, u"顯示名稱", "general", u"星艦工作站"},
        {"profile.workspace", Kind::text, u"工作區名稱", "general", u"我的專案"},
        {"preferences.autosave", Kind::toggle, u"自動儲存偏好", "preferences", {}, true, false, true},
        {"preferences.notifications", Kind::toggle, u"顯示通知偏好", "preferences", {}},
        {"preferences.font", Kind::button, u"切換字型：正黑體／細明體", "preferences", {}},
        {"preferences.smaller", Kind::button, u"縮小文字", "preferences", {}},
        {"preferences.larger", Kind::button, u"放大文字", "preferences", {}},
        {"preset.focus", Kind::button, u"專注工作 — 關閉通知、標準文字", "profiles", {}},
        {"preset.reading", Kind::button, u"舒適閱讀 — 細明體、較大文字", "profiles", {}},
        {"preset.default", Kind::button, u"預設設定 — 正黑體、標準文字", "profiles", {}},
        {"undo", Kind::button, u"復原", "footer", {}},
        {"redo", Kind::button, u"重做", "footer", {}},
        {"reset", Kind::button, u"還原預設", "footer", {}},
        {"discard", Kind::button, u"放棄變更", "footer", {}},
        {"save", Kind::button, u"儲存設定", "footer", {}},
        {"dialog.cancel", Kind::button, u"取消", "dialog", {}},
        {"dialog.confirm", Kind::button, u"確認", "dialog", {}}
    };
}
std::string utf8(std::u16string_view text) {
    std::string out;
    for (std::size_t i=0; i<text.size(); ++i) {
        unsigned cp=text[i];
        if (cp>=0xd800 && cp<=0xdbff) cp=0x10000+((cp-0xd800)<<10)+(text[++i]-0xdc00);
        if (cp<0x80) out+=static_cast<char>(cp);
        else if (cp<0x800) { out+=static_cast<char>(0xc0|(cp>>6)); out+=static_cast<char>(0x80|(cp&63)); }
        else if (cp<0x10000) {
            out+=static_cast<char>(0xe0|(cp>>12)); out+=static_cast<char>(0x80|((cp>>6)&63)); out+=static_cast<char>(0x80|(cp&63));
        } else {
            out+=static_cast<char>(0xf0|(cp>>18)); out+=static_cast<char>(0x80|((cp>>12)&63));
            out+=static_cast<char>(0x80|((cp>>6)&63)); out+=static_cast<char>(0x80|(cp&63));
        }
    }
    return out;
}
bool decode(std::string_view bytes, std::u16string& out) {
    out.clear();
    for (std::size_t i=0; i<bytes.size();) {
        auto c=static_cast<unsigned char>(bytes[i++]); unsigned cp=c, count=0, minimum=0;
        if (c>=0xc2 && c<=0xdf) { cp=c&31; count=1; minimum=0x80; }
        else if (c>=0xe0 && c<=0xef) { cp=c&15; count=2; minimum=0x800; }
        else if (c>=0xf0 && c<=0xf4) { cp=c&7; count=3; minimum=0x10000; }
        else if (c>=0x80) return false;
        for (unsigned n=0; n<count; ++n) {
            if (i==bytes.size()) return false;
            auto b=static_cast<unsigned char>(bytes[i++]); if ((b&0xc0)!=0x80) return false;
            cp=(cp<<6)|(b&63);
        }
        if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
        if (cp<0x10000) out+=static_cast<char16_t>(cp);
        else { cp-=0x10000; out+=static_cast<char16_t>(0xd800+(cp>>10)); out+=static_cast<char16_t>(0xdc00+(cp&1023)); }
    }
    return valid_single_line(out);
}
bool valid(const Values& v) {
    return !v.name.empty() && v.name.find_first_not_of(u" ")!=std::u16string::npos && v.name.size()<=128 &&
           !v.workspace.empty() && v.workspace.find_first_not_of(u" ")!=std::u16string::npos && v.workspace.size()<=256 &&
           valid_single_line(v.name) && valid_single_line(v.workspace) && v.font_size>=16 && v.font_size<=26 && v.font_size%2==0;
}
}
Result read_file(const std::filesystem::path& path, Values& output) {
    std::error_code error;
    if (!std::filesystem::exists(path,error)) return error ? Result{Status::failed,u"無法讀取設定位置"} : Result{};
    if (std::filesystem::file_size(path,error)>32768 || error) return {Status::failed,u"設定檔過大或無法讀取"};
    std::ifstream input(path,std::ios::binary); std::string line;
    if (!input || !std::getline(input,line) || line!="LcarsUI settings 1") return {Status::invalid_value,u"設定檔格式無效，原檔案已保留"};
    std::map<std::string,std::string> fields;
    while (std::getline(input,line)) {
        auto split=line.find('=');
        if (split==std::string::npos || !fields.emplace(line.substr(0,split),line.substr(split+1)).second)
            return {Status::invalid_value,u"設定檔欄位無效，原檔案已保留"};
    }
    if (input.bad() || fields.size()!=6 || !fields.contains("name") || !fields.contains("workspace") ||
        !fields.contains("autosave") || !fields.contains("notifications") || !fields.contains("font") || !fields.contains("size"))
        return {Status::invalid_value,u"設定檔不完整，原檔案已保留"};
    Values candidate;
    if (!decode(fields["name"],candidate.name) || !decode(fields["workspace"],candidate.workspace))
        return {Status::invalid_value,u"設定檔文字無效"};
    auto boolean=[&](const char* key, bool& value) { auto s=fields.at(key); if(s!="0" && s!="1")return false; value=s=="1";return true; };
    if (!boolean("autosave",candidate.autosave) || !boolean("notifications",candidate.notifications) || !boolean("font",candidate.ming_font))
        return {Status::invalid_value,u"設定檔選項無效"};
    const auto& size=fields["size"]; auto [end,ec]=std::from_chars(size.data(),size.data()+size.size(),candidate.font_size);
    if (ec!=std::errc{} || end!=size.data()+size.size() || !valid(candidate)) return {Status::invalid_value,u"設定檔數值無效"};
    output=std::move(candidate); return {};
}
Result write_file(const std::filesystem::path& path, const Values& v) {
    if (!valid(v)) return {Status::invalid_value,u"名稱不可空白；顯示名稱上限 128 字元，工作區上限 256 字元"};
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),error);
    if(error) return {Status::failed,u"無法建立設定資料夾"};
    std::ostringstream bytes;
    bytes<<"LcarsUI settings 1\nname="<<utf8(v.name)<<"\nworkspace="<<utf8(v.workspace)
         <<"\nautosave="<<v.autosave<<"\nnotifications="<<v.notifications<<"\nfont="<<v.ming_font<<"\nsize="<<v.font_size<<"\n";
    // A same-directory exclusive temporary file protects the previous save on failure.
    auto temporary=path; temporary+=L".tmp";
#ifdef _WIN32
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return {Status::failed,u"無法建立暫存檔；請檢查權限或既有 .tmp 檔案"};
    auto data=bytes.str(); DWORD written=0;
    bool ok=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr) && written==data.size();
    if(ok) ok=FlushFileBuffers(file)!=FALSE;
    if(!CloseHandle(file))ok=false;
    if(ok)ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok) { DeleteFileW(temporary.c_str()); return {Status::failed,u"儲存失敗；原設定保持不變"}; }
#else
    std::ofstream file(temporary,std::ios::binary|std::ios::noreplace);
    if(!file) return {Status::failed,u"無法建立暫存檔"};
    file<<bytes.str(); file.close();
    if(!file) { std::filesystem::remove(temporary,error); return {Status::failed,u"寫入設定失敗"}; }
    std::filesystem::rename(temporary,path,error);
    if(error) { std::filesystem::remove(temporary,error); return {Status::failed,u"無法替換設定檔"}; }
#endif
    return {};
}
App::App(std::filesystem::path file):ui(declarations()),file_(std::move(file)) {
    auto result=read_file(file_,saved_);
    if(!result)status_=result.message;
    apply(saved_); previous_=values();
    ui.on_change([this]{ changed(); });
    for (const auto& page : {"general","preferences","profiles"})
        detail::access(ui).bind(std::string("nav.")+page,[this,page]{ page_=page; detail::access(ui).replace_text("search",u""); return Result{}; });
    detail::access(ui).bind("save",[this]{return save();});
    detail::access(ui).bind("undo",[this]{return undo();});
    detail::access(ui).bind("redo",[this]{return redo();});
    detail::access(ui).bind("reset",[this]{ dialog_=Dialog::defaults;detail::access(ui).modal(true);return Result{}; });
    detail::access(ui).bind("discard",[this]{ dialog_=Dialog::discard;detail::access(ui).modal(true);return Result{}; });
    detail::access(ui).bind("dialog.cancel",[this]{ dialog_=Dialog::none;detail::access(ui).modal(false);return Result{}; });
    detail::access(ui).bind("dialog.confirm",[this]{
        auto choice=dialog_;dialog_=Dialog::none;detail::access(ui).modal(false);
        if(choice==Dialog::defaults)return defaults();
        apply(saved_);changed();status_=u"已還原為最近儲存的設定";return Result{};
    });
    for(auto id:{"preferences.autosave","preferences.notifications"})
        detail::access(ui).bind(id,[this,id]{detail::access(ui).check(id,!detail::access(ui).find(id)->checked);changed();return Result{};});
    detail::access(ui).bind("preferences.font",[this]{auto v=values();v.ming_font=!v.ming_font;apply(v);changed();return Result{};});
    detail::access(ui).bind("preferences.smaller",[this]{auto v=values();v.font_size=std::max(16,v.font_size-2);apply(v);changed();return Result{};});
    detail::access(ui).bind("preferences.larger",[this]{auto v=values();v.font_size=std::min(26,v.font_size+2);apply(v);changed();return Result{};});
    detail::access(ui).bind("preset.focus",[this]{auto v=values();v.notifications=false;v.autosave=true;v.font_size=18;apply(v);changed();return Result{};});
    detail::access(ui).bind("preset.reading",[this]{auto v=values();v.font_size=24;v.ming_font=true;apply(v);changed();return Result{};});
    detail::access(ui).bind("preset.default",[this]{dialog_=Dialog::defaults;detail::access(ui).modal(true);return Result{};});
    refresh_commands();
}
Values App::values() const {
    return {detail::access(ui).find("profile.name")->value,detail::access(ui).find("profile.workspace")->value,
            detail::access(ui).find("preferences.autosave")->checked,detail::access(ui).find("preferences.notifications")->checked,
            ming_font_,font_size_};
}
void App::apply(const Values& v) {
    detail::access(ui).replace_text("profile.name",v.name); detail::access(ui).replace_text("profile.workspace",v.workspace);
    detail::access(ui).check("preferences.autosave",v.autosave);detail::access(ui).check("preferences.notifications",v.notifications);
    // Presentation preferences are application-owned, not inferred from the skin.
    ming_font_=v.ming_font; font_size_=v.font_size;
}
void App::changed() {
    auto current=values();
    if(current!=previous_) {
        if(undo_.size()==100)undo_.erase(undo_.begin());
        undo_.push_back(previous_);redo_.clear();previous_=current;status_=u"有尚未儲存的變更";
    }
    refresh_commands();
}
void App::refresh_commands() {
    detail::access(ui).enable("save",dirty());detail::access(ui).enable("discard",dirty());detail::access(ui).enable("undo",!undo_.empty());detail::access(ui).enable("redo",!redo_.empty());
    detail::access(ui).enable("preferences.smaller",values().font_size>16);detail::access(ui).enable("preferences.larger",values().font_size<26);
}
Result App::save() {
    auto result=write_file(file_,values());
    status_=result ? u"設定已儲存" : result.message;
    if(result)saved_=values();
    refresh_commands();return result;
}
Result App::undo() {
    if(undo_.empty())return {Status::disabled,u"沒有可復原的變更"};
    auto v=undo_.back();undo_.pop_back();redo_.push_back(values());apply(v);previous_=v;
    status_=u"已復原上一個變更";refresh_commands();return {};
}
Result App::redo() {
    if(redo_.empty())return {Status::disabled,u"沒有可重做的變更"};
    auto v=redo_.back();redo_.pop_back();undo_.push_back(values());apply(v);previous_=v;
    status_=u"已重做變更";refresh_commands();return {};
}
Result App::defaults() { apply(Values{});changed();status_=u"已套用預設設定；儲存後才會寫入檔案";return {}; }
Screen App::screen() const {
    Screen s;
    s.page=page_;s.dirty=dirty();s.font_size=values().font_size;s.ming_font=values().ming_font;s.status=status_;
    if(page_=="general"){s.heading=u"一般設定";s.description=u"為你的工作空間設定名稱。變更會在儲存後保留。";}
    else if(page_=="preferences"){
        s.heading=u"使用偏好";
        auto size=std::to_string(values().font_size);
        s.description=(values().ming_font?u"目前使用細明體，字級 ":u"目前使用正黑體，字級 ")+std::u16string(size.begin(),size.end())+u"。開關僅保存偏好值。";
    }
    else{s.heading=u"設定範本";s.description=u"選擇一組偏好，再依工作方式調整。套用後可復原。";}
    if(dialog_!=Dialog::none){
        s.dialog_title=dialog_==Dialog::defaults?u"還原預設設定？":u"放棄尚未儲存的變更？";
        s.dialog_message=u"確認後可使用「復原」取回；檔案要等儲存才會更新。";
    }
    return s;
}
}
