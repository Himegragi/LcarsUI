#pragma once
#include <windows.h>
#include <memory>
#include <functional>
namespace isoduo { class Ui; struct Frame; }
namespace isoduo::detail {
class UiaBridge {
public:
    struct Impl;
    UiaBridge(Ui&, HWND, std::function<const Frame&()>);
    ~UiaBridge();
    LRESULT getObject(WPARAM, LPARAM);
    void dispatch();
    void close();
private:
    std::shared_ptr<Impl> impl_;
};
constexpr UINT uiaDispatchMessage = WM_APP + 46;
}
