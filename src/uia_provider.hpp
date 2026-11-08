#pragma once
#include <windows.h>
#include <memory>
namespace lcars { class Ui; }
namespace lcars::detail {
class UiaBridge {
public:
    struct Impl;
    UiaBridge(Ui&, HWND);
    ~UiaBridge();
    LRESULT get_object(WPARAM, LPARAM);
    void dispatch();
    void close();
private:
    std::shared_ptr<Impl> impl_;
};
constexpr UINT uia_dispatch_message = WM_APP + 46;
}
