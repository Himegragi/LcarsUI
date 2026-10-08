#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
static std::ofstream log_file;
static void check(bool ok,const char* name){log_file<<(ok?"PASS ":"FAIL ")<<name<<std::endl;if(!ok)throw std::runtime_error(name);}
static void success(HRESULT hr,const char* name){if(FAILED(hr))log_file<<"HRESULT=0x"<<std::hex<<static_cast<unsigned long>(hr)<<std::dec<<std::endl;check(SUCCEEDED(hr),name);}
class Changes final:public IUIAutomationPropertyChangedEventHandler {
    std::atomic<ULONG> references{1};
public:
    std::atomic<unsigned> count{0};
    std::atomic<bool> expected_value{false};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown)&&id!=__uuidof(IUIAutomationPropertyChangedEventHandler))return E_NOINTERFACE;
        *out=static_cast<IUIAutomationPropertyChangedEventHandler*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*,PROPERTYID id,VARIANT value) override{
        if(id==UIA_ValueValuePropertyId){++count;if(value.vt==VT_BSTR&&value.bstrVal&&std::wstring(value.bstrVal)==L"跨程序 UIA 456")expected_value=true;}
        return S_OK;
    }
};
static std::wstring current_value(IUIAutomationValuePattern* pattern){
    BSTR value{};success(pattern->get_CurrentValue(&value),"read current Value");
    std::wstring result(value?value:L"");SysFreeString(value);return result;
}
int main(int argc,char** argv){
    log_file.open("uia-cross-process.log",std::ios::binary|std::ios::trunc);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    int result=0;HWND window{};
    {
        ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationElement> element;
        ComPtr<IUnknown> raw;ComPtr<IUIAutomationValuePattern> pattern;ComPtr<Changes> changes;
        changes.Attach(new Changes);
        try{
            check(argc==3,"host HWND and process identity supplied");
            window=reinterpret_cast<HWND>(static_cast<uintptr_t>(std::stoull(argv[1])));
            DWORD expected_pid=std::stoul(argv[2]),owner{};GetWindowThreadProcessId(window,&owner);
            check(owner==expected_pid&&owner!=GetCurrentProcessId(),"provider is in a separate known process");
            log_file<<"client_pid="<<GetCurrentProcessId()<<" provider_pid="<<owner<<std::endl;
            success(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)),"create UIA client");
            success(automation->ElementFromHandle(window,&element),"resolve external HWND provider");
            BSTR id{};success(element->get_CurrentAutomationId(&id),"read AutomationId");
            bool identity=id&&std::wstring(id)==L"lcars.prototype.text";SysFreeString(id);check(identity,"target is the intended prototype field");
            CONTROLTYPEID type{};success(element->get_CurrentControlType(&type),"read control role");check(type==UIA_EditControlTypeId,"custom provider exposes Edit role");
            int pid{};success(element->get_CurrentProcessId(&pid),"read UIA process id");check(pid==static_cast<int>(owner),"UIA process identity matches Win32");
            success(element->GetCurrentPattern(UIA_ValuePatternId,&raw),"get external Value pattern");
            success(raw.As(&pattern),"query Value pattern interface");
            PROPERTYID property=UIA_ValueValuePropertyId;
            success(automation->AddPropertyChangedEventHandlerNativeArray(element.Get(),TreeScope_Element,nullptr,changes.Get(),&property,1),"subscribe to Value change events");
            success(pattern->SetValue(L"跨程序 UIA 456"),"write Value across process boundary");
            check(current_value(pattern.Get())==L"跨程序 UIA 456","cross-process value round trip");
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!changes->expected_value&&std::chrono::steady_clock::now()<deadline)Sleep(10);
            check(changes->expected_value,"receive external Value property change event with expected payload");
            log_file<<"value_events="<<changes->count.load()<<std::endl;

            HRESULT invalid=pattern->SetValue(L"line one\nline two");
            log_file<<"multiline_hresult=0x"<<std::hex<<static_cast<unsigned long>(invalid)<<std::dec<<std::endl;
            check(invalid==E_INVALIDARG,"single-line provider rejects multiline mutation");
            check(current_value(pattern.Get())==L"跨程序 UIA 456","rejected mutation preserves value");

            // Test-only fixture hook. This is not an actual IME keystroke test.
            check(SendMessageW(window,WM_APP+21,1,0)==1,"start simulated composition fixture");
            HRESULT composing=pattern->SetValue(L"must not replace composition");
            log_file<<"composition_hresult=0x"<<std::hex<<static_cast<unsigned long>(composing)<<std::dec<<std::endl;
            check(composing==UIA_E_INVALIDOPERATION,"external write rejected during composition");
            check(current_value(pattern.Get())==L"跨程序 UIA 456","composition protection preserves committed text");
            SendMessageW(window,WM_APP+21,0,0);

            check(SetWindowPos(window,nullptr,0,0,760,510,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize dedicated test host");
            RECT expected{},actual{};GetWindowRect(window,&expected);success(element->get_CurrentBoundingRectangle(&actual),"query external HWND bounds after resize");
            check(EqualRect(&expected,&actual)!=FALSE,"bounds match physical HWND coordinates");
            VARIANT available{};success(element->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId,&available),"query TextPattern capability");
            bool absent=available.vt==VT_BOOL&&available.boolVal==VARIANT_FALSE;VariantClear(&available);
            check(absent,"unsupported TextPattern is not advertised");
            success(automation->RemovePropertyChangedEventHandler(element.Get(),changes.Get()),"unsubscribe event handler");
            log_file<<"PASS cross-process UIA contract batch; no Narrator or TextRange claim"<<std::endl;
        }catch(const std::exception& e){log_file<<"FAIL "<<e.what()<<std::endl;result=1;}
        if(window)SendMessageW(window,WM_APP+21,0,0);
        if(automation)automation->RemoveAllEventHandlers();
    }
    CoUninitialize();return result;
}
