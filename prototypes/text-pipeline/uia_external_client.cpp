#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sstream>
#include <iomanip>
#include <vector>

using Microsoft::WRL::ComPtr;
static std::ofstream log_file;
static void check(bool ok,const char* name){log_file<<(ok?"PASS ":"FAIL ")<<name<<std::endl;if(!ok)throw std::runtime_error(name);}
static void success(HRESULT hr,const char* name){if(FAILED(hr))log_file<<"HRESULT=0x"<<std::hex<<static_cast<unsigned long>(hr)<<std::dec<<std::endl;check(SUCCEEDED(hr),name);}
class Changes final:public IUIAutomationPropertyChangedEventHandler,public IUIAutomationEventHandler {
    std::atomic<ULONG> references{1};
public:
    std::atomic<unsigned> count{0};
    std::atomic<bool> expected_value{false};
    std::atomic<unsigned> text_events{0},selection_events{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IUIAutomationPropertyChangedEventHandler))*out=static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if(id==__uuidof(IUIAutomationEventHandler))*out=static_cast<IUIAutomationEventHandler*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*,PROPERTYID id,VARIANT value) override{
        if(id==UIA_ValueValuePropertyId){++count;if(value.vt==VT_BSTR&&value.bstrVal&&std::wstring(value.bstrVal)==L"跨程序 UIA 456")expected_value=true;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*,EVENTID id)override{
        if(id==UIA_Text_TextChangedEventId)++text_events;
        if(id==UIA_Text_TextSelectionChangedEventId)++selection_events;
        return S_OK;
    }
};
static std::wstring current_value(IUIAutomationValuePattern* pattern){
    BSTR value{};success(pattern->get_CurrentValue(&value),"read current Value");
    std::wstring result(value?value:L"");SysFreeString(value);return result;
}
static HRESULT write_value(IUIAutomationValuePattern* pattern,const std::wstring& text){
    BSTR value=SysAllocStringLen(text.data(),static_cast<UINT>(text.size()));if(!value)return E_OUTOFMEMORY;
    HRESULT result=pattern->SetValue(value);SysFreeString(value);return result;
}
static std::wstring range_text(IUIAutomationTextRange* range){
    BSTR value{};success(range->GetText(-1,&value),"read text range");std::wstring result(value?value:L"");SysFreeString(value);return result;
}
static std::wstring version_text(int version){std::wostringstream s;s<<L"並行 "<<std::setw(4)<<std::setfill(L'0')<<version;return s.str();}
static bool valid_version(const std::wstring& text){
    if(text.size()!=7||text.substr(0,3)!=L"並行 ")return false;
    for(size_t i=3;i<7;++i)if(text[i]<L'0'||text[i]>L'9')return false;
    return std::stoi(text.substr(3))<=40;
}
static void concurrent_reads(HWND window,IUIAutomationValuePattern* writer){
    success(write_value(writer,version_text(0)),"initialize concurrent-read fixture");
    std::atomic<int> ready=0,active=0,successful=0,stale=0,invalid=0;
    std::atomic<bool> start=false;
    auto reader=[&]{
        HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(init)){++invalid;++ready;return;}
        {
            ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationElement> element;ComPtr<IUnknown> raw;
            ComPtr<IUIAutomationTextPattern> text;
            HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
            if(SUCCEEDED(hr))hr=automation->ElementFromHandle(window,&element);
            if(SUCCEEDED(hr))hr=element->GetCurrentPattern(UIA_TextPatternId,&raw);
            if(SUCCEEDED(hr))hr=raw.As(&text);
            ++ready;while(!start)Sleep(1);
            if(FAILED(hr))++invalid;
            else{
                ++active;
                for(int i=0;i<100;++i){
                    ComPtr<IUIAutomationTextRange> range;hr=text->get_DocumentRange(&range);
                    if(FAILED(hr)){++invalid;continue;}
                    Sleep(1);BSTR value{};hr=range->GetText(-1,&value);
                    if(hr==UIA_E_ELEMENTNOTAVAILABLE)++stale;
                    else if(FAILED(hr))++invalid;
                    else if(!valid_version(std::wstring(value?value:L"")))++invalid;
                    else ++successful;
                    SysFreeString(value);
                }
                --active;
            }
        }
        CoUninitialize();
    };
    std::thread a(reader),b(reader);
    while(ready<2)Sleep(1);start=true;
    HRESULT written=S_OK;int overlap=0;
    for(int i=1;i<=40;++i){if(active>0)++overlap;written=write_value(writer,version_text(i));if(FAILED(written))break;Sleep(1);}
    a.join();b.join();success(written,"complete 40 concurrent Value writes");
    log_file<<"concurrent_success="<<successful<<" stale="<<stale<<" invalid="<<invalid<<" overlapped_writes="<<overlap<<std::endl;
    check(successful>0&&invalid==0&&successful+stale==200&&overlap>0,"concurrent reads return whole valid snapshots or explicit stale-range errors");
    success(write_value(writer,L"跨程序 UIA 456"),"restore fixture after concurrent reads");
}
static void retired_provider(HWND window,IUIAutomation* automation,IUIAutomationElement* element,IUIAutomationValuePattern* value){
    ComPtr<IUnknown> raw;ComPtr<IUIAutomationTextPattern> text;ComPtr<IUIAutomationTextRange> held;
    success(element->GetCurrentPattern(UIA_TextPatternId,&raw),"retain Text pattern before close");success(raw.As(&text),"query retained Text pattern");
    success(text->get_DocumentRange(&held),"retain range before close");
    check(SendMessageW(window,WM_APP+26,0,0)==1,"request dedicated test control destruction");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(IsWindow(window)&&std::chrono::steady_clock::now()<deadline)Sleep(10);
    check(!IsWindow(window),"native control actually destroyed while client holds interfaces");
    BSTR result{};HRESULT v=value->get_CurrentValue(&result);bool empty_value=!result||SysStringLen(result)==0;log_file<<"retired_value_length="<<(result?SysStringLen(result):0)<<std::endl;SysFreeString(result);
    result=nullptr;HRESULT r=held->GetText(-1,&result);SysFreeString(result);
    ComPtr<IUIAutomationTextRange> fresh;HRESULT d=text->get_DocumentRange(&fresh);
    HRESULT write=write_value(value,L"must not write after close");
    log_file<<"retired_write=0x"<<std::hex<<(unsigned long)write<<std::dec<<std::endl;
    VARIANT raw_property{};HRESULT raw_result=element->GetCurrentPropertyValueEx(UIA_ValueValuePropertyId,TRUE,&raw_property);
    BOOL unsupported=FALSE;if(SUCCEEDED(raw_result))automation->CheckNotSupported(raw_property,&unsupported);
    log_file<<"retired_raw_property=0x"<<std::hex<<(unsigned long)raw_result<<std::dec<<" variant="<<raw_property.vt<<" unsupported="<<unsupported<<std::endl;
    VariantClear(&raw_property);
    log_file<<"retired_value=0x"<<std::hex<<(unsigned long)v<<" retired_range=0x"<<(unsigned long)r<<" retired_document=0x"<<(unsigned long)d<<std::dec<<std::endl;
    check(raw_result==UIA_E_ELEMENTNOTAVAILABLE,"raw property without defaults is unavailable after destruction");
    check(v==UIA_E_ELEMENTNOTAVAILABLE&&empty_value,"retained Value getter reports element unavailable, not a successful empty default");
    check(write==UIA_E_ELEMENTNOTAVAILABLE,"retained Value pattern cannot write after destruction");
    check(r==UIA_E_ELEMENTNOTAVAILABLE&&d==UIA_E_ELEMENTNOTAVAILABLE,"retained TextRange and Text pattern reject access after destruction");
}
static std::array<double,4> rectangle(IUIAutomationTextRange* range){
    SAFEARRAY* array{};success(range->GetBoundingRectangles(&array),"get text bounding rectangles");
    LONG lower{},upper{};SafeArrayGetLBound(array,1,&lower);SafeArrayGetUBound(array,1,&upper);
    check(upper-lower+1==4,"single-line range has one rectangle");std::array<double,4> result{};
    for(LONG i=0;i<4;++i){LONG at=lower+i;SafeArrayGetElement(array,&at,&result[(size_t)i]);}SafeArrayDestroy(array);return result;
}
static void text_ranges(IUIAutomationElement* element,IUIAutomationValuePattern* value,HWND window,Changes* changes){
    ComPtr<IUnknown> raw;ComPtr<IUIAutomationTextPattern> pattern;
    success(element->GetCurrentPattern(UIA_TextPatternId,&raw),"get Text pattern across process boundary");
    success(raw.As(&pattern),"query Text pattern interface");
    ComPtr<IUIAutomationTextRange> document,first,clone,point,found;
    success(pattern->get_DocumentRange(&document),"get document range");
    check(range_text(document.Get())==current_value(value),"document text agrees with Value");
    success(document->Clone(&first),"clone document range");
    success(first->MoveEndpointByRange(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start),"collapse range at document start");
    int moved{};success(first->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&moved),"extend range by one character unit");
    check(moved==1&&range_text(first.Get())==L"跨","first character range is correct");
    auto rect=rectangle(first.Get());RECT whole{};GetWindowRect(window,&whole);
    check(rect[2]>0&&rect[3]>0&&rect[3]<whole.bottom-whole.top,"text bounds are smaller than HWND bounds");
    POINT p{(LONG)std::round(rect[0]+1),(LONG)std::round(rect[1]+rect[3]/2)};
    success(pattern->RangeFromPoint(p,&point),"hit test physical screen point");
    int comparison{};success(point->CompareEndpoints(TextPatternRangeEndpoint_Start,document.Get(),TextPatternRangeEndpoint_Start,&comparison),"compare hit-test text offset");
    check(comparison==0,"left edge resolves to first caret position");
    const auto before_selection=changes->selection_events.load();
    success(first->Select(),"select text through external TextRange");
    ComPtr<IUIAutomationTextRangeArray> selection;
    success(pattern->GetSelection(&selection),"read selected text range");int count{};success(selection->get_Length(&count),"read selection count");check(count==1,"single selection returned");
    ComPtr<IUIAutomationTextRange> selected;success(selection->GetElement(0,&selected),"get selection range");
    auto selected_text=range_text(selected.Get());log_file<<"selected_length="<<selected_text.size()<<" first_code="<<(selected_text.empty()?0:(unsigned)selected_text[0])<<std::endl;
    check(selected_text==L"跨","UIA selection matches selected character");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(changes->selection_events==before_selection&&std::chrono::steady_clock::now()<deadline)Sleep(10);
    check(changes->selection_events>before_selection,"receive TextSelectionChanged event");
    success(first->Clone(&clone),"clone selected range");BOOL equal{};success(first->Compare(clone.Get(),&equal),"compare equal ranges");check(equal,"clone preserves endpoints");
    success(clone->Move(TextUnit_Character,1,&moved),"move range one character");check(moved==1&&range_text(clone.Get())==L"程","range movement selects next character");
    success(clone->ExpandToEnclosingUnit(TextUnit_Document),"expand to document");success(clone->Compare(document.Get(),&equal),"compare expanded document");check(equal,"document expansion is consistent");
    BSTR needle=SysAllocString(L"uia");HRESULT find=document->FindText(needle,FALSE,TRUE,&found);SysFreeString(needle);success(find,"case-insensitive range search");
    check(found&&range_text(found.Get())==L"UIA","range search returns requested text");
    SendMessageW(window,WM_KEYDOWN,VK_F4,0);auto larger=rectangle(first.Get());
    check(larger[2]>rect[2]&&larger[3]>rect[3],"existing range geometry updates after font size change");
    ComPtr<IUIAutomationTextRangeArray> visible;success(pattern->GetVisibleRanges(&visible),"get visible ranges");success(visible->get_Length(&count),"read visible range count");check(count==1,"single visible line returned");

    const auto before_text=changes->text_events.load();
    success(write_value(value,L"新文字"),"mutate text to invalidate previous ranges");
    BSTR stale{};HRESULT stale_result=first->GetText(-1,&stale);SysFreeString(stale);
    log_file<<"stale_range_hresult=0x"<<std::hex<<static_cast<unsigned long>(stale_result)<<std::dec<<std::endl;
    check(stale_result==UIA_E_ELEMENTNOTAVAILABLE,"stale revision-bound range is explicitly rejected");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(changes->text_events==before_text&&std::chrono::steady_clock::now()<deadline)Sleep(10);
    check(changes->text_events>before_text,"receive TextChanged event");
    document.Reset();success(pattern->get_DocumentRange(&document),"reacquire range after text mutation");check(range_text(document.Get())==L"新文字","new document range returns current text");
    success(write_value(value,L""),"set empty document");
    document.Reset();success(pattern->get_DocumentRange(&document),"get empty document range");check(range_text(document.Get()).empty(),"empty range returns empty text");
    SAFEARRAY* empty_rects{};success(document->GetBoundingRectangles(&empty_rects),"get degenerate range rectangles");
    LONG empty_low{},empty_high{};SafeArrayGetLBound(empty_rects,1,&empty_low);SafeArrayGetUBound(empty_rects,1,&empty_high);SafeArrayDestroy(empty_rects);
    check(empty_high<empty_low,"degenerate range has no bounding rectangles");
    std::wstring long_text=L"開頭";for(int i=0;i<40;++i)long_text+=L"繁體文字";
    success(write_value(value,long_text),"set long single-line document");
    document.Reset();first.Reset();success(pattern->get_DocumentRange(&document),"get long document");success(document->Clone(&first),"clone long document");
    success(first->MoveEndpointByRange(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start),"collapse at long document start");
    success(first->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&moved),"select first long-document character");
    success(first->ScrollIntoView(TRUE),"scroll text range into view without changing selection");
    auto scrolled=rectangle(first.Get());POINT client_origin{24,40};ClientToScreen(window,&client_origin);
    check(std::abs(scrolled[0]-client_origin.x)<1,"scrolled range aligns with rendered viewport origin");
    check(current_value(value)==long_text,"scroll operation preserves document contents");
}
static HRESULT logged(const char* name,HRESULT hr){log_file<<"edge "<<name<<"=0x"<<std::hex<<static_cast<unsigned long>(hr)<<std::dec<<std::endl;return hr;}
static VARIANT bool_variant(bool value){VARIANT v{};v.vt=VT_BOOL;v.boolVal=value?VARIANT_TRUE:VARIANT_FALSE;return v;}
static std::pair<unsigned,unsigned> selection_text_and_count(IUIAutomationTextPattern* pattern,std::wstring& text){
    ComPtr<IUIAutomationTextRangeArray> selection;success(pattern->GetSelection(&selection),"read selection for edge checks");
    int count{};success(selection->get_Length(&count),"read selection count for edge checks");text.clear();
    if(count>0){ComPtr<IUIAutomationTextRange> range;success(selection->GetElement(0,&range),"get edge selection range");text=range_text(range.Get());}
    return {static_cast<unsigned>(count),static_cast<unsigned>(text.size())};
}
// Negative and boundary contracts. Arguments are valid unless the check name says otherwise.
static void range_contract_edges(IUIAutomation* automation,IUIAutomationElement* element,IUIAutomationValuePattern* value){
    ComPtr<IUnknown> raw;ComPtr<IUIAutomationTextPattern> pattern;
    success(element->GetCurrentPattern(UIA_TextPatternId,&raw),"get Text pattern for edge checks");success(raw.As(&pattern),"query Text pattern for edge checks");
    success(write_value(value,L"跨程序 UIA 456"),"set edge-check fixture");
    ComPtr<IUIAutomationTextRange> document,live,found;success(pattern->get_DocumentRange(&document),"get edge document range");

    VARIANT attribute{};success(document->GetAttributeValue(UIA_IsReadOnlyAttributeId,&attribute),"read IsReadOnly attribute");
    check(attribute.vt==VT_BOOL&&attribute.boolVal==VARIANT_FALSE,"editable field reports IsReadOnly false");VariantClear(&attribute);
    success(document->GetAttributeValue(UIA_FontNameAttributeId,&attribute),"read unsupported FontName attribute");
    BOOL unsupported=FALSE;success(automation->CheckNotSupported(attribute,&unsupported),"classify unsupported attribute value");VariantClear(&attribute);
    check(unsupported!=FALSE,"unsupported attribute is the reserved NotSupported value");
    success(logged("find_readonly_false",document->FindAttribute(UIA_IsReadOnlyAttributeId,bool_variant(false),FALSE,&found)),"search matching IsReadOnly false");
    BOOL equal=FALSE;check(found!=nullptr,"matching attribute search returns a range");success(found->Compare(document.Get(),&equal),"compare attribute match");check(equal!=FALSE,"attribute match spans the uniform range");
    found.Reset();success(logged("find_readonly_true",document->FindAttribute(UIA_IsReadOnlyAttributeId,bool_variant(true),FALSE,&found)),"search non-matching IsReadOnly true");
    check(found==nullptr,"non-matching live attribute search is success with no range");
    VARIANT font{};font.vt=VT_BSTR;font.bstrVal=SysAllocString(L"Segoe UI");
    HRESULT find_unsupported=logged("find_unsupported_live",document->FindAttribute(UIA_FontNameAttributeId,font,FALSE,&found));VariantClear(&font);
    check(FAILED(find_unsupported)||found==nullptr,"unsupported attribute search never fabricates a match");found.Reset();

    // Degenerate range at both document edges.
    success(document->Clone(&live),"clone for degenerate checks");
    success(live->MoveEndpointByRange(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start),"collapse at start for edge checks");
    int moved=-7;success(live->Move(TextUnit_Character,0,&moved),"zero-count move");check(moved==0&&range_text(live.Get()).empty(),"zero-count move is a no-op");
    moved=-7;success(live->Move(TextUnit_Character,-1,&moved),"move collapsed range before start");check(moved==0,"cannot move before document start");
    moved=-7;success(live->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,TextUnit_Character,-3,&moved),"move start endpoint before document start");check(moved==0,"start endpoint clamps at document start");
    int order=9;success(live->CompareEndpoints(TextPatternRangeEndpoint_Start,document.Get(),TextPatternRangeEndpoint_Start,&order),"compare clamped start");check(order==0,"clamped degenerate range remains at document start");
    moved=-7;success(live->Move(TextUnit_Character,1,&moved),"move collapsed range forward");check(moved==1&&range_text(live.Get()).empty(),"collapsed range moves and stays degenerate");
    moved=-7;success(live->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,TextUnit_Character,1,&moved),"push start past end");
    success(live->CompareEndpoints(TextPatternRangeEndpoint_Start,live.Get(),TextPatternRangeEndpoint_End,&order),"compare pushed endpoints");check(moved==1&&order==0,"moving start past end drags end along");
    success(live->MoveEndpointByRange(TextPatternRangeEndpoint_Start,document.Get(),TextPatternRangeEndpoint_End),"collapse at document end");
    moved=-7;success(live->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,2,&moved),"move end endpoint past document end");check(moved==0,"end endpoint clamps at document end");
    moved=-7;success(live->Move(TextUnit_Character,1,&moved),"move collapsed range past end");check(moved==0&&range_text(live.Get()).empty(),"cannot move past document end");

    // Invalid arguments where the proxy forwards them; only failure and unchanged state are required.
    success(document->Clone(&live),"clone for invalid-argument checks");
    check(FAILED(logged("invalid_expand_unit",live->ExpandToEnclosingUnit(static_cast<TextUnit>(99)))),"invalid ExpandToEnclosingUnit unit is rejected");
    moved=-7;check(FAILED(logged("invalid_move_unit",live->Move(static_cast<TextUnit>(-5),1,&moved)))&&moved<=0,"invalid Move unit is rejected");
    // Observation only: the external client gets S_OK, End-like results. Provider validation is asserted directly in the host.
    order=9;logged("invalid_compare_endpoint_proxy",live->CompareEndpoints(static_cast<TextPatternRangeEndpoint>(7),document.Get(),TextPatternRangeEndpoint_Start,&order));
    log_file<<"edge invalid_compare_endpoint_order="<<order<<std::endl;
    success(live->Compare(document.Get(),&equal),"compare after rejected arguments");check(equal!=FALSE,"rejected arguments leave range unchanged");
    // Same observation for MoveEndpointByUnit; record the resulting endpoint.
    moved=-7;logged("invalid_endpoint_move_proxy",live->MoveEndpointByUnit(static_cast<TextPatternRangeEndpoint>(7),TextUnit_Character,-1,&moved));
    success(live->CompareEndpoints(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_End,&order),"locate end after proxy-normalized endpoint");
    log_file<<"edge invalid_endpoint_move_moved="<<moved<<" end_vs_document_end="<<order<<std::endl;

    // Single selection.
    ComPtr<IUIAutomationTextRange> one,two;success(document->Clone(&one),"clone first selection candidate");
    success(one->MoveEndpointByRange(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start),"collapse selection candidate");
    success(one->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&moved),"extend selection candidate");
    success(one->Clone(&two),"clone second selection candidate");success(two->Move(TextUnit_Character,1,&moved),"move second selection candidate");
    success(one->Select(),"select first candidate");
    success(logged("add_same_selection",one->AddToSelection()),"adding the existing selection is idempotent");
    HRESULT add_other=logged("add_second_selection",two->AddToSelection());
    check(add_other==UIA_E_INVALIDOPERATION,"single-selection provider rejects a second disjoint selection");
    HRESULT remove=logged("remove_live_selection",one->RemoveFromSelection());
    check(remove==UIA_E_INVALIDOPERATION,"single-selection provider does not fabricate RemoveFromSelection");
    std::wstring selected;auto [count,length]=selection_text_and_count(pattern.Get(),selected);
    check(count==1&&selected==L"跨","rejected selection operations keep the one existing selection");

    // Stale range: every operation with valid arguments reports element unavailable.
    ComPtr<IUIAutomationTextRange> stale,stale_other;success(document->Clone(&stale),"retain range to become stale");success(one->Clone(&stale_other),"retain second range to become stale");
    success(write_value(value,L"過期測試"),"mutate text for stale-range edge checks");
    ComPtr<IUIAutomationTextRange> fresh;success(pattern->get_DocumentRange(&fresh),"get fresh range after mutation");
    // Capture exact endpoints in the current revision; a collapsed caret has empty text, so text alone proves nothing.
    ComPtr<IUIAutomationTextRangeArray> before_array;success(pattern->GetSelection(&before_array),"capture selection before stale operations");
    int before_count{};success(before_array->get_Length(&before_count),"count selection before stale operations");
    check(before_count==1,"one selection before stale operations");
    ComPtr<IUIAutomationTextRange> before_range;success(before_array->GetElement(0,&before_range),"get selection before stale operations");
    int before_start{},before_end{};
    success(before_range->CompareEndpoints(TextPatternRangeEndpoint_Start,fresh.Get(),TextPatternRangeEndpoint_Start,&before_start),"locate selection start before stale operations");
    success(before_range->CompareEndpoints(TextPatternRangeEndpoint_End,fresh.Get(),TextPatternRangeEndpoint_End,&before_end),"locate selection end before stale operations");
    log_file<<"edge selection_before start_vs_doc_start="<<before_start<<" end_vs_doc_end="<<before_end<<std::endl;
    unsigned failures=0;
    auto expect_stale=[&](const char* name,HRESULT hr){logged(name,hr);if(hr!=UIA_E_ELEMENTNOTAVAILABLE){++failures;log_file<<"FAIL stale "<<name<<std::endl;}};
    ComPtr<IUIAutomationTextRange> out;expect_stale("stale_clone",stale->Clone(&out));out.Reset();
    expect_stale("stale_attribute_supported",stale->GetAttributeValue(UIA_IsReadOnlyAttributeId,&attribute));VariantClear(&attribute);
    expect_stale("stale_attribute_unsupported",stale->GetAttributeValue(UIA_FontNameAttributeId,&attribute));VariantClear(&attribute);
    expect_stale("stale_find_readonly_false",stale->FindAttribute(UIA_IsReadOnlyAttributeId,bool_variant(false),FALSE,&out));out.Reset();
    expect_stale("stale_find_readonly_true",stale->FindAttribute(UIA_IsReadOnlyAttributeId,bool_variant(true),FALSE,&out));out.Reset();
    BSTR needle=SysAllocString(L"UIA");expect_stale("stale_find_text",stale->FindText(needle,FALSE,FALSE,&out));SysFreeString(needle);out.Reset();
    expect_stale("stale_compare_self_fresh",stale->Compare(fresh.Get(),&equal));
    expect_stale("stale_compare_fresh_other",fresh->Compare(stale.Get(),&equal));
    expect_stale("stale_compare_endpoints",stale->CompareEndpoints(TextPatternRangeEndpoint_Start,fresh.Get(),TextPatternRangeEndpoint_Start,&order));
    expect_stale("stale_compare_endpoints_other",fresh->CompareEndpoints(TextPatternRangeEndpoint_Start,stale.Get(),TextPatternRangeEndpoint_End,&order));
    expect_stale("stale_move_endpoint_by_range",fresh->MoveEndpointByRange(TextPatternRangeEndpoint_End,stale.Get(),TextPatternRangeEndpoint_Start));
    expect_stale("stale_expand",stale->ExpandToEnclosingUnit(TextUnit_Character));
    expect_stale("stale_move",stale->Move(TextUnit_Character,1,&moved));
    expect_stale("stale_move_endpoint",stale->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,-1,&moved));
    SAFEARRAY* rects{};expect_stale("stale_rectangles",stale->GetBoundingRectangles(&rects));if(rects)SafeArrayDestroy(rects);
    ComPtr<IUIAutomationElement> enclosing;expect_stale("stale_enclosing",stale->GetEnclosingElement(&enclosing));
    ComPtr<IUIAutomationElementArray> children;expect_stale("stale_children",stale->GetChildren(&children));
    expect_stale("stale_select",stale_other->Select());
    expect_stale("stale_add_selection",stale_other->AddToSelection());
    expect_stale("stale_remove_selection",stale_other->RemoveFromSelection());
    expect_stale("stale_scroll",stale->ScrollIntoView(TRUE));
    check(current_value(value)==L"過期測試","stale operations do not change Value");
    // before_range stays valid only if no text mutation happened; its own success proves the revision is unchanged.
    ComPtr<IUIAutomationTextRangeArray> after_array;success(pattern->GetSelection(&after_array),"capture selection after stale operations");
    int after_count{};success(after_array->get_Length(&after_count),"count selection after stale operations");
    ComPtr<IUIAutomationTextRange> after_range;if(after_count>0)success(after_array->GetElement(0,&after_range),"get selection after stale operations");
    BOOL same=FALSE;int start_delta=9,end_delta=9;
    success(logged("selection_compare_after_stale",before_range->Compare(after_range.Get(),&same)),"compare selection across stale operations in same revision");
    success(before_range->CompareEndpoints(TextPatternRangeEndpoint_Start,after_range.Get(),TextPatternRangeEndpoint_Start,&start_delta),"compare selection start across stale operations");
    success(before_range->CompareEndpoints(TextPatternRangeEndpoint_End,after_range.Get(),TextPatternRangeEndpoint_End,&end_delta),"compare selection end across stale operations");
    check(range_text(fresh.Get())==L"過期測試","fresh document range is still current after stale operations");
    log_file<<"edge selection_after count="<<after_count<<" same="<<same<<" start_delta="<<start_delta<<" end_delta="<<end_delta<<std::endl;
    check(after_count==before_count&&same&&start_delta==0&&end_delta==0,"stale operations leave selection count and exact endpoints unchanged");
    log_file<<"stale_edge_failures="<<failures<<std::endl;
    check(failures==0,"every stale-range operation reports UIA_E_ELEMENTNOTAVAILABLE");
}
static std::vector<double> rects(IUIAutomationTextRange* range){
    SAFEARRAY* array{};success(range->GetBoundingRectangles(&array),"get bounding rectangles for geometry");
    LONG lower{},upper{};SafeArrayGetLBound(array,1,&lower);SafeArrayGetUBound(array,1,&upper);
    std::vector<double> result;for(LONG i=lower;i<=upper;++i){double v{};SafeArrayGetElement(array,&i,&v);result.push_back(v);}
    SafeArrayDestroy(array);return result;
}
static RECT client_screen(HWND window){RECT r{};GetClientRect(window,&r);POINT a{0,0},b{r.right,r.bottom};ClientToScreen(window,&a);ClientToScreen(window,&b);return {a.x,a.y,b.x,b.y};}
static void log_rect(const char* name,const std::vector<double>& r){log_file<<"geometry "<<name<<" count="<<r.size()/4;for(double v:r)log_file<<' '<<v;log_file<<std::endl;}
static bool close_to(double a,double b){return std::abs(a-b)<0.01;}
static bool same_rect(const std::vector<double>& a,const std::vector<double>& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(!close_to(a[i],b[i]))return false;return true;}
// Resize the window so that its client area has the requested height, keeping width and position.
static RECT resize_client_height(HWND window,int height){
    RECT outer{},inner{};GetWindowRect(window,&outer);GetClientRect(window,&inner);
    int extra=(outer.bottom-outer.top)-inner.bottom;
    check(SetWindowPos(window,nullptr,0,0,outer.right-outer.left,height+extra,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize test host client height");
    return client_screen(window);
}
static int visible_range_count(IUIAutomationTextPattern* pattern,ComPtr<IUIAutomationTextRange>* first=nullptr){
    ComPtr<IUIAutomationTextRangeArray> visible;success(pattern->GetVisibleRanges(&visible),"get visible ranges for geometry");
    int count{};success(visible->get_Length(&count),"count visible ranges for geometry");
    if(first&&count>0)success(visible->GetElement(0,first->ReleaseAndGetAddressOf()),"get first visible range");
    return count;
}
// Geometry-only changes on retained ranges. Same-DPI size changes only; cross-monitor DPI is not exercised.
static void geometry_changes(IUIAutomationElement* element,IUIAutomationValuePattern* value,HWND window){
    ComPtr<IUnknown> raw;ComPtr<IUIAutomationTextPattern> pattern;
    success(element->GetCurrentPattern(UIA_TextPatternId,&raw),"get Text pattern for geometry");success(raw.As(&pattern),"query Text pattern for geometry");
    RECT original{};GetWindowRect(window,&original);
    const std::wstring fixture=L"跨程序 UIA 456";success(write_value(value,fixture),"set geometry fixture");
    ComPtr<IUIAutomationTextRange> document,first;int moved{};
    success(pattern->get_DocumentRange(&document),"get geometry document");success(document->Clone(&first),"clone geometry range");
    success(first->MoveEndpointByRange(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start),"collapse geometry range");
    success(first->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&moved),"extend geometry range");
    success(first->ScrollIntoView(TRUE),"reset horizontal scroll for geometry");success(first->Select(),"select geometry range");
    // Retained ranges must keep exact endpoints, text, selection and Value; a revision change would make them fail.
    auto unchanged=[&](const char* name){
        int start=9,end=9;BOOL same=FALSE;
        success(first->CompareEndpoints(TextPatternRangeEndpoint_Start,document.Get(),TextPatternRangeEndpoint_Start,&start),"compare retained start");
        success(first->CompareEndpoints(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start,&end),"compare retained end");
        ComPtr<IUIAutomationTextRangeArray> selection;success(pattern->GetSelection(&selection),"read geometry selection");
        int count{};success(selection->get_Length(&count),"count geometry selection");
        ComPtr<IUIAutomationTextRange> selected;if(count==1){success(selection->GetElement(0,&selected),"get geometry selection");success(selected->Compare(first.Get(),&same),"compare geometry selection");}
        bool ok=start==0&&end==1&&range_text(first.Get())==L"跨"&&range_text(document.Get())==fixture&&current_value(value)==fixture&&count==1&&same;
        log_file<<"geometry unchanged_after="<<name<<" ok="<<ok<<std::endl;check(ok,"geometry-only change keeps retained endpoints, text, selection and Value");
    };
    // Hit-test just inside the reported left and right edges of the retained first character.
    auto hit_edges=[&](const char* name,const std::vector<double>& r){
        check(r.size()==4,"hit-test rectangle exists");
        LONG y=static_cast<LONG>(std::lround(r[1]+r[3]/2));
        ComPtr<IUIAutomationTextRange> left,right;int at_start=9,at_end=9;
        success(pattern->RangeFromPoint({static_cast<LONG>(std::lround(r[0]+1)),y},&left),"hit test retained left edge");
        success(pattern->RangeFromPoint({static_cast<LONG>(std::lround(r[0]+r[2]-1)),y},&right),"hit test retained right edge");
        success(left->CompareEndpoints(TextPatternRangeEndpoint_Start,first.Get(),TextPatternRangeEndpoint_Start,&at_start),"compare left-edge hit");
        success(right->CompareEndpoints(TextPatternRangeEndpoint_Start,first.Get(),TextPatternRangeEndpoint_End,&at_end),"compare right-edge hit");
        log_file<<"geometry hit_after="<<name<<" left_vs_start="<<at_start<<" right_vs_end="<<at_end<<std::endl;
        check(at_start==0&&at_end==0,"hit tests at reported rectangle edges resolve to retained endpoints");
    };
    auto base=rects(first.Get());log_rect("base",base);RECT client=client_screen(window);
    check(base.size()==4&&close_to(base[0],client.left+24)&&close_to(base[1],client.top+40),"base range starts at rendered text origin");
    hit_edges("base",base);

    // Window move: same rectangle size, translated by the window delta.
    check(SetWindowPos(window,nullptr,original.left+37,original.top+23,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"move test host");
    auto moved_rect=rects(first.Get());log_rect("moved",moved_rect);unchanged("move");
    check(moved_rect.size()==4&&close_to(moved_rect[0]-base[0],37)&&close_to(moved_rect[1]-base[1],23)&&close_to(moved_rect[2],base[2])&&close_to(moved_rect[3],base[3]),"window move translates retained rectangle exactly");
    hit_edges("move",moved_rect);
    check(SetWindowPos(window,nullptr,original.left,original.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"restore test host position");
    check(same_rect(rects(first.Get()),base),"restored position restores rectangle");

    // Font size at the same DPI (F3 smaller, F4 restores). Not a cross-monitor DPI change.
    SendMessageW(window,WM_KEYDOWN,VK_F3,0);auto smaller=rects(first.Get());log_rect("f3",smaller);unchanged("f3");
    check(smaller.size()==4&&close_to(smaller[0],base[0])&&smaller[3]<base[3]&&smaller[2]<=base[2],"smaller font shrinks retained rectangle at same origin");
    hit_edges("f3",smaller);
    SendMessageW(window,WM_KEYDOWN,VK_F4,0);check(same_rect(rects(first.Get()),base),"F4 restores retained rectangle");unchanged("f4");hit_edges("f4",base);

    // Font family toggle twice; height depends only on pixel size.
    SendMessageW(window,WM_KEYDOWN,VK_F2,0);auto other_font=rects(first.Get());log_rect("f2",other_font);unchanged("f2");
    check(other_font.size()==4&&close_to(other_font[0],base[0])&&close_to(other_font[3],base[3])&&other_font[2]>0,"font family change keeps retained rectangle at origin and line height");
    hit_edges("f2",other_font);
    SendMessageW(window,WM_KEYDOWN,VK_F2,0);check(same_rect(rects(first.Get()),base),"second F2 restores rectangle");unchanged("f2-restore");hit_edges("f2-restore",base);

    // Narrow client width (geometry only). Viewport is [client.left+24, client.right-24); content width is
    // client width - 48. The test host opts out of native minimum tracking size in noninteractive test mode.
    auto set_client_width=[&](int width){
        RECT outer{},inner{};GetWindowRect(window,&outer);GetClientRect(window,&inner);
        int extra=(outer.right-outer.left)-inner.right;
        check(SetWindowPos(window,nullptr,0,0,width+extra,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize test host client width");
        RECT c=client_screen(window);log_file<<"geometry requested_client_width="<<width<<" actual="<<c.right-c.left<<std::endl;
        check(c.right-c.left==width,"requested client width achieved");return c;
    };
    // Host renders to GL_BACK without presenting and returns one client pixel (0 = outside client).
    auto render=[&](const char* name,double sx,double sy){
        POINT p{static_cast<LONG>(std::floor(sx)),static_cast<LONG>(std::floor(sy))};ScreenToClient(window,&p);
        LRESULT packed=SendMessageW(window,WM_APP+27,0,MAKELPARAM(p.x,p.y));
        check(packed!=0,"render witness sample inside client");
        unsigned rgb=static_cast<unsigned>(packed)&0xffffff;log_file<<"geometry render "<<name<<" client="<<p.x<<','<<p.y<<" rgb="<<(rgb>>16)<<','<<((rgb>>8)&255)<<','<<(rgb&255)<<std::endl;
        return rgb;
    };
    constexpr unsigned highlight=(42u<<16)|(78u<<8)|123u,margin=(18u<<16)|(25u<<8)|38u;
    const double sample_y=base[1]+base[3]-3;
    check(render("base_inside",base[0]+2,sample_y)==highlight,"base render witness is selection highlight");

    // Partly clipped first character: viewport narrower than the glyph.
    const int partial_width=48+static_cast<int>(base[2]/2);
    RECT narrow=set_client_width(partial_width);const double narrow_left=narrow.left+24,narrow_right=narrow.right-24;
    auto partial=rects(first.Get());log_rect("narrow_first",partial);unchanged("narrow");
    check(partial.size()==4&&partial[0]>=narrow_left-0.01&&close_to(partial[0]+partial[2],narrow_right)&&partial[2]<base[2],"first character partly clipped by right margin");
    check(visible_range_count(pattern.Get())==1,"partly clipped width still reports one visible range");
    check(render("narrow_inside",partial[0]+partial[2]/2,sample_y)==highlight,"render highlight inside clipped UIA rectangle");
    check(render("narrow_past_right",narrow_right+2,sample_y)==margin,"render margin just past clipped UIA right edge");

    // Width 49: one pixel of content.
    RECT one=set_client_width(49);auto sliver=rects(first.Get());log_rect("width49",sliver);unchanged("width49");
    check(sliver.size()==4&&close_to(sliver[0],one.left+24)&&close_to(sliver[2],1),"one-pixel viewport reports one-pixel rectangle");
    check(visible_range_count(pattern.Get())==1,"one-pixel viewport reports one visible range");
    check(render("width49_content",one.left+24,sample_y)==highlight,"render one-pixel content column is highlighted");

    // Width 48 boundary and below: zero content width. The range itself is not degenerate; the viewport is empty.
    for(int width:{48,30}){
        RECT zero=set_client_width(width);auto none=rects(first.Get());int count=visible_range_count(pattern.Get());
        log_rect(width==48?"width48":"width30",none);log_file<<"geometry zero_width="<<width<<" visible_ranges="<<count<<std::endl;
        unchanged(width==48?"width48":"width30");
        check(none.empty()&&count==0,"zero content width has no rectangles and no visible ranges");
        check(range_text(document.Get())==fixture,"zero viewport does not hide text from range content");
        check(render(width==48?"width48_center":"width30_center",zero.left+width/2.0,sample_y)==margin,"render zero content width shows only margin");
    }

    // Recovery: caret-visible scrolling during narrow resizes may persist; ScrollIntoView restores the origin.
    check(SetWindowPos(window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"restore test host width");
    auto after_resize=rects(first.Get());log_rect("restore_width_before_scroll",after_resize);unchanged("restore-width");
    success(first->ScrollIntoView(TRUE),"scroll retained range back to origin");
    check(same_rect(rects(first.Get()),base),"restored width and scroll restore rectangle");unchanged("restore-width-scroll");
    check(render("restored_inside",base[0]+2,sample_y)==highlight,"restored render witness is selection highlight");

    // Short viewport: line box taller than remaining client height must be clipped to the client bottom.
    const int line=static_cast<int>(std::lround(base[3]));
    RECT short_client=resize_client_height(window,40+line/2);
    log_file<<"geometry short_client_height="<<short_client.bottom-short_client.top<<" line_height="<<line<<std::endl;
    check(short_client.bottom-short_client.top<40+line&&short_client.bottom-short_client.top>40,"short viewport height achieved");
    auto clipped=rects(first.Get());log_rect("short",clipped);unchanged("short");
    log_file<<"geometry short_rect_bottom="<<(clipped.size()==4?clipped[1]+clipped[3]:0)<<" client_bottom="<<short_client.bottom<<std::endl;
    check(clipped.size()==4&&close_to(clipped[1],base[1])&&clipped[1]+clipped[3]<=short_client.bottom+0.01,"partly clipped line rectangle stays inside client area");
    check(visible_range_count(pattern.Get())==1,"partly clipped viewport still reports one visible range");

    // Zero content height: client ends above the text line, so nothing is visible.
    RECT tiny_client=resize_client_height(window,30);
    log_file<<"geometry tiny_client_height="<<tiny_client.bottom-tiny_client.top<<std::endl;
    check(tiny_client.bottom-tiny_client.top<=30,"zero-content viewport height achieved");
    auto hidden=rects(first.Get());log_rect("tiny",hidden);unchanged("tiny");
    int tiny_visible=visible_range_count(pattern.Get());log_file<<"geometry tiny_visible_ranges="<<tiny_visible<<std::endl;
    check(hidden.empty(),"fully clipped range has no bounding rectangles");
    check(tiny_visible==0,"zero-content viewport reports no visible ranges");
    check(SetWindowPos(window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"restore test host size");
    check(same_rect(rects(first.Get()),base),"restored size restores rectangle");unchanged("restore-size");

    // Horizontal scroll on a long line: full clip, partial clip, visible range and hit test follow the viewport.
    std::wstring long_text=L"開頭";for(int i=0;i<40;++i)long_text+=L"繁體文字";
    success(write_value(value,long_text),"set long geometry document");
    ComPtr<IUIAutomationTextRange> whole,head,target;success(pattern->get_DocumentRange(&whole),"get long geometry document");
    success(whole->Clone(&head),"clone long head");success(head->MoveEndpointByRange(TextPatternRangeEndpoint_End,whole.Get(),TextPatternRangeEndpoint_Start),"collapse long head");
    success(head->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&moved),"extend long head");
    success(head->Clone(&target),"clone scroll target");success(target->Move(TextUnit_Character,50,&moved),"move scroll target");check(moved==50,"scroll target is character 50");
    success(head->Select(),"select long head");
    success(target->ScrollIntoView(TRUE),"scroll character 50 to viewport left");client=client_screen(window);
    const double view_left=client.left+24,view_right=client.right-24;
    auto target_rect=rects(target.Get()),head_rect=rects(head.Get()),whole_rect=rects(whole.Get());
    log_rect("scroll_target",target_rect);log_rect("scroll_head",head_rect);log_rect("scroll_whole",whole_rect);
    check(target_rect.size()==4&&close_to(target_rect[0],view_left),"scrolled target aligns with viewport left");
    check(head_rect.empty(),"range scrolled out of view has no rectangle");
    check(whole_rect.size()==4&&close_to(whole_rect[0],view_left)&&close_to(whole_rect[0]+whole_rect[2],view_right),"long range is clipped to both viewport edges");
    ComPtr<IUIAutomationTextRange> visible;check(visible_range_count(pattern.Get(),&visible)==1,"scrolled viewport has one visible range");
    int order=9;success(visible->CompareEndpoints(TextPatternRangeEndpoint_Start,target.Get(),TextPatternRangeEndpoint_Start,&order),"compare visible start");
    int tail=9;success(visible->CompareEndpoints(TextPatternRangeEndpoint_End,whole.Get(),TextPatternRangeEndpoint_End,&tail),"compare visible end");
    log_file<<"geometry visible_start_vs_target="<<order<<" visible_end_vs_document="<<tail<<std::endl;
    check(order==0&&tail<0,"visible range starts at scrolled offset and ends before document end");
    ComPtr<IUIAutomationTextRange> hit;POINT p{static_cast<LONG>(std::lround(view_left+1)),static_cast<LONG>(std::lround(target_rect[1]+target_rect[3]/2))};
    success(pattern->RangeFromPoint(p,&hit),"hit test scrolled viewport");
    success(hit->CompareEndpoints(TextPatternRangeEndpoint_Start,target.Get(),TextPatternRangeEndpoint_Start,&order),"compare scrolled hit");
    check(order==0,"hit test at viewport left resolves to scrolled offset");
    ComPtr<IUIAutomationTextRangeArray> selection;success(pattern->GetSelection(&selection),"read selection after scroll");
    ComPtr<IUIAutomationTextRange> selected;success(selection->GetElement(0,&selected),"get selection after scroll");BOOL same=FALSE;
    success(selected->Compare(head.Get(),&same),"compare selection after scroll");check(same&&current_value(value)==long_text,"horizontal scroll keeps selection and Value");
    success(head->ScrollIntoView(TRUE),"scroll back to document start");
    auto restored_head=rects(head.Get());log_rect("scroll_back_head",restored_head);
    check(restored_head.size()==4&&close_to(restored_head[0],view_left),"scrolling back restores head rectangle");
}
static void final_selection(IUIAutomationElement* element,IUIAutomationValuePattern* value,HWND window){
    ComPtr<IUnknown> raw;ComPtr<IUIAutomationTextPattern> pattern;ComPtr<IUIAutomationTextRange> document,first;int moved{};
    success(element->GetCurrentPattern(UIA_TextPatternId,&raw),"get Text pattern for final selection");success(raw.As(&pattern),"query Text pattern for final selection");
    success(write_value(value,L"跨程序 UIA 456"),"restore test fixture");
    success(pattern->get_DocumentRange(&document),"get restored document");success(document->Clone(&first),"clone restored document");
    success(first->MoveEndpointByRange(TextPatternRangeEndpoint_End,document.Get(),TextPatternRangeEndpoint_Start),"collapse restored range");
    success(first->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&moved),"extend restored first character");success(first->Select(),"leave first character selected for host verification");
    auto final_rect=rectangle(first.Get());
    std::ofstream bounds("uia-text-rectangle.tsv",std::ios::binary);
    bounds<<final_rect[0]<<'\t'<<final_rect[1]<<'\t'<<final_rect[2]<<'\t'<<final_rect[3]<<'\n';
    check(bool(bounds),"export actual UIA text rectangle for renderer comparison");
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
            check(argc>=3,"host HWND and process identity supplied");
            bool concurrent=false,lifecycle=false;
            for(int i=3;i<argc;++i){if(std::string(argv[i])=="--concurrent")concurrent=true;if(std::string(argv[i])=="--lifecycle")lifecycle=true;}
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
            if(lifecycle){retired_provider(window,automation.Get(),element.Get(),pattern.Get());window=nullptr;log_file<<"PASS retired provider lifecycle"<<std::endl;}
            else{
            PROPERTYID property=UIA_ValueValuePropertyId;
            success(automation->AddPropertyChangedEventHandlerNativeArray(element.Get(),TreeScope_Element,nullptr,changes.Get(),&property,1),"subscribe to Value change events");
            success(automation->AddAutomationEventHandler(UIA_Text_TextChangedEventId,element.Get(),TreeScope_Element,nullptr,changes.Get()),"subscribe to TextChanged");
            success(automation->AddAutomationEventHandler(UIA_Text_TextSelectionChangedEventId,element.Get(),TreeScope_Element,nullptr,changes.Get()),"subscribe to TextSelectionChanged");
            success(write_value(pattern.Get(),L"跨程序 UIA 456"),"write Value across process boundary");
            check(current_value(pattern.Get())==L"跨程序 UIA 456","cross-process value round trip");
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!changes->expected_value&&std::chrono::steady_clock::now()<deadline)Sleep(10);
            check(changes->expected_value,"receive external Value property change event with expected payload");
            log_file<<"value_events="<<changes->count.load()<<std::endl;

            HRESULT invalid=write_value(pattern.Get(),L"line one\nline two");
            log_file<<"multiline_hresult=0x"<<std::hex<<static_cast<unsigned long>(invalid)<<std::dec<<std::endl;
            check(invalid==E_INVALIDARG,"single-line provider rejects multiline mutation");
            check(current_value(pattern.Get())==L"跨程序 UIA 456","rejected mutation preserves value");

            // Test-only fixture hook. This is not an actual IME keystroke test.
            check(SendMessageW(window,WM_APP+21,1,0)==1,"start simulated composition fixture");
            HRESULT composing=write_value(pattern.Get(),L"must not replace composition");
            log_file<<"composition_hresult=0x"<<std::hex<<static_cast<unsigned long>(composing)<<std::dec<<std::endl;
            check(composing==UIA_E_INVALIDOPERATION,"external write rejected during composition");
            check(current_value(pattern.Get())==L"跨程序 UIA 456","composition protection preserves committed text");
            SendMessageW(window,WM_APP+21,0,0);

            check(SetWindowPos(window,nullptr,0,0,760,510,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize dedicated test host");
            RECT expected{},actual{};GetWindowRect(window,&expected);success(element->get_CurrentBoundingRectangle(&actual),"query external HWND bounds after resize");
            check(EqualRect(&expected,&actual)!=FALSE,"bounds match physical HWND coordinates");
            VARIANT available{};success(element->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId,&available),"query TextPattern capability");
            bool present=available.vt==VT_BOOL&&available.boolVal==VARIANT_TRUE;VariantClear(&available);
            check(present,"single-line TextPattern capability is advertised");
            if(concurrent)concurrent_reads(window,pattern.Get());
            text_ranges(element.Get(),pattern.Get(),window,changes.Get());
            range_contract_edges(automation.Get(),element.Get(),pattern.Get());
            geometry_changes(element.Get(),pattern.Get(),window);
            final_selection(element.Get(),pattern.Get(),window);
            success(automation->RemovePropertyChangedEventHandler(element.Get(),changes.Get()),"unsubscribe event handler");
            log_file<<"PASS cross-process Value and limited single-line TextRange batch; no Narrator claim"<<std::endl;
            }
        }catch(const std::exception& e){log_file<<"FAIL "<<e.what()<<std::endl;result=1;}
        if(window)SendMessageW(window,WM_APP+21,0,0);
        if(automation)automation->RemoveAllEventHandlers();
    }
    CoUninitialize();return result;
}
