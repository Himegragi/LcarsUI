#pragma once
// Included after Bridge/ValueProvider. Prototype ranges are revision-bound:
// text mutations invalidate old ranges; geometry-only changes keep them valid.
class TextRange final:public ITextRangeProvider {
    std::atomic<ULONG> references{1};
    ValueProvider* owner;
    unsigned first,last;
    std::uint64_t revision;
    Bridge& b()const{return *owner->bridge;}
    HRESULT valid()const{return b().alive&&b().revision==revision?S_OK:UIA_E_ELEMENTNOTAVAILABLE;}
    static bool endpoint(TextPatternRangeEndpoint e){return e==TextPatternRangeEndpoint_Start||e==TextPatternRangeEndpoint_End;}
    std::vector<unsigned> stops()const{std::vector<unsigned> result;for(auto [offset,x]:b().positions)result.push_back(offset);if(result.empty())result.push_back(0);return result;}
    double x(unsigned offset)const{auto it=b().positions.upper_bound(offset);return it==b().positions.begin()?b().bounds.left:std::prev(it)->second;}
public:
    TextRange(ValueProvider* p,unsigned a,unsigned z,std::uint64_t v):owner(p),first(a),last(z),revision(v){owner->AddRef();}
    ~TextRange(){owner->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextRangeProvider))return E_NOINTERFACE;
        *out=static_cast<ITextRangeProvider*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** out)override{
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        *out=new TextRange(owner,first,last,revision);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range,BOOL* out)override{
        if(!out)return E_POINTER;*out=FALSE;auto other=dynamic_cast<TextRange*>(range);if(!other||other->owner!=owner)return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid())||FAILED(other->valid()))return UIA_E_ELEMENTNOTAVAILABLE;
        *out=first==other->first&&last==other->last;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint end,ITextRangeProvider* range,TextPatternRangeEndpoint other_end,int* out)override{
        if(!out)return E_POINTER;*out=0;auto other=dynamic_cast<TextRange*>(range);
        if(!other||other->owner!=owner||!endpoint(end)||!endpoint(other_end))return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid())||FAILED(other->valid()))return UIA_E_ELEMENTNOTAVAILABLE;
        unsigned a=end==TextPatternRangeEndpoint_Start?first:last,z=other_end==TextPatternRangeEndpoint_Start?other->first:other->last;
        *out=a<z?-1:a>z?1:0;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit)override{
        if(unit<TextUnit_Character||unit>TextUnit_Document)return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        if(unit!=TextUnit_Character){first=0;last=(unsigned)b().visible.size();return S_OK;}
        auto edges=stops();auto it=std::upper_bound(edges.begin(),edges.end(),first);
        if(it==edges.end()){if(edges.size()>1){last=edges.back();first=edges[edges.size()-2];}else first=last=0;}
        else{last=*it;first=it==edges.begin()?0:*std::prev(it);}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID attr,VARIANT value,BOOL,ITextRangeProvider** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(attr!=UIA_IsReadOnlyAttributeId)return UIA_E_NOTSUPPORTED;
        if(value.vt!=VT_BOOL)return E_INVALIDARG;
        if(value.boolVal==VARIANT_FALSE)return Clone(out);
        std::lock_guard lock(b().mutex);return valid();  // no match, but only for a current range
    }
    HRESULT STDMETHODCALLTYPE FindText(BSTR text,BOOL backward,BOOL ignore_case,ITextRangeProvider** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(!text||SysStringLen(text)==0)return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        unsigned length=SysStringLen(text);if(length>last-first)return S_OK;
        for(unsigned step=0;step<=last-first-length;++step){unsigned pos=backward?last-length-step:first+step;
            if(CompareStringOrdinal(b().visible.data()+pos,(int)length,text,(int)length,ignore_case)==CSTR_EQUAL){*out=new TextRange(owner,pos,pos+length,revision);break;}}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attr,VARIANT* out)override{
        if(!out)return E_POINTER;VariantInit(out);std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        if(attr==UIA_IsReadOnlyAttributeId){out->vt=VT_BOOL;out->boolVal=VARIANT_FALSE;return S_OK;}
        out->vt=VT_UNKNOWN;return UiaGetReservedNotSupportedValue(&out->punkVal);
    }
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** out)override{
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        double left=std::max<double>(x(first),b().bounds.left),right=std::min<double>(x(last),b().bounds.right);
        bool visible=first!=last&&right>left&&b().bounds.bottom>b().bounds.top;
        *out=SafeArrayCreateVector(VT_R8,0,visible?4:0);if(!*out)return E_OUTOFMEMORY;
        if(visible){double rect[]={left,double(b().bounds.top),right-left,double(b().bounds.bottom-b().bounds.top)};
            for(LONG i=0;i<4;++i)SafeArrayPutElement(*out,&i,&rect[i]);}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** out)override{
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        *out=owner;owner->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetText(int max_length,BSTR* out)override{
        if(!out)return E_POINTER;*out=nullptr;if(max_length<-1)return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        unsigned length=last-first;if(max_length>=0)length=std::min(length,(unsigned)max_length);
        *out=SysAllocStringLen(b().visible.data()+first,length);return *out?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit,int count,int* moved)override{
        if(!moved)return E_POINTER;*moved=0;if(unit<TextUnit_Character||unit>TextUnit_Document)return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();if(!count)return S_OK;
        if(unit!=TextUnit_Character){first=0;last=(unsigned)b().visible.size();return S_OK;}
        auto edges=stops();bool collapsed=first==last;
        auto it=std::upper_bound(edges.begin(),edges.end(),first);
        long long index=it==edges.begin()?0:std::distance(edges.begin(),it)-1;
        long long maximum=std::max(0LL,(long long)edges.size()-(collapsed?1:2));index=std::min(index,maximum);
        long long next=std::clamp(index+(long long)count,0LL,maximum);
        first=edges[(size_t)next];last=collapsed?first:edges[std::min((size_t)next+1,edges.size()-1)];*moved=(int)(next-index);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint end,TextUnit unit,int count,int* moved)override{
        if(!moved)return E_POINTER;*moved=0;if(!endpoint(end)||unit<TextUnit_Character||unit>TextUnit_Document)return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();if(!count)return S_OK;
        auto edges=unit==TextUnit_Character?stops():std::vector<unsigned>{0,(unsigned)b().visible.size()};
        unsigned position=end==TextPatternRangeEndpoint_Start?first:last;
        auto it=std::lower_bound(edges.begin(),edges.end(),position);
        long long index=std::min<long long>(std::distance(edges.begin(),it),edges.size()-1);
        long long next=std::clamp(index+(long long)count,0LL,(long long)edges.size()-1);
        position=edges[(size_t)next];*moved=(int)(next-index);
        if(end==TextPatternRangeEndpoint_Start){first=position;last=std::max(last,first);}else{last=position;first=std::min(first,last);}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint end,ITextRangeProvider* range,TextPatternRangeEndpoint target)override{
        auto other=dynamic_cast<TextRange*>(range);if(!other||other->owner!=owner||!endpoint(end)||!endpoint(target))return E_INVALIDARG;
        std::lock_guard lock(b().mutex);if(FAILED(valid())||FAILED(other->valid()))return UIA_E_ELEMENTNOTAVAILABLE;
        auto value=target==TextPatternRangeEndpoint_Start?other->first:other->last;
        if(end==TextPatternRangeEndpoint_Start){first=value;last=std::max(last,first);}else{last=value;first=std::min(first,last);}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select()override{
        HWND window{};RangeSelection selection{};
        {std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();window=b().window;selection={first,last};}
        return static_cast<HRESULT>(SendMessageW(window,select_range_message,0,reinterpret_cast<LPARAM>(&selection)));
    }
    HRESULT STDMETHODCALLTYPE AddToSelection()override{
        {std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
            if(b().selection_begin!=b().selection_end&&(b().selection_begin!=first||b().selection_end!=last))return UIA_E_INVALIDOPERATION;}
        return Select();
    }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection()override{
        std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();return UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL align_top)override{
        HWND window{};unsigned position;
        {std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();window=b().window;position=align_top?first:last;}
        return static_cast<HRESULT>(SendMessageW(window,scroll_range_message,align_top,position));
    }
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** out)override{
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(b().mutex);if(FAILED(valid()))return valid();
        *out=SafeArrayCreateVector(VT_UNKNOWN,0,0);return *out?S_OK:E_OUTOFMEMORY;
    }
};
static HRESULT range_array(ITextRangeProvider* range,SAFEARRAY** out){
    *out=SafeArrayCreateVector(VT_UNKNOWN,0,range?1:0);
    if(!*out){if(range)range->Release();return E_OUTOFMEMORY;}
    if(range){LONG index=0;HRESULT hr=SafeArrayPutElement(*out,&index,range);range->Release();return hr;}return S_OK;
}
HRESULT ValueProvider::GetSelection(SAFEARRAY** out){
    if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
    trace_selection_snapshot(bridge->selection_begin,bridge->selection_end,bridge->visible.size(),bridge->composing);
    return range_array(new TextRange(this,bridge->selection_begin,bridge->selection_end,bridge->revision),out);
}
HRESULT ValueProvider::GetVisibleRanges(SAFEARRAY** out){
    if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
    if(bridge->bounds.right<=bridge->bounds.left||bridge->bounds.bottom<=bridge->bounds.top)return range_array(nullptr,out);
    unsigned first=0,last=(unsigned)bridge->visible.size();
    for(auto [offset,x]:bridge->positions){if(x<=bridge->bounds.left)first=offset;if(x>=bridge->bounds.right){last=offset;break;}}
    return range_array(new TextRange(this,first,last,bridge->revision),out);
}
HRESULT ValueProvider::RangeFromPoint(UiaPoint point,ITextRangeProvider** out){
    if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
    unsigned position=0;double distance=1e300;
    for(auto [offset,x]:bridge->positions)if(std::abs(x-point.x)<distance){distance=std::abs(x-point.x);position=offset;}
    *out=new TextRange(this,position,position,bridge->revision);return S_OK;
}
HRESULT ValueProvider::get_DocumentRange(ITextRangeProvider** out){
    if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(bridge->mutex);if(!bridge->alive)return UIA_E_ELEMENTNOTAVAILABLE;
    *out=new TextRange(this,0,(unsigned)bridge->visible.size(),bridge->revision);return S_OK;
}
