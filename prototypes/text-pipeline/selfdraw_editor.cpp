#define LCARS_TEXT_HELPERS_ONLY
#include "text_probe.cpp"
#include "editing_model.hpp"
#include <imm.h>
#include <windowsx.h>
#include <objbase.h>
#include <oleauto.h>
#include <unknwn.h>
#include <UIAutomation.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <wrl/client.h>

// Prototype: one custom HWND, no native Edit child. Single-line TextRange uses
// the same cached layout; this is not a complete Unicode editor or TSF store.
static constexpr UINT set_value_message=WM_APP+20;
static constexpr UINT select_range_message=WM_APP+23,scroll_range_message=WM_APP+24;
struct RangeSelection { unsigned begin,end; };
struct StateTrace { unsigned long long sequence; DWORD thread; UINT message; unsigned long long detail; bool composing; unsigned anchor,caret; size_t length,preedit_length; const char* stage; };
static std::mutex trace_mutex;
static std::deque<StateTrace> state_trace;
static unsigned long long trace_sequence=0;
static bool save_success_trace=false;
static bool inject_test_composition=false,inject_test_selection=false;
static void trace_state(const char* stage,UINT message,unsigned long long detail,const editing::Model& model){
    if(!save_success_trace)return;
    std::lock_guard lock(trace_mutex);
    if(state_trace.size()==2048)state_trace.pop_front();
    state_trace.push_back({++trace_sequence,GetCurrentThreadId(),message,detail,model.composing,model.state.anchor,model.state.caret,model.state.text.size(),model.preedit.size(),stage});
}
static void trace_selection_snapshot(unsigned a,unsigned z,size_t length,bool composing){
    if(!save_success_trace)return;
    std::lock_guard lock(trace_mutex);
    if(state_trace.size()==2048)state_trace.pop_front();
    state_trace.push_back({++trace_sequence,GetCurrentThreadId(),0xffff,0,composing,a,z,length,0,"uia-get-selection"});
}
static void save_state_trace(){
    std::lock_guard lock(trace_mutex);std::ofstream out("state-trace.tsv",std::ios::binary);
    out<<"seq\tthread\tmessage\tdetail\tcomposing\tanchor\tcaret\tlength\tpreedit_length\tstage\n";
    for(const auto& e:state_trace)out<<e.sequence<<'\t'<<e.thread<<'\t'<<e.message<<'\t'<<e.detail<<'\t'<<e.composing<<'\t'<<e.anchor<<'\t'<<e.caret<<'\t'<<e.length<<'\t'<<e.preedit_length<<'\t'<<e.stage<<'\n';
}
static std::u16string u16(const std::wstring& s){return {s.begin(),s.end()};}
static std::wstring wide(const std::u16string& s){return {s.begin(),s.end()};}
struct Bridge {
    std::mutex mutex;
    HWND window{};
    bool alive=true,focused=false,composing=false,disconnecting=false;
    std::wstring value;
    std::wstring visible;
    std::map<unsigned,double> positions;
    unsigned selection_begin=0,selection_end=0;
    std::uint64_t revision=0;
    RECT bounds{},window_bounds{};
};
class ValueProvider final:public IRawElementProviderSimple,public IValueProvider,public ITextProvider {
    std::atomic<ULONG> references{1};
public:
    std::shared_ptr<Bridge> bridge;
    explicit ValueProvider(std::shared_ptr<Bridge> b):bridge(std::move(b)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(id==__uuidof(IValueProvider))*out=static_cast<IValueProvider*>(this);
        else if(id==__uuidof(ITextProvider))*out=static_cast<ITextProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override{if(!out)return E_POINTER;*out=ProviderOptions_ServerSideProvider;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,IUnknown** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
        if(pattern==UIA_ValuePatternId)*out=static_cast<IValueProvider*>(this);
        if(pattern==UIA_TextPatternId)*out=static_cast<ITextProvider*>(this);
        if(*out)AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,VARIANT* out) override {
        if(!out)return E_POINTER;VariantInit(out);
        std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
        auto str=[&](const wchar_t* text){out->vt=VT_BSTR;out->bstrVal=SysAllocString(text);return out->bstrVal?S_OK:E_OUTOFMEMORY;};
        auto flag=[&](bool value){out->vt=VT_BOOL;out->boolVal=value?VARIANT_TRUE:VARIANT_FALSE;};
        if(property==UIA_NamePropertyId)return str(L"LcarsUI 自繪文字輸入框");
        if(property==UIA_AutomationIdPropertyId)return str(L"lcars.prototype.text");
        if(property==UIA_FrameworkIdPropertyId)return str(L"LcarsUI Prototype");
        if(property==UIA_ProviderDescriptionPropertyId)return str(L"LcarsUI prototype: Value and single-line TextRange");
        if(property==UIA_ControlTypePropertyId){out->vt=VT_I4;out->lVal=UIA_EditControlTypeId;}
        else if(property==UIA_IsKeyboardFocusablePropertyId||property==UIA_IsEnabledPropertyId||property==UIA_IsControlElementPropertyId||property==UIA_IsContentElementPropertyId)flag(true);
        else if(property==UIA_HasKeyboardFocusPropertyId)flag(bridge->focused);
        else if(property==UIA_IsTextPatternAvailablePropertyId)flag(true);
        else if(property==UIA_IsPasswordPropertyId)flag(false);
        // HWND host provider supplies BoundingRectangle. Text geometry needs TextRange.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if(!out)return E_POINTER;HWND window;
        // Identity must remain queryable while UiaDisconnectProvider resolves it.
        // Operational Value/Text calls still reject bridge->alive == false.
        {std::lock_guard lock(bridge->mutex);if(!bridge->alive&&!bridge->disconnecting){*out=nullptr;return UIA_E_ELEMENTNOTAVAILABLE;}window=bridge->window;}
        if(!IsWindow(window)){*out=nullptr;return UIA_E_ELEMENTNOTAVAILABLE;}
        return UiaHostProviderFromHwnd(window,out);
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        if(!value)return E_INVALIDARG;HWND window;
        {std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;window=bridge->window;}
        // Synchronous marshal to owning UI thread; never touch FT/GL on a COM thread.
        return static_cast<HRESULT>(SendMessageW(window,set_value_message,0,reinterpret_cast<LPARAM>(value)));
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(bridge->mutex);
        if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
        *out=SysAllocStringLen(bridge->value.data(),static_cast<UINT>(bridge->value.size()));return *out?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override{if(!out)return E_POINTER;*out=FALSE;std::lock_guard lock(bridge->mutex);return bridge->alive?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*,ITextRangeProvider** out) override{if(!out)return E_POINTER;*out=nullptr;return E_INVALIDARG;}
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point,ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* out) override{if(!out)return E_POINTER;std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;*out=SupportedTextSelection_Single;return S_OK;}
};
#include "text_range_provider.hpp"
struct EditorHost {
    Context& context;
    TextEngine& engine;
    editing::Model model;
    std::shared_ptr<Bridge> bridge=std::make_shared<Bridge>();
    ValueProvider* provider{};
    bool retired=false;
    HRESULT disconnect_result=E_UNEXPECTED;
    bool dirty=true;
    int size_dip=24;
    float horizontal_scroll=0;
    // Limited WM_CHAR surrogate assembler: a high surrogate waits for the next low one. An unmatched low is
    // ignored, a new high replaces the buffered one, and accepted editing-context changes (UIA SetValue/Select,
    // editing/navigation keys, mouse down, focus loss, composition start, fixture ops) discard it.
    wchar_t pending_high=0;
    unsigned composition_messages=0,composition_results=0;
    bool candidate_positioned=false;
    POINT candidate{};
    explicit EditorHost(Context& c,TextEngine& e):context(c),engine(e){
        bridge->window=c.window;provider=new ValueProvider(bridge);
        model.state.text=u"自繪文字 ABC";model.select(0,(unsigned)model.state.text.size());
    }
    void retire(){
        if(retired)return;retired=true;
        {std::lock_guard lock(bridge->mutex);bridge->alive=false;bridge->disconnecting=true;}
        disconnect_result=UiaDisconnectProvider(provider);
        {std::lock_guard lock(bridge->mutex);bridge->disconnecting=false;}
    }
    ~EditorHost(){retire();provider->Release();}
    int pixels() const{return MulDiv(size_dip,static_cast<int>(GetDpiForWindow(context.window)),96);}
    Layout layout(){return engine.shape(wide(model.visible()),pixels());}
    float x_at(const Layout& l,unsigned offset){auto it=l.carets.upper_bound(offset);return it==l.carets.begin()?0:std::prev(it)->second;}
    void sync(bool keep_caret_visible=true) {
        auto l=layout();RECT client{};GetClientRect(context.window,&client);
        float x=x_at(l,model.visible_caret());
        const float span=std::max(40L,client.right-48);
        if(keep_caret_visible){
            if(x-horizontal_scroll>span)horizontal_scroll=x-span;
            if(x<horizontal_scroll)horizontal_scroll=x;
        }
        horizontal_scroll=std::max(0.f,horizontal_scroll);
        candidate={24+static_cast<LONG>(std::round(x-horizontal_scroll)),40+pixels()+10};
        SetCaretPos(candidate.x,40);
        HIMC imc=ImmGetContext(context.window);candidate_positioned=false;
        if(imc){
            CANDIDATEFORM cf{};cf.dwIndex=0;cf.dwStyle=CFS_EXCLUDE;cf.ptCurrentPos=candidate;
            cf.rcArea={candidate.x,40,candidate.x+2,candidate.y};
            COMPOSITIONFORM composition{};composition.dwStyle=CFS_POINT;composition.ptCurrentPos={candidate.x,40};
            const bool a=ImmSetCandidateWindow(imc,&cf)!=FALSE;
            const bool b=ImmSetCompositionWindow(imc,&composition)!=FALSE;
            CANDIDATEFORM actual{};
            candidate_positioned=a&&b&&ImmGetCandidateWindow(imc,0,&actual)&&actual.ptCurrentPos.x==candidate.x&&actual.ptCurrentPos.y==candidate.y;
            ImmReleaseContext(context.window,imc);
        }
        POINT origin{24,40};ClientToScreen(context.window,&origin);
        POINT client_bottom{0,client.bottom};ClientToScreen(context.window,&client_bottom);
        // The line box is clipped by the client area, as in draw(); empty when the client ends above the text.
        const LONG line_bottom=std::max(origin.y,std::min<LONG>(origin.y+pixels()+10,client_bottom.y));
        std::wstring old,current=wide(model.state.text),presented=wide(model.visible());
        bool text_changed=false,selection_changed=false;
        {
            std::lock_guard lock(bridge->mutex);old=bridge->value;bridge->value=current;bridge->composing=model.composing;
            bridge->focused=GetFocus()==context.window;
            bridge->bounds={origin.x,origin.y,origin.x+std::max(0L,client.right-48),line_bottom};
            unsigned selected_begin=model.begin(),selected_end=model.composing?model.begin()+(unsigned)model.preedit.size():model.end();
            text_changed=bridge->visible!=presented;
            selection_changed=bridge->selection_begin!=selected_begin||bridge->selection_end!=selected_end;
            if(text_changed)++bridge->revision;
            bridge->visible=presented;bridge->selection_begin=selected_begin;bridge->selection_end=selected_end;
            bridge->positions.clear();for(auto [offset,pos]:l.carets)bridge->positions[offset]=origin.x+pos-horizontal_scroll;
            GetWindowRect(context.window,&bridge->window_bounds);
        }
        if(text_changed)UiaRaiseAutomationEvent(provider,UIA_Text_TextChangedEventId);
        if(selection_changed)UiaRaiseAutomationEvent(provider,UIA_Text_TextSelectionChangedEventId);
        if(old!=current){
            VARIANT before{},after{};before.vt=after.vt=VT_BSTR;before.bstrVal=SysAllocString(old.c_str());after.bstrVal=SysAllocString(current.c_str());
            UiaRaiseAutomationPropertyChangedEvent(provider,UIA_ValueValuePropertyId,before,after);
            VariantClear(&before);VariantClear(&after);
        }
        std::wostringstream title;title<<L"LcarsUI 自繪編輯原型 | F2 字型 / F3,F4 字級 | "<<size_dip<<L" DIP | composing="<<model.composing<<L" | IMM messages="<<composition_messages;
        SetWindowTextW(context.window,title.str().c_str());dirty=true;
    }
    bool dragging=false;
    void end_drag(){dragging=false;if(GetCapture()==context.window)ReleaseCapture();}
    // Signed client x, clamped to the text viewport, then the shared layout hit test.
    unsigned mouse_offset(LPARAM l){
        RECT client{};GetClientRect(context.window,&client);
        const int x=std::clamp(GET_X_LPARAM(l),24,std::max(24,static_cast<int>(client.right)-24));
        return engine.hit(layout(),x-24.f+horizontal_scroll);
    }
    void move(int direction,bool extend){
        auto l=layout();auto pos=model.state.caret;
        if(direction<0){auto it=l.carets.lower_bound(pos);if(it!=l.carets.begin())pos=std::prev(it)->first;}
        else {auto it=l.carets.upper_bound(pos);if(it!=l.carets.end())pos=it->first;}
        model.select(extend?model.state.anchor:pos,pos);
    }
    void erase(bool back){
        if(model.begin()==model.end()){auto anchor=model.state.caret;move(back?-1:1,true);model.state.anchor=anchor;}
        if(model.begin()!=model.end())model.replace(u"");
    }
    void draw(Scene& scene,bool present=true){
        RECT r{};GetClientRect(context.window,&r);if(r.right<=0||r.bottom<=0)return;
        auto l=layout();for(auto& g:l.glyphs)g.x-=horizontal_scroll;for(auto& [offset,x]:l.carets)x-=horizontal_scroll;
        unsigned begin=model.begin(),end=model.composing?model.begin()+(unsigned)model.preedit.size():model.end();
        auto image=paint(engine,l,r.right,r.bottom,begin,end,pixels(),model.visible_caret());auto& gl=context.gl;
        for(int y=0;y<r.bottom;++y)for(int x=0;x<r.right;++x)if(x<24||x>=r.right-24){
            size_t p=(static_cast<size_t>(y)*r.right+x)*4;image[p]=18;image[p+1]=25;image[p+2]=38;
        }
        gl.Viewport(0,0,r.right,r.bottom);gl.Disable(GL_SCISSOR_TEST);gl.Disable(GL_BLEND);
        gl.BindTexture(GL_TEXTURE_2D,scene.texture);gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,r.right,r.bottom,0,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        gl.UseProgram(scene.program);gl.BindVertexArray(scene.vao);gl.DrawArrays(GL_TRIANGLES,0,3);
        require(gl.GetError()==GL_NO_ERROR,"Selfdraw GL error");if(present)SwapBuffers(context.dc);dirty=false;
    }
};
static EditorHost* host=nullptr;
static bool allow_test_fixture=false;
// Noninteractive cross-process test window only: accept tiny sizes so narrow viewports are testable.
static bool test_accept_any_size=false;
static constexpr UINT test_render_sample_message=WM_APP+27;
static unsigned test_poschanging_messages=0;
// Diagnostics for the in-process UIA roundtrip (independent ElementFromHandle E_FAIL).
static std::atomic<unsigned> getobject_messages{0};static std::atomic<LONG> last_getobject_id{0};static std::atomic<LRESULT> last_getobject_result{0};
static bool test_close_requested=false;
struct HostBinding {
    EditorHost& editor;
    bool attached=true;
    explicit HostBinding(EditorHost& e):editor(e){host=&e;}
    void detach(){
        if(!attached)return;attached=false;
        // Runs before EditorHost destruction, including exception unwinding.
        // Context teardown may synchronously send focus/IME/window messages.
        host=nullptr;
        SetWindowLongPtrW(editor.context.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(wnd_proc));
    }
    ~HostBinding(){detach();}
};
static bool copy_selection(HWND window,const editing::Model& model){
    if(model.begin()==model.end()||!OpenClipboard(window))return false;
    auto text=wide(model.state.text.substr(model.begin(),model.end()-model.begin()));
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
    if(!memory){CloseClipboard();return false;}
    auto data=GlobalLock(memory);if(!data){GlobalFree(memory);CloseClipboard();return false;}
    memcpy(data,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
    const bool ok=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
    if(!ok)GlobalFree(memory);CloseClipboard();return ok;
}
static std::u16string composition_string(HIMC imc,DWORD kind){
    LONG bytes=ImmGetCompositionStringW(imc,kind,nullptr,0);if(bytes<=0)return {};
    std::u16string text(static_cast<size_t>(bytes)/2,u'\0');
    if(ImmGetCompositionStringW(imc,kind,text.data(),bytes)<0)return {};return text;
}
static LRESULT CALLBACK edit_proc(HWND h,UINT message,WPARAM w,LPARAM l){
    if(!host)return wnd_proc(h,message,w,l);
    auto& e=*host;
    bool relevant=message==select_range_message||message==set_value_message||message==WM_APP+21||message==WM_CHAR||message==WM_KEYDOWN||message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_CAPTURECHANGED||message==WM_CANCELMODE||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_IME_STARTCOMPOSITION||message==WM_IME_COMPOSITION||message==WM_IME_ENDCOMPOSITION;
    struct ExitTrace{bool enabled;UINT message;const editing::Model& model;~ExitTrace(){if(enabled)trace_state("exit",message,0,model);}} exit_trace{relevant,message,e.model};
    if(relevant)trace_state("enter",message,message==WM_CHAR?0:static_cast<unsigned long long>(w),e.model);
    switch(message){
    case WM_APP+26:if(!allow_test_fixture)return 0;test_close_requested=true;return 1;
    case WM_WINDOWPOSCHANGING:
        // DefWindowProc clamps to the minimum tracking size here (a WM_GETMINMAXINFO override alone
        // was observed not to be consulted). Only the noninteractive test window skips that default.
        if(!test_accept_any_size)break;
        ++test_poschanging_messages;return 0;
    case test_render_sample_message:{
        // Same GL_BACK witness as the final check: render without presenting, read one client pixel.
        if(!allow_test_fixture)return 0;
        RECT r{};GetClientRect(h,&r);const int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l);
        if(x<0||y<0||x>=r.right||y>=r.bottom)return 0;
        Scene witness(e.context.gl);witness.init();e.draw(witness,false);e.context.gl.Finish();
        reinterpret_cast<PFNGLREADBUFFERPROC>(symbol("glReadBuffer"))(GL_BACK);
        std::array<unsigned char,4> pixel{};e.context.gl.ReadPixels(x,r.bottom-1-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        e.dirty=true;return 0x1000000|(pixel[0]<<16)|(pixel[1]<<8)|pixel[2];
    }
    case select_range_message:{
        if(e.model.composing)return UIA_E_INVALIDOPERATION;
        auto range=reinterpret_cast<const RangeSelection*>(l);if(!range)return E_INVALIDARG;
        e.pending_high=0;e.model.select(range->begin,range->end);
        if(allow_test_fixture&&inject_test_selection){e.model.select(range->end,range->end);trace_state("injected-caret-move",select_range_message,0,e.model);}
        e.sync();return S_OK;
    }
    case scroll_range_message:{
        if(e.model.composing)return UIA_E_INVALIDOPERATION;
        auto layout=e.layout();RECT rect{};GetClientRect(h,&rect);
        e.horizontal_scroll=std::max(0.f,e.x_at(layout,static_cast<unsigned>(l))-(w?0.f:std::max(40L,rect.right-48)));
        e.sync(false);return S_OK;
    }
    case WM_APP+21:
        if(!allow_test_fixture)return 0;
        // Test-only composition fixture; uses the same model calls as the IMM handlers, no OS IME.
        // 0 cancel/end, 1 start+fixed preedit, 2 start empty, 3 update preedit table[l], 4 commit preedit, 5 undo, 6 redo.
        switch(w){
        case 0:e.model.cancel();break;
        case 1:e.model.start();e.model.update(u"測試",2);break;
        case 2:e.model.start();break;
        case 3:{static const std::u16string table[]={u"ㄘ",u"測",u"測試"};if(l<0||l>2)return 0;e.model.update(table[l],(unsigned)table[l].size());break;}
        case 4:if(!e.model.composing)return 0;e.model.commit(e.model.preedit);break;
        case 5:if(!e.model.undo())return 0;break;
        case 6:if(!e.model.redo())return 0;break;
        default:return 0;
        }
        e.pending_high=0;  // same rule as the IME/undo paths below
        e.sync();return 1;
    case WM_PAINT:ValidateRect(h,nullptr);e.dirty=true;return 0;
    case WM_GETOBJECT:{
        ++getobject_messages;last_getobject_id=static_cast<LONG>(l);
        if(static_cast<LONG>(l)==UiaRootObjectId){
            LRESULT r=e.retired?0:UiaReturnRawElementProvider(h,w,l,e.provider);last_getobject_result=r;
            if(allow_test_fixture)std::cout<<"WM_GETOBJECT root wparam=0x"<<std::hex<<static_cast<unsigned long long>(w)<<" result=0x"<<static_cast<unsigned long long>(r)<<" lasterror="<<std::dec<<GetLastError()<<" retired="<<e.retired<<'\n';
            return r;
        }
        if(allow_test_fixture)std::cout<<"WM_GETOBJECT id="<<static_cast<LONG>(l)<<" (default handling)\n";
        break;
    }
    case set_value_message:
        if(e.model.composing)return UIA_E_INVALIDOPERATION;
        if(std::wstring(reinterpret_cast<const wchar_t*>(l)).find_first_of(L"\r\n\t")!=std::wstring::npos)return E_INVALIDARG;
        e.pending_high=0;e.model.set(u16(reinterpret_cast<const wchar_t*>(l)));e.sync();return S_OK;
    case WM_SETFOCUS:CreateCaret(h,nullptr,2,e.pixels()+10);e.sync();UiaRaiseAutomationEvent(e.provider,UIA_AutomationFocusChangedEventId);return 0;
    case WM_KILLFOCUS:{e.end_drag();e.pending_high=0;auto imc=ImmGetContext(h);if(imc){ImmNotifyIME(imc,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(h,imc);}e.model.cancel();DestroyCaret();e.sync();return 0;}
    case WM_SIZE:case WM_MOVE:e.sync();break;
    case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);e.sync();return 0;}
    case WM_IME_STARTCOMPOSITION:e.end_drag();e.pending_high=0;e.model.start();e.sync();return 0;
    case WM_IME_SETCONTEXT:return DefWindowProcW(h,message,w,l&~ISC_SHOWUICOMPOSITIONWINDOW);
    case WM_IME_REQUEST:
        if(w==IMR_QUERYCHARPOSITION&&l){
            auto info=reinterpret_cast<IMECHARPOSITION*>(l);if(info->dwSize<sizeof(IMECHARPOSITION))return 0;
            auto layout=e.layout();auto offset=e.model.composing?e.model.begin()+std::min(info->dwCharPos,(DWORD)e.model.preedit.size()):e.model.state.caret;
            info->pt={24+(LONG)std::round(e.x_at(layout,offset)-e.horizontal_scroll),40};ClientToScreen(h,&info->pt);
            info->cLineHeight=e.pixels()+10;GetClientRect(h,&info->rcDocument);
            POINT first{info->rcDocument.left,info->rcDocument.top},last{info->rcDocument.right,info->rcDocument.bottom};ClientToScreen(h,&first);ClientToScreen(h,&last);
            info->rcDocument={first.x,first.y,last.x,last.y};return 1;
        }
        break;
    case WM_IME_COMPOSITION:{
        ++e.composition_messages;auto imc=ImmGetContext(h);if(!imc)return 0;
        if(l&GCS_RESULTSTR){e.model.commit(composition_string(imc,GCS_RESULTSTR));++e.composition_results;}
        if(l&GCS_COMPSTR){auto text=composition_string(imc,GCS_COMPSTR);LONG caret=ImmGetCompositionStringW(imc,GCS_CURSORPOS,nullptr,0);e.model.update(std::move(text),static_cast<unsigned>(std::max(0L,caret)));}
        ImmReleaseContext(h,imc);e.sync();return 0;
    }
    case WM_IME_ENDCOMPOSITION:e.model.cancel();e.sync();return 0;
    case WM_IME_CHAR:return 0; // Result is already consumed through GCS_RESULTSTR.
    case WM_LBUTTONDOWN:{
        if(e.model.composing)return 0;if(!test_accept_any_size)SetFocus(h);
        auto offset=e.mouse_offset(l);e.pending_high=0;
        e.model.select((w&MK_SHIFT)?e.model.state.anchor:offset,offset);
        // The noninteractive test window must not capture the user's real mouse: after a background
        // SetCapture the system delivered a real button-up move that ended the synthetic drag.
        e.dragging=true;if(!test_accept_any_size)SetCapture(h);e.sync();return 0;
    }
    case WM_MOUSEMOVE:{
        if(!e.dragging)break;
        // Composition or a released button (e.g. lost up message) ends the drag instead of extending.
        if(e.model.composing||!(w&MK_LBUTTON)){e.end_drag();return 0;}
        auto offset=e.mouse_offset(l);
        if(offset!=e.model.state.caret){e.model.select(e.model.state.anchor,offset);e.sync();}
        return 0;
    }
    case WM_LBUTTONUP:{
        if(!e.dragging)break;
        if(!e.model.composing){auto offset=e.mouse_offset(l);if(offset!=e.model.state.caret){e.model.select(e.model.state.anchor,offset);e.sync();}}
        e.end_drag();return 0;
    }
    case WM_CAPTURECHANGED:if(reinterpret_cast<HWND>(l)!=h)e.dragging=false;break;
    case WM_CANCELMODE:e.end_drag();break;
    case WM_KEYDOWN:{
        if(e.model.composing){
            if(w==VK_ESCAPE){auto imc=ImmGetContext(h);if(imc){ImmNotifyIME(imc,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(h,imc);}e.model.cancel();e.sync();return 0;}
            break;
        }
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(ctrl&&w=='A')e.model.select(0,(unsigned)e.model.state.text.size());
        else if(ctrl&&w=='Z')e.model.undo();else if(ctrl&&w=='Y')e.model.redo();
        else if(ctrl&&(w=='C'||w=='X')){if(copy_selection(h,e.model)&&w=='X')e.model.replace(u"");}
        else if(ctrl&&w=='V'){
            if(OpenClipboard(h)){
                auto data=GetClipboardData(CF_UNICODETEXT);
                auto ptr=data?static_cast<const wchar_t*>(GlobalLock(data)):nullptr;
                if(ptr){std::wstring text(ptr);if(text.find_first_of(L"\r\n\t")==std::wstring::npos)e.model.replace(u16(text));GlobalUnlock(data);}
                CloseClipboard();
            }
        }
        else if(w==VK_LEFT||w==VK_RIGHT)e.move(w==VK_LEFT?-1:1,shift);
        else if(w==VK_HOME)e.model.select(shift?e.model.state.anchor:0,0);
        else if(w==VK_END){auto end=(unsigned)e.model.state.text.size();e.model.select(shift?e.model.state.anchor:end,end);}
        else if(w==VK_BACK)e.erase(true);else if(w==VK_DELETE)e.erase(false);
        else if(w==VK_F2)e.engine.reload(!e.engine.ming);
        else if(w==VK_F3)e.size_dip=std::max(12,e.size_dip-2);
        else if(w==VK_F4)e.size_dip=std::min(48,e.size_dip+2);
        else break;
        // Editing/navigation keys change the insertion context; font keys are geometry only.
        if(w!=VK_F2&&w!=VK_F3&&w!=VK_F4)e.pending_high=0;
        e.sync();return 0;
    }
    case WM_CHAR:{
        if(e.model.composing||w<32||w==127)return 0;
        wchar_t ch=static_cast<wchar_t>(w);
        if(ch>=0xd800&&ch<=0xdbff){e.pending_high=ch;return 0;}
        std::u16string text;
        if(ch>=0xdc00&&ch<=0xdfff){if(!e.pending_high)return 0;text.push_back(e.pending_high);}
        e.pending_high=0;text.push_back(ch);e.model.replace(std::move(text));e.sync();return 0;
    }
    }
    return wnd_proc(h,message,w,l);
}
static void check_edit(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';require(ok,name);}
// Direct provider call; the external UIA client path returns S_OK for these values (layer not instrumented).
static void direct_endpoint_validation(EditorHost& e){
    ITextRangeProvider *range{},*other{};
    check_edit(SUCCEEDED(e.provider->get_DocumentRange(&range))&&SUCCEEDED(range->Clone(&other)),"direct provider document range");
    const auto bad=static_cast<TextPatternRangeEndpoint>(7);int order=9,moved=9;BOOL equal=FALSE;
    HRESULT left=range->CompareEndpoints(bad,other,TextPatternRangeEndpoint_Start,&order);
    HRESULT right=range->CompareEndpoints(TextPatternRangeEndpoint_Start,other,bad,&order);
    check_edit(left==E_INVALIDARG&&right==E_INVALIDARG&&order==0,"direct provider CompareEndpoints rejects endpoint 7");
    HRESULT move=range->MoveEndpointByUnit(bad,TextUnit_Character,-1,&moved);
    check_edit(move==E_INVALIDARG&&moved==0,"direct provider MoveEndpointByUnit rejects endpoint 7");
    int start=9,end=9;
    bool unchanged=SUCCEEDED(range->Compare(other,&equal))&&equal
        &&SUCCEEDED(range->CompareEndpoints(TextPatternRangeEndpoint_Start,other,TextPatternRangeEndpoint_Start,&start))&&start==0
        &&SUCCEEDED(range->CompareEndpoints(TextPatternRangeEndpoint_End,other,TextPatternRangeEndpoint_End,&end))&&end==0;
    check_edit(unchanged,"rejected endpoint arguments leave direct provider range unchanged");
    other->Release();range->Release();
}
// Shared layout for supplementary-plane text: clusters/carets must start on scalar boundaries.
// Missing glyphs (id 0, drawn as the red .notdef box) are reported, not treated as data corruption.
static void supplementary_layout_observation(EditorHost& e){
    const std::wstring text=L"繁體A\xd840\xdc00中\xd83d\xde00" L"B";
    auto layout=e.engine.shape(text,e.pixels());bool aligned=true;
    for(const auto& g:layout.glyphs){
        std::cout<<"unicode glyph cluster="<<g.cluster<<" font="<<g.font<<" id="<<g.id<<" advance="<<g.advance<<'\n';
        if(g.cluster>=text.size()||(text[g.cluster]>=0xdc00&&text[g.cluster]<=0xdfff))aligned=false;
    }
    for(auto [offset,x]:layout.carets)if(offset<text.size()&&text[offset]>=0xdc00&&text[offset]<=0xdfff)aligned=false;
    std::cout<<"unicode layout missing_glyphs="<<layout.missing<<" fallback_glyphs="<<layout.fallback<<'\n';
    check_edit(aligned,"layout clusters and caret stops never point inside a surrogate pair");
}
static void model_scenarios(){
    editing::Model m;m.state.text=u"ABC";m.select(1,2);
    m.start();m.update(u"ㄓ",1);
    check_edit(m.state.text==u"ABC"&&m.visible()==u"AㄓC","preedit changes presentation, not committed text");
    m.update(u"中文",1);check_edit(m.visible_caret()==2,"composition cursor maps into presentation");
    m.cancel();check_edit(m.state.text==u"ABC"&&m.state.anchor==1&&m.state.caret==2,"cancel restores pre-composition selection");
    m.start();m.update(u"ㄓ",1);m.update(u"中文",2);m.commit(u"中文");
    check_edit(m.state.text==u"A中文C"&&m.state.caret==3&&!m.composing,"commit replaces selection once");
    check_edit(m.undo()&&m.state.text==u"ABC","entire composition commits as one undo transaction");
    check_edit(m.redo()&&m.state.text==u"A中文C","redo restores committed composition");
    m.start();m.update(u"暫",1);m.undo();check_edit(!m.composing&&m.state.text==u"A中文C","undo during composition cancels preedit");
}
static void uia_roundtrip(EditorHost& e){
    using Microsoft::WRL::ComPtr;
    std::atomic<bool> done=false;
    std::atomic<HRESULT> result=E_FAIL;
    // Diagnostics only: name and duration of each completed phase; the pass/fail rule is unchanged.
    std::atomic<const char*> phase="CoInitializeEx";
    auto started=std::chrono::steady_clock::now();
    auto mark=[&](const char* next,HRESULT hr){
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::cout<<"UIA phase="<<phase.load()<<" hr=0x"<<std::hex<<static_cast<unsigned long>(hr)<<std::dec<<" t="<<ms<<"ms\n";
        if(SUCCEEDED(hr))phase=next;
    };
    std::thread client([&]{
        HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        HRESULT hr=init;mark("CoCreateInstance(CUIAutomation)",hr);
        if(SUCCEEDED(init)){
            {
                ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationElement> element;
                ComPtr<IUnknown> unknown;ComPtr<IUIAutomationValuePattern> pattern;
                hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
                mark("ElementFromHandle",hr);
                if(SUCCEEDED(hr)){
                    const unsigned before=getobject_messages;
                    hr=automation->ElementFromHandle(e.context.window,&element);
                    std::cout<<"UIA ElementFromHandle window="<<e.context.window<<" IsWindow="<<IsWindow(e.context.window)<<" hung="<<IsHungAppWindow(e.context.window)
                        <<" getobject_during="<<(getobject_messages-before)<<" last_id="<<last_getobject_id<<" last_result=0x"<<std::hex<<static_cast<unsigned long long>(last_getobject_result)<<std::dec<<'\n';
                    mark("role/value/bounds",hr);
                }
                if(SUCCEEDED(hr)){
                    CONTROLTYPEID type{};hr=element->get_CurrentControlType(&type);
                    std::cout<<"UIA role="<<type<<" expected="<<UIA_EditControlTypeId<<'\n';
                    if(SUCCEEDED(hr)&&type!=UIA_EditControlTypeId)hr=E_FAIL;
                }
                if(SUCCEEDED(hr))hr=element->GetCurrentPattern(UIA_ValuePatternId,&unknown);
                if(SUCCEEDED(hr))hr=unknown.As(&pattern);
                if(SUCCEEDED(hr)){BSTR value=SysAllocString(L"UIA 設定 ABC");hr=value?pattern->SetValue(value):E_OUTOFMEMORY;SysFreeString(value);}
                if(SUCCEEDED(hr)){
                    BSTR value{};hr=pattern->get_CurrentValue(&value);
                    std::cout<<"UIA value length="<<(value?SysStringLen(value):0)<<'\n';
                    if(SUCCEEDED(hr)&&std::wstring(value?value:L"")!=L"UIA 設定 ABC")hr=E_FAIL;
                    SysFreeString(value);
                }
                if(SUCCEEDED(hr)){
                    RECT r{};hr=element->get_CurrentBoundingRectangle(&r);
                    RECT expected{};{std::lock_guard lock(e.bridge->mutex);expected=e.bridge->window_bounds;}
                    std::cout<<"UIA bounds="<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<" expected="<<expected.left<<','<<expected.top<<','<<expected.right<<','<<expected.bottom<<'\n';
                    if(SUCCEEDED(hr)&&!EqualRect(&r,&expected))hr=E_FAIL;
                }
                if(SUCCEEDED(hr)){
                    VARIANT available{};hr=element->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId,&available);
                    if(SUCCEEDED(hr)&&(available.vt!=VT_BOOL||available.boolVal!=VARIANT_TRUE))hr=E_FAIL;
                    VariantClear(&available);
                }
                mark("done",hr);
            }
            CoUninitialize();
        }
        result=hr;done=true;
    });
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
    while(!done){
        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(std::chrono::steady_clock::now()>deadline){std::cerr<<"FAIL UIA client timeout\n";ExitProcess(3);}
        Sleep(1);    }
    client.join();
    std::cout<<"UIA client HRESULT=0x"<<std::hex<<static_cast<unsigned long>(result.load())<<std::dec<<'\n';
    check_edit(SUCCEEDED(result.load()),"real UIA client on MTA thread reads/writes Value, role and HWND bounds (not TextRange)");
    check_edit(e.model.state.text==u"UIA 設定 ABC","UIA mutation reaches common editing model");
}
static void cross_process_roundtrip(EditorHost& e,bool concurrent=false,HostBinding* closing=nullptr){
    const bool require_inactive=(GetWindowLongPtrW(e.context.window,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0;
    wchar_t filename[32768]{};DWORD length=GetModuleFileNameW(nullptr,filename,32768);
    require(length>0&&length<32768,"Executable path unavailable");
    std::wstring executable(filename,length);executable.resize(executable.find_last_of(L"\\/")+1);executable+=L"uia_external_client.exe";
    std::wstring command=L"\""+executable+L"\" "+std::to_wstring(reinterpret_cast<uintptr_t>(e.context.window))+L" "+std::to_wstring(GetCurrentProcessId());
    if(concurrent)command+=L" --concurrent";
    if(closing)command+=L" --lifecycle";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot launch independent UIA test client");
    CloseHandle(process.hThread);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
    bool timeout=false,release_ok=true,activated=false;
    while(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        if(e.context.window&&GetForegroundWindow()==e.context.window)activated=true;
        if(closing&&test_close_requested&&e.context.window){
            closing->detach();e.retire();
            BSTR retired_value{};HRESULT raw_value=e.provider->get_Value(&retired_value);SysFreeString(retired_value);
            std::cout<<"disconnect_hresult=0x"<<std::hex<<(unsigned long)e.disconnect_result<<" raw_retired_value=0x"<<(unsigned long)raw_value<<std::dec<<'\n';
            release_ok=raw_value==UIA_E_ELEMENTNOTAVAILABLE&&e.provider->SetValue(L"must not write after retirement")==UIA_E_ELEMENTNOTAVAILABLE;
            DestroyCaret();release_ok=e.context.release()&&release_ok;
        }
        if(std::chrono::steady_clock::now()>deadline){timeout=true;TerminateProcess(process.hProcess,3);WaitForSingleObject(process.hProcess,2000);break;}
        Sleep(1);
    }
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
    std::cout<<"test_size_override poschanging_messages="<<test_poschanging_messages<<'\n';
    std::ifstream transcript("uia-cross-process.log");std::cout<<transcript.rdbuf();
    require(!timeout,"Cross-process UIA client timeout");
    check_edit(code==0,"independent process UIA tests");
    if(require_inactive)check_edit(!activated,"automated test did not become foreground during UIA requests");
    if(closing){check_edit(release_ok&&e.retired&&SUCCEEDED(e.disconnect_result)&&!e.context.window,"provider disconnected and HWND destroyed before client releases interfaces");return;}
    check_edit(e.model.state.text==u"跨程序 UIA 456"&&!e.model.composing,"cross-process writes and fixture cancellation reach model");
    check_edit(e.model.begin()==0&&e.model.end()==1,"external TextRange selection reaches renderer editing model");
}
int main(int argc,char** argv){
    try{
        std::cout<<std::unitbuf;
        bool interactive=false,concurrent=false,lifecycle=false;
        for(int i=1;i<argc;++i){
            const std::string arg=argv[i];
            if(arg=="--interactive")interactive=true;if(arg=="--cross-process")allow_test_fixture=true;if(arg=="--trace-state")save_success_trace=true;
            if(arg=="--inject-test-composition")inject_test_composition=true;if(arg=="--inject-test-selection")inject_test_selection=true;
            if(arg=="--concurrent")concurrent=true;if(arg=="--lifecycle")lifecycle=true;
        }
        require((!inject_test_composition&&!inject_test_selection)||allow_test_fixture,"Fault injection requires cross-process test mode");
        require(!lifecycle||(!interactive&&allow_test_fixture),"Lifecycle test requires noninteractive cross-process mode");
        test_accept_any_size=allow_test_fixture&&!interactive;
        model_scenarios();
        require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization failed");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=wnd_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LcarsBuildProbe";wc.hCursor=LoadCursorW(nullptr,IDC_IBEAM);RegisterClassW(&wc);
        TextEngine text;Context context;context.open(interactive);
        check_edit(interactive||GetForegroundWindow()!=context.window,"automated window does not take foreground focus");
        {
            EditorHost e(context,text);HostBinding binding(e);
            SetWindowLongPtrW(context.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(edit_proc));
            if(interactive)SetFocus(context.window);CreateCaret(context.window,nullptr,2,e.pixels()+10);e.sync();
            check_edit(GetWindow(context.window,GW_CHILD)==nullptr,"custom HWND has no native Edit child");
            SendMessageW(context.window,WM_CHAR,L'A',0);
            check_edit(e.model.state.text==u"A","WM_CHAR edits selfdraw model");
            SendMessageW(context.window,WM_CHAR,0xd840,0);SendMessageW(context.window,WM_CHAR,0xdc00,0);
            check_edit(e.model.state.text==u"A\U00020000","surrogate input committed together");
            SendMessageW(context.window,WM_KEYDOWN,VK_BACK,0);
            check_edit(e.model.state.text==u"A","backspace uses layout boundary instead of splitting surrogate");
            e.model.set(u"繁體中文 ABC");e.model.select(2,2);e.sync();
            const auto first_candidate=e.candidate;
            e.size_dip=32;e.sync();check_edit(e.candidate.x>first_candidate.x,"font size updates shared caret/candidate geometry");
            POINT screen=e.candidate;ClientToScreen(context.window,&screen);
            std::cout<<"IMM candidate placement roundtrip="<<e.candidate_positioned<<" client="<<e.candidate.x<<','<<e.candidate.y<<" screen="<<screen.x<<','<<screen.y<<'\n';
            check_edit(e.candidate_positioned,"IMM candidate API accepts and returns shared geometry (not visual IME proof)");
            e.model.start();e.model.update(u"測",1);e.sync();
            IMECHARPOSITION position{};position.dwSize=sizeof(position);position.dwCharPos=1;
            check_edit(SendMessageW(context.window,WM_IME_REQUEST,IMR_QUERYCHARPOSITION,reinterpret_cast<LPARAM>(&position))==1,"IME character-position request answered from text layout");
            POINT expected{e.candidate.x,40};ClientToScreen(context.window,&expected);
            check_edit(position.pt.x==expected.x&&position.pt.y==expected.y,"IME character screen coordinates match caret layout");
            check_edit(e.provider->SetValue(L"must not overwrite preedit")==UIA_E_INVALIDOPERATION,"UIA write rejected during composition");
            SendMessageW(context.window,WM_KEYDOWN,VK_ESCAPE,0);
            check_edit(!e.model.composing&&!quitting,"Escape cancels composition without closing editor");
            uia_roundtrip(e);
            direct_endpoint_validation(e);
            supplementary_layout_observation(e);
            if(allow_test_fixture){
                if(inject_test_composition){e.model.start();e.model.update(u"測",1);trace_state("injected-composition",WM_IME_STARTCOMPOSITION,0,e.model);e.sync();}
                cross_process_roundtrip(e,concurrent);
                Scene witness(context.gl);witness.init();e.draw(witness,false);context.gl.Finish();
                double left{},top{},width{},height{};std::ifstream bounds("uia-text-rectangle.tsv");bounds>>left>>top>>width>>height;
                require(bool(bounds)&&width>4&&height>4,"UIA witness rectangle unavailable");
                POINT sample{(LONG)std::round(left+2),(LONG)std::round(top+height-3)};ScreenToClient(context.window,&sample);
                RECT client{};GetClientRect(context.window,&client);
                auto read_buffer=reinterpret_cast<PFNGLREADBUFFERPROC>(symbol("glReadBuffer"));read_buffer(GL_BACK);
                std::array<unsigned char,4> pixel{};
                context.gl.ReadPixels(sample.x,client.bottom-1-sample.y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
                std::cout<<"UIA rectangle render sample="<<int(pixel[0])<<','<<int(pixel[1])<<','<<int(pixel[2])<<'\n';
                check_edit(pixel[0]==42&&pixel[1]==78&&pixel[2]==123,"external TextRange rectangle lands inside rendered selection highlight");
                capture(context.gl,client.right,client.bottom,"textrange-selection.bmp");SwapBuffers(context.dc);
            }
            e.model.set(u"自繪輸入：繁體中文 ABC");e.model.select((unsigned)e.model.state.text.size(),(unsigned)e.model.state.text.size());e.size_dip=24;e.sync();
            {
            Scene scene(context.gl);scene.init();e.draw(scene,false);
            context.gl.Finish();
            auto read_buffer=reinterpret_cast<PFNGLREADBUFFERPROC>(symbol("glReadBuffer"));
            read_buffer(GL_BACK);RECT r{};GetClientRect(context.window,&r);capture(context.gl,r.right,r.bottom,"selfdraw-editor.bmp");SwapBuffers(context.dc);
            while(interactive&&!quitting){
                MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                if(e.dirty)e.draw(scene);Sleep(10);
            }
            }
            if(lifecycle)cross_process_roundtrip(e,false,&binding);
            std::cout<<"Observed real WM_IME_COMPOSITION messages="<<e.composition_messages<<" result messages="<<e.composition_results<<'\n';
            host=nullptr;DestroyCaret();
        }
        check_edit(host==nullptr&&(!context.window||GetWindowLongPtrW(context.window,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(wnd_proc)),"window callbacks detached before model teardown");
        require(context.release(),"WGL cleanup failed");CoUninitialize();
        std::cout<<"PASS selfdraw editing/IMM geometry/UIA Value and limited TextRange; real IME and Narrator remain unverified\n";
        if(save_success_trace)save_state_trace();return 0;
    }catch(const std::exception& error){save_state_trace();std::cerr<<"cleanup_host_detached="<<(host==nullptr)<<"\nFAIL "<<error.what()<<'\n';return 1;}
}
