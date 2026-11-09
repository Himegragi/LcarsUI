#include "utf.hpp"
#include "uia_provider.hpp"
#include "lcars/semantics.hpp"
#include "core_internal.hpp"
#include "lcars/layout.hpp"
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
IUnknown* make_selection_provider(Provider*);
// UIA can reenter the provider, so raise outside the UI thread. One draining
// worker preserves semantic event order without blocking host shutdown.
struct EventQueue : std::enable_shared_from_this<EventQueue> {
    std::mutex mutex;
    std::deque<std::function<void()>> pending;
    bool running = false;
    void post(std::function<void()> action) {
        std::lock_guard lock(mutex); pending.push_back(std::move(action));
        if (running) return;
        running = true;
        std::thread([self = shared_from_this()] {
            auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (;;) {
                std::function<void()> next;
                { std::lock_guard guard(self->mutex);
                  if (self->pending.empty()) { self->running = false; break; }
                  next = std::move(self->pending.front()); self->pending.pop_front(); }
                try { next(); } catch (...) { }
            }
            if (SUCCEEDED(init)) CoUninitialize();
        }).detach();
    }
};
struct Request {
    std::mutex mutex; std::condition_variable changed;
    enum class State { waiting, running, done, abandoned } state = State::waiting;
    std::function<HRESULT()> action; HRESULT result = E_FAIL;
};
}
struct UiaBridge::Impl : std::enable_shared_from_this<UiaBridge::Impl> {
    Ui* ui; std::function<const Frame&()> frame; HWND hwnd; DWORD thread; std::mutex mutex; std::atomic<bool> alive = true, identity_available = true;
    std::deque<std::shared_ptr<Request>> queue;
    std::shared_ptr<EventQueue> events = std::make_shared<EventQueue>();
    std::shared_ptr<EventQueue> focus_events = std::make_shared<EventQueue>();
    std::shared_ptr<EventQueue> structure_events = std::make_shared<EventQueue>();
    std::shared_ptr<EventQueue> overlay_events = std::make_shared<EventQueue>();
    std::shared_ptr<EventQueue> text_events = std::make_shared<EventQueue>();
    std::vector<Provider*> providers;
    Subscription subscription;
    bool observing = false;
    HRESULT com_init = E_FAIL;
    explicit Impl(Ui& u, HWND w, std::function<const Frame&()> f) : ui(&u), frame(std::move(f)), hwnd(w), thread(GetCurrentThreadId()) {}
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
BSTR bstr_id(const std::string& s) { return bstr(utf16_from_utf8(s)); }
class Provider final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot, public IInvokeProvider, public IToggleProvider, public ISelectionItemProvider, public IExpandCollapseProvider, public IRangeValueProvider, public IWindowProvider, public IValueProvider, public ITextProvider {
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
        else if (iid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (iid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
        else if (iid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (iid == __uuidof(IWindowProvider)) *out = static_cast<IWindowProvider*>(this);
        else if (iid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else if (iid == __uuidof(ITextProvider)) *out = static_cast<ITextProvider*>(this);
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
            if (id == UIA_SelectionPatternId && (n->role == Role::radio_group || n->role == Role::dropdown || n->role == Role::list || n->role == Role::tabs)) { *result = make_selection_provider(this); return *result ? S_OK : E_OUTOFMEMORY; }
            if (id == UIA_SelectionItemPatternId && (n->role == Role::radio_option || n->role == Role::dropdown_option || n->role == Role::list_item || n->role == Role::tab_item)) *result = static_cast<ISelectionItemProvider*>(this);
            if (id == UIA_ValuePatternId && n->role == Role::text) *result = static_cast<IValueProvider*>(this);
            if (id == UIA_TextPatternId && n->role == Role::text) *result = static_cast<ITextProvider*>(this);
            if (*result) (*result)->AddRef(); return S_OK; }); Release();
        if (SUCCEEDED(hr)) *out = *result; return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER; VariantInit(out); auto result = std::make_shared<VARIANT>(); VariantInit(result.get()); AddRef();
        auto hr = ctx->call([this, id, result] { auto h = check(); if (FAILED(h)) return h; if (root()) return S_OK;
            auto n = ctx->ui->semantics().get(ref); auto& v = *result;
            if (id == UIA_AutomationIdPropertyId) { v.vt=VT_BSTR; v.bstrVal=bstr_id(ref.item ? ref.item->id : ref.id); }
            else if (id == UIA_NamePropertyId || id == UIA_HelpTextPropertyId || id == UIA_FrameworkIdPropertyId) { v.vt=VT_BSTR; v.bstrVal=bstr(id == UIA_NamePropertyId ? n->name : id == UIA_HelpTextPropertyId ? n->description : u"LcarsUI"); }
            else if (id == UIA_AcceleratorKeyPropertyId) { v.vt=VT_BSTR; v.bstrVal=bstr(n->accelerator_key); }
            else if (id == UIA_ControlTypePropertyId) { v.vt=VT_I4; v.lVal=control_type(n->role); }
            else if (id == UIA_IsEnabledPropertyId || id == UIA_IsKeyboardFocusablePropertyId || id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsOffscreenPropertyId || id == UIA_IsDataValidForFormPropertyId || id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
                v.vt=VT_BOOL; bool b = id == UIA_IsEnabledPropertyId ? n->enabled : id == UIA_IsKeyboardFocusablePropertyId ? n->focusable : id == UIA_HasKeyboardFocusPropertyId ? n->focused && ::GetFocus() == ctx->hwnd : id == UIA_IsOffscreenPropertyId ? !n->bounds : id == UIA_IsDataValidForFormPropertyId ? !n->invalid : true; v.boolVal=b ? VARIANT_TRUE : VARIANT_FALSE;
            }
            else if (id == UIA_LabeledByPropertyId && !n->labelled_by.empty()) { v.vt=VT_UNKNOWN; v.punkVal=static_cast<IRawElementProviderSimple*>(ctx->provider(n->labelled_by.front())); v.punkVal->AddRef(); }
            else if (id == UIA_DescribedByPropertyId && !n->described_by.empty()) {
                auto array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(n->described_by.size()));
                if (!array) return E_OUTOFMEMORY;
                for (LONG i = 0; i < static_cast<LONG>(n->described_by.size()); ++i) {
                    auto provider = static_cast<IRawElementProviderSimple*>(ctx->provider(n->described_by[i]));
                    auto result = SafeArrayPutElement(array, &i, provider);
                    if (FAILED(result)) { SafeArrayDestroy(array); return result; }
                }
                v.vt = VT_ARRAY | VT_UNKNOWN; v.parray = array;
            }
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
    HRESULT selection_get(SAFEARRAY** out) {
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
    HRESULT selection_can_select_multiple(BOOL* out) { if (!out) return E_POINTER; *out = FALSE; AddRef(); auto hr = ctx->call([this] { return check(); }); Release(); return hr; }
    HRESULT selection_is_required(BOOL* out) {
        if (!out) return E_POINTER; auto result = std::make_shared<BOOL>(FALSE); AddRef();
        auto hr = ctx->call([this, result] { auto h = check(); if (FAILED(h)) return h; *result = ctx->ui->semantics().get(ref)->selection_required; return S_OK; });
        Release(); if (SUCCEEDED(hr)) *out = *result; return hr;
    }
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
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY**) override;
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY**) override;
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider**) override;
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint, ITextRangeProvider**) override;
    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider**) override;
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* out) override {
        if (!out) return E_POINTER; *out = SupportedTextSelection_None;
        AddRef(); auto hr = ctx->call([this] { return check(); }); Release();
        if (SUCCEEDED(hr)) *out = SupportedTextSelection_Single; return hr;
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        if (!value) return E_INVALIDARG;
        std::u16string copy(reinterpret_cast<const char16_t*>(value));
        AddRef(); auto hr = ctx->call([this, copy = std::move(copy)] {
            auto h = check(); return FAILED(h) ? h : status_hr(ctx->ui->semantics().perform(ref, Verb::set_value, copy).status);
        }); Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (!out) return E_POINTER; *out = nullptr;
        auto result = std::make_shared<std::u16string>(); AddRef();
        auto hr = ctx->call([this, result] { auto h = check(); if (FAILED(h)) return h;
            auto n = ctx->ui->semantics().get(ref); *result = n->value; return S_OK; }); Release();
        if (SUCCEEDED(hr)) { *out = bstr(*result); if (!*out) return E_OUTOFMEMORY; } return hr;
    }
    HRESULT STDMETHODCALLTYPE Invoke() override { return perform(Verb::invoke); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return perform(Verb::toggle); }
    HRESULT perform(Verb v) { AddRef(); auto hr=ctx->call([this,v] { auto h=check(); return FAILED(h) ? h : status_hr(ctx->ui->semantics().perform(ref,v).status); }); Release(); return hr; }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override { if (!out) return E_POINTER; auto result=std::make_shared<ToggleState>(ToggleState_Off); AddRef(); auto hr=ctx->call([this,result] { auto h=check(); if (FAILED(h)) return h; auto n=ctx->ui->semantics().get(ref); *result=n->checked.value_or(false) ? ToggleState_On : ToggleState_Off; return S_OK; }); Release(); if (SUCCEEDED(hr)) *out=*result; return hr; }
};


// ITextProvider and ISelectionProvider both declare GetSelection with different
// meanings. A tear-off keeps their COM identities and results distinct.
class SelectionProvider final : public ISelectionProvider {
    std::atomic<ULONG> refs_{1}; Provider* owner_;
public:
    explicit SelectionProvider(Provider* owner) : owner_(owner) { owner_->AddRef(); }
    ~SelectionProvider() { owner_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ISelectionProvider)) return E_NOINTERFACE;
        *out = static_cast<ISelectionProvider*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override { return owner_->selection_get(out); }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* out) override { return owner_->selection_can_select_multiple(out); }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* out) override { return owner_->selection_is_required(out); }
};
IUnknown* make_selection_provider(Provider* owner) { return static_cast<ISelectionProvider*>(new SelectionProvider(owner)); }

class TextRange final : public ITextRangeProvider {
    std::atomic<ULONG> refs{1};
    Provider *owner;
    unsigned first, last;
    std::u16string version;
    std::uint64_t revision;
    HRESULT call(std::function<HRESULT(const SemanticNode &)> action) {
        AddRef();
        auto hr = owner->ctx->call([this, action = std::move(action)] {
            auto h = owner->check();
            if (FAILED(h))
                return h;
            auto n = owner->ctx->ui->semantics().get(owner->ref);
            if (!n || n->text_revision != revision)
                return static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
            return action(*n);
        });
        Release();
        return hr;
    }
    const Placement *placement() const {
        return find_placement(owner->ctx->frame(), owner->ref.id);
    }
    std::vector<unsigned> stops() const {
        std::vector<unsigned> out;
        auto session = access(*owner->ctx->ui).edit(owner->ref.id);
        if (auto p = placement())
            for (auto [offset, x] : p->run.stops) {
                if (session && session->composing && offset > session->begin()) {
                    auto end = session->begin() + static_cast<unsigned>(session->preedit.size());
                    if (offset < end)
                        continue;
                    offset = offset - static_cast<unsigned>(session->preedit.size()) + session->end() -
                             session->begin();
                }
                if (offset <= version.size())
                    out.push_back(offset);
            }
        if (out.empty() || out.front() != 0)
            out.insert(out.begin(), 0);
        if (out.back() != version.size())
            out.push_back(static_cast<unsigned>(version.size()));
        return out;
    }
    static bool endpoint(TextPatternRangeEndpoint e) {
        return e == TextPatternRangeEndpoint_Start || e == TextPatternRangeEndpoint_End;
    }

  public:
    TextRange(Provider *p, unsigned a, unsigned b, std::u16string v)
        : owner(p), first(a), last(b), version(std::move(v)),
          revision(p->ctx->ui->semantics().get(p->ref)->text_revision) {
        owner->AddRef();
    }
    ~TextRange() {
        owner->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(ITextRangeProvider))
            return E_NOINTERFACE;
        *out = static_cast<ITextRangeProvider *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return call([&](const auto &) {
            *out = new TextRange(owner, first, last, version);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider *r, BOOL *out) override {
        if (!out)
            return E_POINTER;
        *out = FALSE;
        auto o = dynamic_cast<TextRange *>(r);
        if (!o || o->owner != owner)
            return E_INVALIDARG;
        return call([&](const auto &) {
            if (o->revision != revision)
                return static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
            *out = first == o->first && last == o->last;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint e, ITextRangeProvider *r,
                                               TextPatternRangeEndpoint f, int *out) override {
        if (!out)
            return E_POINTER;
        *out = 0;
        auto o = dynamic_cast<TextRange *>(r);
        if (!o || o->owner != owner || !endpoint(e) || !endpoint(f))
            return E_INVALIDARG;
        return call([&](const auto &) {
            if (o->revision != revision)
                return static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
            auto a = e == TextPatternRangeEndpoint_Start ? first : last,
                 b = f == TextPatternRangeEndpoint_Start ? o->first : o->last;
            *out = a < b ? -1 : a > b ? 1 : 0;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override {
        if (unit < TextUnit_Character || unit > TextUnit_Document)
            return E_INVALIDARG;
        return call([&](const auto &) {
            if (unit != TextUnit_Character) {
                first = 0;
                last = static_cast<unsigned>(version.size());
            } else {
                auto v = stops();
                auto it = std::upper_bound(v.begin(), v.end(), first);
                if (it == v.end()) {
                    last = v.back();
                    first = v.size() > 1 ? v[v.size() - 2] : last;
                } else {
                    last = *it;
                    first = it == v.begin() ? 0 : *std::prev(it);
                }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL,
                                            ITextRangeProvider **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return call([](const auto &) { return static_cast<HRESULT>(UIA_E_NOTSUPPORTED); });
    }
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignore,
                                       ITextRangeProvider **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!text || !SysStringLen(text))
            return E_INVALIDARG;
        std::wstring query(text, SysStringLen(text));
        return call([&](const auto &) {
            auto len = query.size();
            if (len <= last - first)
                for (unsigned i = 0; i <= last - first - len; ++i) {
                    auto at = backward ? last - static_cast<unsigned>(len) - i : first + i;
                    if (CompareStringOrdinal(reinterpret_cast<const wchar_t *>(version.data() + at),
                                             static_cast<int>(len), query.data(), static_cast<int>(len),
                                             ignore) == CSTR_EQUAL) {
                        *out = new TextRange(owner, at, at + static_cast<unsigned>(len), version);
                        break;
                    }
                }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID id, VARIANT *out) override {
        if (!out)
            return E_POINTER;
        VariantInit(out);
        return call([&](const auto &n) {
            if (id == UIA_IsReadOnlyAttributeId) {
                out->vt = VT_BOOL;
                out->boolVal = n.read_only ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            out->vt = VT_UNKNOWN;
            return UiaGetReservedNotSupportedValue(&out->punkVal);
        });
    }
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return call([&](const auto &) {
            std::vector<Rect> rectangles;
            if (auto p = placement(); p && first != last) {
                auto editor = p->field.editor;
                editor.x += p->bounds.x; editor.y += p->bounds.y;
                auto clip = intersect(editor, p->clip);
                auto add = [&](unsigned a, unsigned b) {
                    int left = p->caret_x(a), right = p->caret_x(b);
                    auto r = intersect({left, editor.y, std::max(0, right-left), editor.h}, clip);
                    if (r.w > 0 && r.h > 0) rectangles.push_back(r);
                };
                auto session = access(*owner->ctx->ui).edit(owner->ref.id);
                if (session && session->composing) {
                    auto begin = session->begin(), end = session->end();
                    if (first < begin) add(first, std::min(last, begin));
                    if (last > end) {
                        auto offset = static_cast<unsigned>(session->preedit.size());
                        add(std::max(first, end) - end + begin + offset, last - end + begin + offset);
                    }
                } else add(first, last);
            }
            *out = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(rectangles.size() * 4));
            if (!*out) return E_OUTOFMEMORY;
            LONG i = 0;
            for (auto r : rectangles) {
                POINT pt{r.x, r.y}; ClientToScreen(owner->ctx->hwnd, &pt);
                double values[] = {double(pt.x), double(pt.y), double(r.w), double(r.h)};
                for (double value : values) { SafeArrayPutElement(*out, &i, &value); ++i; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return call([&](const auto &) {
            *out = static_cast<IRawElementProviderSimple *>(owner);
            owner->AddRef();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetText(int max, BSTR *out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (max < -1)
            return E_INVALIDARG;
        return call([&](const auto &) {
            auto n = last - first;
            if (max >= 0)
                n = std::min(n, static_cast<unsigned>(max));
            *out = bstr(std::u16string_view(version).substr(first, n));
            return *out ? S_OK : E_OUTOFMEMORY;
        });
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint e, TextUnit unit, int count,
                                                 int *moved) override {
        if (!moved)
            return E_POINTER;
        *moved = 0;
        if (!endpoint(e) || unit < TextUnit_Character || unit > TextUnit_Document)
            return E_INVALIDARG;
        return call([&](const auto &) {
            auto v = unit == TextUnit_Character
                         ? stops()
                         : std::vector<unsigned>{0, static_cast<unsigned>(version.size())};
            unsigned pos = e == TextPatternRangeEndpoint_Start ? first : last;
            auto i = static_cast<long long>(std::lower_bound(v.begin(), v.end(), pos) - v.begin());
            i = std::min(i, static_cast<long long>(v.size() - 1));
            auto j = std::clamp(i + count, 0LL, static_cast<long long>(v.size() - 1));
            *moved = static_cast<int>(j - i);
            pos = v[static_cast<size_t>(j)];
            if (e == TextPatternRangeEndpoint_Start) {
                first = pos;
                last = std::max(last, first);
            } else {
                last = pos;
                first = std::min(first, last);
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int *moved) override {
        if (!moved)
            return E_POINTER;
        *moved = 0;
        if (unit < TextUnit_Character || unit > TextUnit_Document)
            return E_INVALIDARG;
        return call([&](const auto &) {
            if (!count)
                return S_OK;
            if (unit != TextUnit_Character) {
                first = 0;
                last = static_cast<unsigned>(version.size());
                return S_OK;
            }
            auto v = stops();
            bool empty = first == last;
            auto it = std::upper_bound(v.begin(), v.end(), first);
            long long i = it == v.begin() ? 0 : it - v.begin() - 1;
            long long max = std::max(0LL, static_cast<long long>(v.size()) - (empty ? 1 : 2));
            i = std::min(i, max);
            auto j = std::clamp(i + count, 0LL, max);
            first = v[static_cast<size_t>(j)];
            last = empty ? first : v[static_cast<size_t>(j + 1)];
            *moved = static_cast<int>(j - i);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint e, ITextRangeProvider *r,
                                                  TextPatternRangeEndpoint f) override {
        auto o = dynamic_cast<TextRange *>(r);
        if (!o || o->owner != owner || !endpoint(e) || !endpoint(f))
            return E_INVALIDARG;
        return call([&](const auto &) {
            if (revision != o->revision)
                return static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
            unsigned pos = f == TextPatternRangeEndpoint_Start ? o->first : o->last;
            if (e == TextPatternRangeEndpoint_Start) {
                first = pos;
                last = std::max(last, first);
            } else {
                last = pos;
                first = std::min(first, last);
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Select() override {
        return call([&](const auto &) {
            return status_hr(
                owner->ctx->ui->semantics().perform(owner->ref, Verb::select, Selection{first, last}).status);
        });
    }
    HRESULT STDMETHODCALLTYPE AddToSelection() override {
        return call([&](const auto &n) {
            auto s = n.text_selection.value_or(Selection{});
            if (s.anchor != s.caret &&
                (std::min(s.anchor, s.caret) != first || std::max(s.anchor, s.caret) != last))
                return static_cast<HRESULT>(UIA_E_INVALIDOPERATION);
            return status_hr(
                owner->ctx->ui->semantics().perform(owner->ref, Verb::select, Selection{first, last}).status);
        });
    }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
        return call([](const auto &) { return static_cast<HRESULT>(UIA_E_INVALIDOPERATION); });
    }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override {
        return call([](const auto &) { return static_cast<HRESULT>(UIA_E_NOTSUPPORTED); });
    }
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return call([&](const auto &) {
            *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
            return *out ? S_OK : E_OUTOFMEMORY;
        });
    }
};
static HRESULT ranges(ITextRangeProvider *range, SAFEARRAY **out) {
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, range ? 1 : 0);
    if (!*out) {
        if (range)
            range->Release();
        return E_OUTOFMEMORY;
    }
    if (range) {
        LONG i = 0;
        auto h = SafeArrayPutElement(*out, &i, range);
        range->Release();
        return h;
    }
    return S_OK;
}
HRESULT Provider::get_DocumentRange(ITextRangeProvider **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    AddRef();
    auto h = ctx->call([&] {
        auto hr = check();
        if (FAILED(hr))
            return hr;
        auto n = ctx->ui->semantics().get(ref);
        *out = new TextRange(this, 0, static_cast<unsigned>(n->value.size()), n->value);
        return S_OK;
    });
    Release();
    return h;
}
HRESULT Provider::GetSelection(SAFEARRAY **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    AddRef();
    auto h = ctx->call([&] {
        auto hr = check();
        if (FAILED(hr))
            return hr;
        auto n = ctx->ui->semantics().get(ref);
        auto s = n->text_selection.value_or(Selection{});
        return ranges(new TextRange(this, std::min(s.anchor, s.caret), std::max(s.anchor, s.caret), n->value),
                      out);
    });
    Release();
    return h;
}
HRESULT Provider::GetVisibleRanges(SAFEARRAY **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    AddRef();
    auto h = ctx->call([&] {
        auto hr = check();
        if (FAILED(hr))
            return hr;
        auto n = ctx->ui->semantics().get(ref);
        const auto& frame = ctx->frame();
        auto p = find_placement(frame, ref.id);
        if (!p || n->hidden) return ranges(nullptr, out);
        auto editor = p->field.editor; editor.x += p->bounds.x; editor.y += p->bounds.y;
        auto clip = intersect(editor, p->clip);
        if (clip.w <= 0 || clip.h <= 0) return ranges(nullptr, out);
        auto first = placement_text_offset(frame, ref.id, clip.x);
        auto last = placement_text_offset(frame, ref.id, clip.x + clip.w);
        auto session = access(*ctx->ui).edit(ref.id);
        auto committed = [&](unsigned pos) {
            if (session && session->composing && pos > session->begin()) {
                auto end = session->begin() + static_cast<unsigned>(session->preedit.size());
                pos = pos < end ? session->begin() : pos - static_cast<unsigned>(session->preedit.size()) + session->end() - session->begin();
            }
            return std::min(pos, static_cast<unsigned>(n->value.size()));
        };
        first = committed(first); last = committed(last);
        return ranges(new TextRange(this, std::min(first,last), std::max(first,last), n->value), out);
    });
    Release();
    return h;
}
HRESULT Provider::RangeFromChild(IRawElementProviderSimple *, ITextRangeProvider **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    AddRef(); auto hr = ctx->call([this] { return check(); }); Release();
    return FAILED(hr) ? hr : E_INVALIDARG;
}
HRESULT Provider::RangeFromPoint(UiaPoint point, ITextRangeProvider **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    AddRef();
    auto h = ctx->call([&] {
        auto hr = check();
        if (FAILED(hr))
            return hr;
        auto n = ctx->ui->semantics().get(ref);
        POINT p{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
        ScreenToClient(ctx->hwnd, &p);
        unsigned offset = placement_text_offset(ctx->frame(), ref.id, p.x);
        auto session = access(*ctx->ui).edit(ref.id);
        if (session && session->composing && offset > session->begin()) {
            auto end = session->begin() + static_cast<unsigned>(session->preedit.size());
            offset = offset < end ? session->begin()
                                  : offset - static_cast<unsigned>(session->preedit.size()) +
                                        (session->end() - session->begin());
        }
        offset = std::min(offset, static_cast<unsigned>(n->value.size()));
        *out = new TextRange(this, offset, offset, n->value);
        return S_OK;
    });
    Release();
    return h;
}
} // namespace
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
        if (event.kind == SemanticEventKind::property_changed && event.property == "hidden" && event.hidden) {
            auto n = ctx->ui->semantics().get(event.ref); if (!n || (n->role != Role::dialog && n->role != Role::tooltip)) return;
            bool visible = !*event.hidden;
            EVENTID id = n->role == Role::dialog ? (visible ? UIA_Window_WindowOpenedEventId : UIA_Window_WindowClosedEventId)
                : (visible ? UIA_ToolTipOpenedEventId : UIA_ToolTipClosedEventId);
            auto p = ctx->provider(event.ref); p->AddRef();
            ctx->overlay_events->post([p, id] { UiaRaiseAutomationEvent(p, id); p->Release(); });
        }
        if (event.kind == SemanticEventKind::layout_changed) {
            auto p = ctx->provider({}); p->AddRef();
            ctx->text_events->post([p] {
                UiaRaiseAutomationEvent(p, UIA_LayoutInvalidatedEventId); p->Release(); });
        }
        if (event.kind == SemanticEventKind::text_selection_changed) {
            auto p = ctx->provider(event.ref); p->AddRef();
            ctx->text_events->post([p] {
                UiaRaiseAutomationEvent(p, UIA_Text_TextSelectionChangedEventId); p->Release(); });
        }
        if (event.kind == SemanticEventKind::text_changed) {
            auto p = ctx->provider(event.ref); p->AddRef();
            ctx->text_events->post([p] {
                UiaRaiseAutomationEvent(p, UIA_Text_TextChangedEventId); p->Release(); });
        }
        if (event.kind == SemanticEventKind::focus_changed) {
            auto p = ctx->provider(event.ref); p->AddRef();
            ctx->focus_events->post([p] {
                UiaRaiseAutomationEvent(p, UIA_AutomationFocusChangedEventId);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::structure_changed) {
            auto p = ctx->provider(event.ref); p->AddRef();
            ctx->structure_events->post([p] {
                UiaRaiseStructureChangedEvent(p, StructureChangeType_ChildrenInvalidated, nullptr, 0);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "enabled") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n) return;
            auto p = ctx->provider(event.ref); p->AddRef(); bool enabled = n->enabled;
            ctx->events->post([p, enabled] {
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                before.vt = after.vt = VT_BOOL; before.boolVal = enabled ? VARIANT_FALSE : VARIANT_TRUE; after.boolVal = enabled ? VARIANT_TRUE : VARIANT_FALSE;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_IsEnabledPropertyId, before, after);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "value") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n || n->role != Role::text) return;
            auto p = ctx->provider(event.ref); p->AddRef(); auto value = n->value;
            ctx->events->post([p, value = std::move(value)] {
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                after.vt = VT_BSTR; after.bstrVal = bstr(value);
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_ValueValuePropertyId, before, after); VariantClear(&after);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "name") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n) return;
            auto p = ctx->provider(event.ref); p->AddRef(); auto name = n->name;
            ctx->events->post([p, name = std::move(name)] {
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                after.vt = VT_BSTR; after.bstrVal = bstr(name);
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_NamePropertyId, before, after); VariantClear(&after);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "numeric_value") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n || !n->numeric_value) return;
            auto p = ctx->provider(event.ref); p->AddRef(); double value = *n->numeric_value;
            ctx->events->post([p, value] {
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                after.vt = VT_R8; after.dblVal = value;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_RangeValueValuePropertyId, before, after);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "expanded") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n) return;
            auto p = ctx->provider(event.ref); p->AddRef(); bool expanded = n->expanded;
            ctx->events->post([p, expanded] {
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                before.vt = after.vt = VT_I4;
                before.lVal = expanded ? ExpandCollapseState_Collapsed : ExpandCollapseState_Expanded;
                after.lVal = expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_ExpandCollapseExpandCollapseStatePropertyId, before, after);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "selected") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n) return;
            auto p = ctx->provider(event.ref); p->AddRef(); bool selected = n->selected;
            ctx->events->post([p, selected] {
                VARIANT before, after; VariantInit(&before); VariantInit(&after);
                before.vt = after.vt = VT_BOOL; before.boolVal = selected ? VARIANT_FALSE : VARIANT_TRUE; after.boolVal = selected ? VARIANT_TRUE : VARIANT_FALSE;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_SelectionItemIsSelectedPropertyId, before, after);
                if (selected) UiaRaiseAutomationEvent(p, UIA_SelectionItem_ElementSelectedEventId);
                p->Release(); });
        }
        if (event.kind == SemanticEventKind::property_changed && event.property == "checked") {
            auto n = ctx->ui->semantics().get(event.ref); if (!n || !n->checked) return;
            auto p = ctx->provider(event.ref); p->AddRef(); bool checked = *n->checked;
            ctx->events->post([p, checked] {
                VARIANT old_value, new_value; VariantInit(&old_value); VariantInit(&new_value);
                old_value.vt = new_value.vt = VT_I4;
                new_value.lVal = checked ? ToggleState_On : ToggleState_Off;
                old_value.lVal = checked ? ToggleState_Off : ToggleState_On;
                UiaRaiseAutomationPropertyChangedEvent(p, UIA_ToggleToggleStatePropertyId, old_value, new_value);
                p->Release(); });
        }
    });
}
UiaBridge::UiaBridge(Ui& ui, HWND hwnd, std::function<const Frame&()> frame) : impl_(std::make_shared<Impl>(ui,hwnd,std::move(frame))) {
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
