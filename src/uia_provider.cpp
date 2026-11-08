#include "uia_provider.hpp"
#include "lcars/semantics.hpp"
#include <objbase.h>
#include <UIAutomation.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <algorithm>
#include <functional>
#include <thread>
#include <unordered_map>

namespace lcars::detail {
namespace {
HRESULT status_hr(Status s) {
    switch (s) {
    case Status::ok: return S_OK;
    case Status::not_found: return UIA_E_ELEMENTNOTAVAILABLE;
    case Status::disabled: return UIA_E_ELEMENTNOTENABLED;
    case Status::invalid_value: return E_INVALIDARG;
    case Status::not_supported: return UIA_E_NOTSUPPORTED;
    case Status::failed: return E_FAIL;
    case Status::wrong_thread: OutputDebugStringW(L"LcarsUI UIA: unexpected wrong thread\n"); return E_FAIL;
    default: return UIA_E_INVALIDOPERATION;
    }
}
class Provider;
struct Request {
    std::mutex mutex; std::condition_variable changed;
    enum class State { waiting, running, done, abandoned } state = State::waiting;
    std::function<HRESULT()> action; HRESULT result = E_FAIL;
};
}
struct UiaBridge::Impl : std::enable_shared_from_this<UiaBridge::Impl> {
    Ui* ui; HWND hwnd; DWORD thread; std::mutex mutex; std::atomic<bool> alive = true, identity_available = true;
    std::deque<std::shared_ptr<Request>> queue;
    std::vector<Provider*> providers;
    Subscription subscription;
    bool observing = false;
    HRESULT com_init = E_FAIL;
    explicit Impl(Ui& u, HWND w) : ui(&u), hwnd(w), thread(GetCurrentThreadId()) {}
    HRESULT call(std::function<HRESULT()> action) {
        if (GetCurrentThreadId() == thread) {
            if (!alive) return UIA_E_ELEMENTNOTAVAILABLE;
            try { return action(); } catch (...) { return E_FAIL; }
        }
        auto request = std::make_shared<Request>(); request->action = std::move(action);
        {
            std::lock_guard lock(mutex);
            if (!alive) return UIA_E_ELEMENTNOTAVAILABLE;
            queue.push_back(request);
            if (!PostMessageW(hwnd, uia_dispatch_message, 0, 0)) { queue.pop_back(); return UIA_E_ELEMENTNOTAVAILABLE; }
        }
        std::unique_lock lock(request->mutex);
        if (!request->changed.wait_for(lock, std::chrono::milliseconds(1000), [&] { return request->state != Request::State::waiting; })) {
            request->state = Request::State::abandoned; return UIA_E_TIMEOUT;
        }
        request->changed.wait(lock, [&] { return request->state == Request::State::done; });
        return request->result;
    }
    void dispatch() {
        for (;;) {
            std::shared_ptr<Request> request;
            { std::lock_guard lock(mutex); if (queue.empty()) break; request = queue.front(); queue.pop_front(); }
            { std::lock_guard lock(request->mutex); if (request->state == Request::State::abandoned) continue; request->state = Request::State::running; }
            HRESULT hr = UIA_E_ELEMENTNOTAVAILABLE;
            if (alive) { try { hr = request->action(); } catch (...) { hr = E_FAIL; } }
            { std::lock_guard lock(request->mutex); request->result = hr; request->state = Request::State::done; }
            request->changed.notify_all();
        }
    }
    Provider* provider(const NodeRef& ref);
    void observe();
};
namespace {
int control_type(Role r) {
    switch (r) {
    case Role::text: return UIA_EditControlTypeId;
    case Role::button: return UIA_ButtonControlTypeId;
    case Role::label: return UIA_TextControlTypeId;
    case Role::checkbox: return UIA_CheckBoxControlTypeId;
    case Role::radio_group: return UIA_GroupControlTypeId;
    case Role::radio_option: return UIA_RadioButtonControlTypeId;
    case Role::slider: return UIA_SliderControlTypeId;
    case Role::dropdown: return UIA_ComboBoxControlTypeId;
    case Role::dropdown_option: case Role::list_item: return UIA_ListItemControlTypeId;
    case Role::list: return UIA_ListControlTypeId;
    case Role::tabs: return UIA_TabControlTypeId;
    case Role::tab_item: return UIA_TabItemControlTypeId;
    case Role::progress: return UIA_ProgressBarControlTypeId;
    case Role::dialog: return UIA_WindowControlTypeId;
    case Role::tooltip: return UIA_ToolTipControlTypeId;
    }
    return UIA_CustomControlTypeId;
}
BSTR bstr(std::u16string_view s) { return SysAllocStringLen(reinterpret_cast<const wchar_t*>(s.data()), static_cast<UINT>(s.size())); }
BSTR bstr_id(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    BSTR result = SysAllocStringLen(nullptr, n);
    if (result && n) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result, n);
    return result;
}
class Provider final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot, public IInvokeProvider, public IToggleProvider, public ISelectionProvider, public ISelectionItemProvider, public IExpandCollapseProvider, public IRangeValueProvider, public IWindowProvider {
    std::atomic<ULONG> refs_{1};
public:
    std::shared_ptr<UiaBridge::Impl> ctx; NodeRef ref; int serial; std::uint64_t instance; std::atomic<bool> valid = true, disconnecting = false;
    Provider(std::shared_ptr<UiaBridge::Impl> c, NodeRef r, int n, std::uint64_t generation) : ctx(std::move(c)), ref(std::move(r)), serial(n), instance(generation) {}
    bool root() const { return ref.id.empty(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot) && root()) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
        else if (iid == __uuidof(ISelectionProvider)) *out = static_cast<ISelectionProvider*>(this);
        else if (iid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (iid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
        else if (iid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (iid == __uuidof(IWindowProvider)) *out = static_cast<IWindowProvider*>(this);
        if (!*out) return E_NOINTERFACE; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT check() {
        if (!valid || !ctx->alive) return UIA_E_ELEMENTNOTAVAILABLE;
        if (root()) return S_OK;
        auto node = ctx->ui->semantics().get(ref);
        if (!node) return status_hr(node.status);
        return node->instance != instance ? UIA_E_ELEMENTNOTAVAILABLE : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
        if (!o) return E_POINTER;
        if ((!valid || !ctx->alive) && !disconnecting) return UIA_E_ELEMENTNOTAVAILABLE;
        *o = ProviderOptions_ServerSideProvider; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        auto result = std::make_shared<IUnknown*>(nullptr); AddRef();
        auto hr = ctx->call([this, id, result] { auto h = check(); if (FAILED(h)) return h; if (root()) return S_OK;
            auto n = ctx->ui->semantics().get(ref); if (id == UIA_InvokePatternId && n->role == Role::button) *result = static_cast<IInvokeProvider*>(this);
            if (id == UIA_TogglePatternId && n->role == Role::checkbox) *result = static_cast<IToggleProvider*>(this);
            if (id == UIA_WindowPatternId && n->role == Role::dialog) *result = static_cast<IWindowProvider*>(this);
            if (id == UIA_RangeValuePatternId && (n->role == Role::slider || n->role == Role::progress)) *result = static_cast<IRangeValueProvider*>(this);
            if (id == UIA_ExpandCollapsePatternId && n->role == Role::dropdown) *result = static_cast<IExpandCollapseProvider*>(this);
            if (id == UIA_SelectionPatternId && (n->role == Role::radio_group || n->role == Role::dropdown || n->role == Role::list || n->role == Role::tabs)) *result = static_cast<ISelectionProvider*>(this);
            if (id == UIA_SelectionItemPatternId && (n->role == Role::radio_option || n->role == Role::dropdown_option || n->role == Role::list_item || n->role == Role::tab_item)) *result = static_cast<ISelectionItemProvider*>(this);
            if (*result) (*result)->AddRef(); return S_OK; }); Release();
        if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER; VariantInit(out); auto result = std::make_shared<VARIANT>(); VariantInit(result.get()); AddRef();
        auto hr = ctx->call([this, id, result] { auto h = check(); if (FAILED(h)) return h; if (root()) return S_OK;
            auto n = ctx->ui->semantics().get(ref); auto& v = *result;
            if (id == UIA_AutomationIdPropertyId) { v.vt=VT_BSTR; v.bstrVal=bstr_id(ref.item ? ref.item->id : ref.id); }
            else if (id == UIA_NamePropertyId || id == UIA_HelpTextPropertyId || id == UIA_FrameworkIdPropertyId) { v.vt=VT_BSTR; v.bstrVal=bstr(id == UIA_NamePropertyId ? n->name : id == UIA_HelpTextPropertyId ? n->description : u"LcarsUI"); }
            else if (id == UIA_ControlTypePropertyId) { v.vt=VT_I4; v.lVal=control_type(n->role); }
            else if (id == UIA_IsEnabledPropertyId || id == UIA_IsKeyboardFocusablePropertyId || id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsOffscreenPropertyId || id == UIA_IsDataValidForFormPropertyId || id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
                v.vt=VT_BOOL; bool b = id == UIA_IsEnabledPropertyId ? n->enabled : id == UIA_IsKeyboardFocusablePropertyId ? n->focusable : id == UIA_HasKeyboardFocusPropertyId ? n->focused && ::GetFocus() == ctx->hwnd : id == UIA_IsOffscreenPropertyId ? !n->bounds : id == UIA_IsDataValidForFormPropertyId ? !n->invalid : true; v.boolVal=b ? VARIANT_TRUE : VARIANT_FALSE;
            }
            else if (id == UIA_LabeledByPropertyId && !n->labelled_by.empty()) { v.vt=VT_UNKNOWN; v.punkVal=static_cast<IRawElementProviderSimple*>(ctx->provider(n->labelled_by.front())); v.punkVal->AddRef(); }
            return S_OK; }); Release();
        if (SUCCEEDED(hr)) *out = *result; else VariantClear(result.get()); return hr;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (!ctx->identity_available || (!valid && !disconnecting)) return UIA_E_ELEMENTNOTAVAILABLE;
        return root() ? UiaHostProviderFromHwnd(ctx->hwnd, out) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER; *out=nullptr; auto result=std::make_shared<Provider*>(nullptr); AddRef();
        auto hr=ctx->call([this,direction,result] { auto h=check(); if (FAILED(h)) return h;
            NodeRef parent; if (ref.item) parent.id=ref.id;
            else if (!root()) {
                auto roots = ctx->ui->semantics().children({});
                if (roots) for (const auto& candidate : *roots) {
                    auto n = ctx->ui->semantics().get(candidate);
                    if (!n || n->role != Role::dialog) continue;
                    auto members = ctx->ui->semantics().children(candidate);
                    if (members && std::find(members->begin(), members->end(), ref) != members->end()) { parent = candidate; break; }
                }
            }
            if (direction==NavigateDirection_Parent) { if (!root()) *result=ctx->provider(parent); }
            else if (direction==NavigateDirection_FirstChild || direction==NavigateDirection_LastChild) {
                auto children=ctx->ui->semantics().children(ref); if (!children) return status_hr(children.status);
                std::vector<NodeRef> visible; for (auto& child:*children) { auto n=ctx->ui->semantics().get(child); if (n && !n->hidden) visible.push_back(child); }
                if (!visible.empty()) *result=ctx->provider(direction==NavigateDirection_FirstChild ? visible.front() : visible.back());
            } else if (!root()) {
                auto children=ctx->ui->semantics().children(parent); if (!children) return status_hr(children.status);
                std::vector<NodeRef> visible; for (auto& child:*children) { auto n=ctx->ui->semantics().get(child); if (n && !n->hidden) visible.push_back(child); }
                auto it=std::find(visible.begin(),visible.end(),ref); if (it!=visible.end()) {
                    if (direction==NavigateDirection_NextSibling && ++it!=visible.end()) *result=ctx->provider(*it);
                    else if (direction==NavigateDirection_PreviousSibling && it!=visible.begin()) *result=ctx->provider(*--it);
                }
            }
            if (*result) (*result)->AddRef(); return S_OK; }); Release();
        if (SUCCEEDED(hr) && *result) *out=static_cast<IRawElementProviderFragment*>(*result); return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if ((!valid || !ctx->alive) && !disconnecting) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!disconnecting) {
            AddRef(); auto hr = ctx->call([this] { return check(); }); Release();
            if (FAILED(hr)) return hr;
        }
        if (root()) return S_OK;
        auto a = SafeArrayCreateVector(VT_I4, 0, 2); if (!a) return E_OUTOFMEMORY;
        LONG i = 0; int prefix = UiaAppendRuntimeId; SafeArrayPutElement(a, &i, &prefix); i = 1; SafeArrayPutElement(a, &i, &serial); *out = a; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override { if (!out) return E_POINTER; *out={}; auto result=std::make_shared<UiaRect>(); AddRef(); auto hr=ctx->call([this,result] { auto h=check(); if (FAILED(h)) return h; if (root()) { RECT r{}; GetWindowRect(ctx->hwnd,&r); *result={double(r.left),double(r.top),double(r.right-r.left),double(r.bottom-r.top)}; } else { auto n=ctx->ui->semantics().get(ref); if (n->bounds) { auto r=*n->bounds; POINT p{r.x,r.y}; ClientToScreen(ctx->hwnd,&p); *result={double(p.x),double(p.y),double(r.w),double(r.h)}; } } return S_OK; }); Release(); if (SUCCEEDED(hr)) *out=*result; return hr; }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        AddRef(); auto hr = ctx->call([this] { return check(); }); Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { AddRef(); auto hr=ctx->call([this] { auto h=check(); return FAILED(h) ? h : status_hr(ctx->ui->semantics().perform(ref,Verb::focus).status); }); Release(); return hr; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override { if (!out) return E_POINTER; *out=nullptr; auto result=std::make_shared<Provider*>(nullptr); AddRef(); auto hr=ctx->call([this,result] { auto h=check(); if (FAILED(h)) return h; *result=ctx->provider({}); (*result)->AddRef(); return S_OK; }); Release(); if (SUCCEEDED(hr)) *out=static_cast<IRawElementProviderFragmentRoot*>(*result); return hr; }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER; *out = nullptr; auto result = std::make_shared<Provider*>(nullptr); AddRef();
        auto hr = ctx->call([this, x, y, result] {
            auto h = check(); if (FAILED(h)) return h;
            POINT point{static_cast<LONG>(x), static_cast<LONG>(y)}; ScreenToClient(ctx->hwnd, &point);
            auto visit = [&](auto&& self, const NodeRef& parent) -> void {
                auto children = ctx->ui->semantics().children(parent); if (!children) return;
                for (const auto& child : *children) {
                    auto node = ctx->ui->semantics().get(child); if (!node || node->hidden) continue;
                    if (node->bounds && node->bounds->contains(point.x, point.y)) *result = ctx->provider(child);
                    self(self, child);
                }
            };
            visit(visit, {}); if (*result) (*result)->AddRef(); return S_OK;
        });
        Release(); if (SUCCEEDED(hr) && *result) *out = static_cast<IRawElementProviderFragment*>(*result); return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER; *out = nullptr; auto result = std::make_shared<Provider*>(nullptr); AddRef();
        auto hr = ctx->call([this, result] {
            auto h = check(); if (FAILED(h)) return h;
            if (::GetFocus() != ctx->hwnd) return S_OK;
            auto visit = [&](auto&& self, const NodeRef& parent) -> void {
                auto children = ctx->ui->semantics().children(parent); if (!children) return;
                for (const auto& child : *children) {
                    auto node = ctx->ui->semantics().get(child); if (!node || node->hidden) continue;
                    if (node->focused) *result = ctx->provider(child);
                    self(self, child);
                }
            };
            visit(visit, {}); if (*result) (*result)->AddRef(); return S_OK;
        });
        Release(); if (SUCCEEDED(hr) && *result) *out = static_cast<IRawElementProviderFragment*>(*result); return hr;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        auto result = std::make_shared<SAFEARRAY*>(nullptr); AddRef();
        auto hr = ctx->call([this, result] {
            auto h = check(); if (FAILED(h)) return h;
            auto children = ctx->ui->semantics().children(ref); if (!children) return status_hr(children.status);
            std::vector<IRawElementProviderSimple*> selected;
            for (auto& child : *children) { auto n = ctx->ui->semantics().get(child); if (n && n->selected) selected.push_back(static_cast<IRawElementProviderSimple*>(ctx->provider(child))); }
            auto a = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selected.size())); if (!a) return E_OUTOFMEMORY;
            for (LONG i = 0; i < static_cast<LONG>(selected.size()); ++i) SafeArrayPutElement(a, &i, selected[i]);
            *result = a; return S_OK;
        }); Release(); if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* out) override { if (!out) return E_POINTER; *out = FALSE; AddRef(); auto hr = ctx->call([this] { return check(); }); Release(); return hr; }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* out) override { if (!out) return E_POINTER; *out = TRUE; AddRef(); auto hr = ctx->call([this] { return check(); }); Release(); return hr; }
    HRESULT STDMETHODCALLTYPE Select() override { return perform(Verb::select); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return perform(Verb::select); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override {
        if (!out) return E_POINTER; auto result = std::make_shared<BOOL>(FALSE); AddRef();
        auto hr = ctx->call([this, result] { auto h = check(); if (FAILED(h)) return h; *result = ctx->ui->semantics().get(ref)->selected; return S_OK; });
        Release(); if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER; *out = nullptr; auto result = std::make_shared<Provider*>(nullptr); AddRef();
        auto hr = ctx->call([this, result] { auto h = check(); if (FAILED(h)) return h; *result = ctx->provider({ref.id}); (*result)->AddRef(); return S_OK; });
        Release(); if (SUCCEEDED(hr)) *out = static_cast<IRawElementProviderSimple*>(*result); return hr;
    }
    HRESULT STDMETHODCALLTYPE Expand() override { return perform(Verb::open); }
    HRESULT STDMETHODCALLTYPE Collapse() override { return perform(Verb::cancel); }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* out) override {
        if (!out) return E_POINTER; auto result = std::make_shared<ExpandCollapseState>(ExpandCollapseState_Collapsed); AddRef();
        auto hr = ctx->call([this, result] { auto h = check(); if (FAILED(h)) return h; *result = ctx->ui->semantics().get(ref)->expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed; return S_OK; });
        Release(); if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        if (!std::isfinite(value) || std::trunc(value) != value || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) return E_INVALIDARG;
        AddRef(); auto hr = ctx->call([this, value] { auto h = check(); return FAILED(h) ? h : status_hr(ctx->ui->semantics().perform(ref, Verb::set_value, static_cast<int>(value)).status); }); Release(); return hr;
    }
    HRESULT range_property(double* out, int which) {
        if (!out) return E_POINTER; auto result = std::make_shared<double>(0); AddRef();
        auto hr = ctx->call([this, result, which] { auto h = check(); if (FAILED(h)) return h; auto n = ctx->ui->semantics().get(ref);
            auto r = n->range.value_or(SliderRange{0, 100, 1});
            *result = which == 0 ? n->numeric_value.value_or(0) : which == 1 ? r.minimum : which == 2 ? r.maximum : r.step; return S_OK; });
        Release(); if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override { return range_property(out, 0); }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { return range_property(out, 1); }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { return range_property(out, 2); }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override { return range_property(out, 3); }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override { return range_property(out, 3); }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
        if (!out) return E_POINTER; auto result = std::make_shared<BOOL>(FALSE); AddRef();
        auto hr = ctx->call([this, result] { auto h = check(); if (FAILED(h)) return h; auto n = ctx->ui->semantics().get(ref); *result = n->read_only || n->role == Role::progress; return S_OK; });
        Release(); if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE Close() override { return perform(Verb::cancel); }
    HRESULT STDMETHODCALLTYPE SetVisualState(WindowVisualState) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE WaitForInputIdle(int, BOOL* out) override { if (!out) return E_POINTER; *out = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CanMaximize(BOOL* out) override { if (!out) return E_POINTER; *out = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CanMinimize(BOOL* out) override { if (!out) return E_POINTER; *out = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsModal(BOOL* out) override { if (!out) return E_POINTER; *out = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsTopmost(BOOL* out) override { if (!out) return E_POINTER; *out = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_WindowVisualState(WindowVisualState* out) override { if (!out) return E_POINTER; *out = WindowVisualState_Normal; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_WindowInteractionState(WindowInteractionState* out) override { if (!out) return E_POINTER; *out = WindowInteractionState_Running; return S_OK; }
    HRESULT STDMETHODCALLTYPE Invoke() override { return perform(Verb::invoke); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return perform(Verb::toggle); }
    HRESULT perform(Verb v) { AddRef(); auto hr=ctx->call([this,v] { auto h=check(); return FAILED(h) ? h : status_hr(ctx->ui->semantics().perform(ref,v).status); }); Release(); return hr; }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override { if (!out) return E_POINTER; auto result=std::make_shared<ToggleState>(ToggleState_Off); AddRef(); auto hr=ctx->call([this,result] { auto h=check(); if (FAILED(h)) return h; auto n=ctx->ui->semantics().get(ref); *result=n->checked.value_or(false) ? ToggleState_On : ToggleState_Off; return S_OK; }); Release(); if (SUCCEEDED(hr)) *out=*result; return hr; }
};
}
Provider* UiaBridge::Impl::provider(const NodeRef& ref) {
    auto n = ref.id.empty() ? Query<SemanticNode>{} : ui->semantics().get(ref);
    auto generation = n ? n->instance : 0;
    for (auto p : providers) if (p->valid && p->ref == ref && p->instance == generation) return p;
    static std::atomic<int> serial{1}; auto p=new Provider(shared_from_this(),ref,serial++,generation); providers.push_back(p); return p;
}
void UiaBridge::Impl::observe() {
    if (observing) return;
    // Each semantic observer snapshots the tree around operations, so install
    // this one only once a client has requested the window's UIA root.
    observing = true;
    std::weak_ptr<Impl> weak = shared_from_this();
    subscription = ui->semantics().subscribe([weak](const SemanticEvent& event) {
        auto ctx = weak.lock(); if (!ctx || !ctx->alive) return;
        if (event.kind == SemanticEventKind::structure_changed) {
            for (auto p : ctx->providers) if (!p->root() && p->valid && FAILED(p->check())) {
                p->disconnecting = true; p->valid = false; UiaDisconnectProvider(p); p->disconnecting = false;
            }
        }
        if (!UiaClientsAreListening()) return;
        if (event.kind == SemanticEventKind::property_changed && event.property == "selected") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n) return;
            auto p = ctx->provider(event.ref); p->AddRef(); bool selected = n->selected;
            std::thread([p, selected] {
                auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                before.vt = after.vt = VT_BOOL; before.boolVal = selected ? VARIANT_FALSE : VARIANT_TRUE; after.boolVal = selected ? VARIANT_TRUE : VARIANT_FALSE;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_SelectionItemIsSelectedPropertyId, before, after);
                if (selected) UiaRaiseAutomationEvent(p, UIA_SelectionItem_ElementSelectedEventId);
                p->Release(); if (SUCCEEDED(init)) CoUninitialize();
            }).detach();
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "checked") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n || !n->checked) return;
            auto p = ctx->provider(event.ref); p->AddRef(); bool checked = *n->checked;
            std::thread([p, checked] {
                auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                VARIANT old_value, new_value; VariantInit(&old_value); VariantInit(&new_value);
                old_value.vt = new_value.vt = VT_I4;
                new_value.lVal = checked ? ToggleState_On : ToggleState_Off;
                old_value.lVal = checked ? ToggleState_Off : ToggleState_On;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_ToggleToggleStatePropertyId, old_value, new_value);
                p->Release(); if (SUCCEEDED(init)) CoUninitialize();
            }).detach();
        }
    });
}
UiaBridge::UiaBridge(Ui& ui, HWND hwnd) : impl_(std::make_shared<Impl>(ui,hwnd)) {
    impl_->com_init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}
UiaBridge::~UiaBridge() { close(); }
LRESULT UiaBridge::get_object(WPARAM w, LPARAM l) {
    if (static_cast<LONG>(l) != UiaRootObjectId) return 0;
    impl_->observe();
    return UiaReturnRawElementProvider(impl_->hwnd, w, l, impl_->provider({}));
}
void UiaBridge::dispatch() { impl_->dispatch(); }
void UiaBridge::close() {
    if (!impl_) return; auto ctx=impl_; { std::lock_guard lock(ctx->mutex); if (!ctx->alive) return; ctx->alive=false; }
    ctx->subscription.reset(); ctx->dispatch(); UiaReturnRawElementProvider(ctx->hwnd,0,0,nullptr);
    for (auto p:ctx->providers) { p->disconnecting=true; p->valid=false; UiaDisconnectProvider(p); p->disconnecting=false; p->Release(); } ctx->providers.clear();
    ctx->identity_available = false;
    if (SUCCEEDED(ctx->com_init)) { CoUninitialize(); ctx->com_init = E_FAIL; }
}
}
