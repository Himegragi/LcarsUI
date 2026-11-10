#include "settings_store.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
namespace settings_demo {
namespace {
std::u16string error_text(DWORD code) {
    wchar_t* text=nullptr;
    auto n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::u16string result;
    if (n && text) { result.assign(reinterpret_cast<char16_t*>(text),reinterpret_cast<char16_t*>(text)+n); LocalFree(text); }
    while (!result.empty() && (result.back() == u'\r' || result.back() == u'\n')) result.pop_back();
    if (result.empty()) { auto value=std::to_string(code); result.assign(value.begin(),value.end()); }
    return result;
}
}
LoadResult SettingsStore::load() const {
    if (auto e=fault("read")) return {LoadKind::unreadable,{},error_text(e),false};
    auto file=path(); HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (h==INVALID_HANDLE_VALUE) { auto e=GetLastError(); if (e==ERROR_FILE_NOT_FOUND || e==ERROR_PATH_NOT_FOUND) return {LoadKind::missing,{},{},false}; return {LoadKind::unreadable,{},error_text(e),false}; }
    LARGE_INTEGER size{}; if (!GetFileSizeEx(h,&size)) { auto e=GetLastError(); CloseHandle(h); return {LoadKind::unreadable,{},error_text(e),false}; }
    std::string bytes; bool too_large=size.QuadPart>1024*1024; DWORD count=0;
    if (!too_large) { bytes.resize(static_cast<size_t>(size.QuadPart)); if (!ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) || count!=bytes.size()) { auto e=GetLastError(); CloseHandle(h); return {LoadKind::unreadable,{},error_text(e),false}; } }
    CloseHandle(h);
    if (!too_large) if (auto doc=parse_settings(bytes)) return {LoadKind::loaded,std::move(doc),{},false};
    auto bad=file; bad+=L".bad"; auto injected=fault("rename"); bool renamed=!injected && MoveFileExW(file.c_str(),bad.c_str(),MOVEFILE_REPLACE_EXISTING)!=0; if (injected) SetLastError(injected);
    std::u16string reason = u"格式或欄位不合法";
    if (!renamed) reason += u"；原檔無法改名（" + error_text(GetLastError()) + u"）";
    return {LoadKind::corrupt,{},std::move(reason),renamed};
}
std::u16string SettingsStore::save(const SettingsDocument& doc) const {
    if (auto e=fault("directory")) return error_text(e);
    std::error_code ec; std::filesystem::create_directories(directory_,ec); if (ec) return error_text(static_cast<DWORD>(ec.value()));
    auto tmp=path(); tmp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    auto cleanup=[&] { DeleteFileW(tmp.c_str()); };
    if (auto e=fault("create")) { cleanup(); return error_text(e); }
    HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (h==INVALID_HANDLE_VALUE) { auto e=GetLastError(); cleanup(); return error_text(e); }
    auto bytes=write_settings(doc); DWORD written=0; DWORD e=0;
    e=fault("write"); if (!e && (!WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written!=bytes.size())) { e=GetLastError(); if (!e) e=ERROR_WRITE_FAULT; }
    if (!e) { e=fault("flush"); if (!e && !FlushFileBuffers(h)) e=GetLastError(); }
    auto close_error=fault("close"); if (!CloseHandle(h) && !e) e=GetLastError(); if (!e) e=close_error;
    if (!e) {
        for (int attempt=0; attempt<4; ++attempt) {
            e=fault("move"); if (!e && MoveFileExW(tmp.c_str(),path().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return {};
            if (!e) e=GetLastError(); if (attempt==3 || (e!=ERROR_SHARING_VIOLATION && e!=ERROR_ACCESS_DENIED && e!=ERROR_LOCK_VIOLATION)) break; Sleep(50);
        }
    }
    cleanup(); return error_text(e);
}
}
