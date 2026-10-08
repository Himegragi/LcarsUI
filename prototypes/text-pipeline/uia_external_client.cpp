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
