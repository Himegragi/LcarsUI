#include "desktop.hpp"
#include "renderer_opengl.hpp"
#include <windowsx.h>
#include <imm.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace settings {
using namespace lcars;
namespace {
using namespace detail;
std::u16string lower(std::u16string value) {
    for(auto& c:value)if(c>=u'A'&&c<=u'Z')c+=u'a'-u'A';
    return value;
}
std::u16string composition(HIMC imc,DWORD kind) {
    LONG bytes=ImmGetCompositionStringW(imc,kind,nullptr,0);
    if(bytes<=0 || bytes>8192 || bytes%2)return {};
    std::u16string value(static_cast<std::size_t>(bytes)/2,u'\0');
    if(ImmGetCompositionStringW(imc,kind,value.data(),bytes)!=bytes)return {};
    return value;
}
struct Item { Id id; Rect bounds,editor; bool in_content=false; };
struct Host {
    Ui& ui;
    const Appearance& appearance;
    std::function<Screen()> get_screen;
    HWND window=nullptr;
    Text text;
    std::unique_ptr<Renderer> renderer;
    std::vector<Item> items;
    std::map<Id,float> horizontal;
    Id focus="profile.name",hover,pressed,return_focus;
    char16_t pending_high=0;
    bool dragging=false,closing=false,failed=false,test_mode=false,was_modal=false;
    int scroll=0,max_scroll=0,caret_height=0;
    std::u16string last_query;
    float scale=1;
    Rect content;
    std::u16string input_error;
    explicit Host(Ui& model,const Appearance& skin,std::function<Screen()> screen):ui(model),appearance(skin),get_screen(std::move(screen)){}
    int d(int value) const {return static_cast<int>(std::lround(value*scale));}
    EditSession* editor(){return ui.edit(focus);}
    Item* item(std::string_view id){auto it=std::find_if(items.begin(),items.end(),[&](auto& i){return i.id==id;});return it==items.end()?nullptr:&*it;}
    void invalidate(){if(window)InvalidateRect(window,nullptr,FALSE);}
    void cancel_composition(){
        pending_high=0;
        if(auto e=editor();e&&e->composing){
            auto imc=ImmGetContext(window);
            if(imc){ImmNotifyIME(imc,NI_COMPOSITIONSTR,CPS_CANCEL,0);ImmReleaseContext(window,imc);}
            e->cancel_composition();
        }
    }
    void end_drag(){dragging=false;if(GetCapture()==window)ReleaseCapture();}
    void set_focus(Id id){
        if(focus==id)return;
        cancel_composition();end_drag();focus=std::move(id);input_error.clear();invalidate();
    }
    bool matches(const Control& c,const Screen& screen,const std::u16string& query){
        if(c.kind==Kind::navigation||c.page=="footer"||c.page=="dialog"||c.page=="search")return false;
        return query.empty()?c.page==screen.page:lower(c.label).find(query)!=std::u16string::npos;
    }
    void frame(Canvas& canvas){
        auto screen=get_screen();auto p=appearance.palette();
        scale=GetDpiForWindow(window)/96.f;text.set_ming(screen.ming_font);
        const int size=d(screen.font_size),small=d(14),left=d(244),width=canvas.width-left-d(36);
        content={left,d(270),width,std::max(0,canvas.height-d(420))};
        if(ui.modal()!=was_modal){
            if(ui.modal()){return_focus=focus;cancel_composition();focus="dialog.cancel";}
            else focus=return_focus;
            was_modal=ui.modal();
        }
        items.clear();
        canvas.fill({0,0,d(208),canvas.height},p.panel);
        canvas.fill({d(207),0,d(1),canvas.height},p.border);
        canvas.rounded({d(24),d(30),d(40),d(40)},d(12),p.accent);
        text.label(canvas,u"L",{d(37),d(34),d(25),d(34)},d(25),p.on_accent);
        text.label(canvas,u"LCARS UI",{d(77),d(33),d(120),d(32)},d(21),p.text);
        text.label(canvas,u"設定管理",{d(26),d(94),d(160),d(25)},small,p.muted);
        int nav_y=d(136);
        for(auto& c:ui.controls())if(c.kind==Kind::navigation){
            Rect r{d(16),nav_y,d(176),d(48)};
            bool selected=c.page==screen.page;
            if(selected||hover==c.id)canvas.rounded(r,d(appearance.radius()),selected?p.selection:p.background);
            if(focus==c.id){canvas.rounded(r,d(appearance.radius()),p.accent);canvas.rounded({r.x+d(2),r.y+d(2),r.w-d(4),r.h-d(4)},d(appearance.radius()),p.selection);}
            text.label(canvas,c.label,{r.x+d(16),r.y+d(12),r.w-d(24),d(28)},d(17),selected?p.accent:p.text);
            items.push_back({c.id,r,{}});nav_y+=d(58);
        }
        text.label(canvas,u"DESKTOP / MVP 0.1",{d(24),canvas.height-d(75),d(176),d(24)},d(12),p.muted);
        auto skin=appearance.name();std::u16string skin_name(skin.begin(),skin.end());
        text.label(canvas,skin_name,{d(24),canvas.height-d(49),d(176),d(24)},d(12),p.muted);
        auto query=lower(ui.find("search")->value);
        if(query!=last_query){scroll=0;last_query=query;}
        text.label(canvas,query.empty()?screen.heading:u"搜尋結果",{left,d(35),width,d(48)},d(32),p.text);
        text.label(canvas,query.empty()?screen.description:u"顯示所有頁面中符合名稱的設定。",{left,d(98),width,d(28)},d(15),p.muted);
        auto field=[&](const Control& c,Rect r,bool in_content){
            // Appearance geometry is expressed in DIPs, scaled once by the host.
            auto logical=appearance.field({0,0,static_cast<int>(r.w/scale),screen.font_size*2+48},screen.font_size);
            auto physical=[&](Rect b){return Rect{r.x+d(b.x),r.y+d(b.y),d(b.w),d(b.h)};};
            FieldParts parts{physical(logical.label),physical(logical.surface),physical(logical.editor),logical.underline};
            bool focused=focus==c.id;
            canvas.rounded(parts.surface,d(appearance.radius()),focused?p.accent:p.border);
            canvas.rounded({parts.surface.x+d(1),parts.surface.y+d(1),parts.surface.w-d(2),parts.surface.h-d(2)},d(appearance.radius()),p.surface);
            if(parts.underline)canvas.fill({parts.surface.x+d(10),parts.surface.y+parts.surface.h-d(2),parts.surface.w-d(20),d(focused?2:1)},focused?p.accent:p.muted);
            text.label(canvas,c.label,parts.label,parts.underline?small:size,p.muted);
            auto* e=ui.edit(c.id);auto layout=text.shape(e->visible(),size);
            auto& offset=horizontal[c.id];float caret=layout.at(e->visible_caret());
            if(focused){
                if(caret-offset>parts.editor.w-d(4))offset=caret-parts.editor.w+d(4);
                if(caret-offset<0)offset=caret;
            }
            offset=std::clamp(offset,0.f,std::max(0.f,layout.width-parts.editor.w+d(4)));
            auto old_clip=canvas.clip;
            int x=std::max(old_clip.x,parts.editor.x),y=std::max(old_clip.y,parts.editor.y);
            canvas.clip={x,y,std::max(0,std::min(old_clip.x+old_clip.w,parts.editor.x+parts.editor.w)-x),
                std::max(0,std::min(old_clip.y+old_clip.h,parts.editor.y+parts.editor.h)-y)};
            if(focused){
                unsigned first=e->begin(),last=e->composing?e->begin()+static_cast<unsigned>(e->preedit.size()):e->end();
                int sx=parts.editor.x+static_cast<int>(layout.at(first)-offset),ex=parts.editor.x+static_cast<int>(layout.at(last)-offset);
                canvas.fill({sx,parts.editor.y,ex-sx,parts.editor.h},p.selection);
                if(e->composing)canvas.fill({sx,parts.editor.y+size+2,ex-sx,d(1)},p.accent);
            }
            text.draw(canvas,layout,parts.editor.x-static_cast<int>(offset),parts.editor.y+size,p.text);
            if(focused&&GetFocus()==window)canvas.fill({parts.editor.x+static_cast<int>(caret-offset),parts.editor.y,d(1),size+d(3)},p.accent);
            canvas.clip=old_clip;
            items.push_back({c.id,{r.x,r.y,r.w,std::max(parts.surface.y+parts.surface.h-r.y,size*2+d(40))},parts.editor,in_content});
        };
        field(*ui.find("search"),{left,d(156),width,size*2+d(44)},false);
        auto button=[&](const Control& c,Rect r,bool primary,bool in_content){
            bool enabled=bool(ui.available(c.id)),focused=focus==c.id;
            Color background=primary?p.accent:p.panel,ink=primary?p.on_accent:p.text;
            if(!enabled){background=p.background;ink=p.muted;}
            else if(hover==c.id||pressed==c.id)background=primary?p.accent:p.selection;
            canvas.rounded(r,d(appearance.radius()),focused?p.accent:p.border);
            canvas.rounded({r.x+d(1),r.y+d(1),r.w-d(2),r.h-d(2)},d(appearance.radius()),background);
            if(focused)canvas.rounded({r.x+d(3),r.y+d(3),r.w-d(6),r.h-d(6)},d(appearance.radius()),primary?p.accent:p.selection);
            text.label(canvas,c.label,{r.x+d(14),r.y+(r.h-d(20))/2-d(2),r.w-d(28),d(26)},d(16),ink);
            items.push_back({c.id,r,{},in_content});
        };
        auto old_clip=canvas.clip;canvas.clip=content;
        int y=content.y-scroll,count=0;
        for(auto& c:ui.controls())if(matches(c,screen,query)){
            ++count;
            if(c.kind==Kind::text){field(c,{left,y,width,size*2+d(48)},true);y+=size*2+d(76);}
            else if(c.kind==Kind::toggle){
                Rect r{left,y,width,d(64)};
                canvas.rounded(r,d(appearance.radius()),focus==c.id?p.selection:p.panel);
                text.label(canvas,c.label,{left+d(16),y+d(17),width-d(100),d(32)},size,p.text);
                Rect track{left+width-d(66),y+d(19),d(46),d(26)};
                canvas.rounded(track,d(13),c.checked?p.accent:p.border);
                canvas.rounded({track.x+d(c.checked?23:3),track.y+d(3),d(20),d(20)},d(10),p.panel);
                items.push_back({c.id,r,{},true});y+=d(78);
            }else{button(c,{left,y,width,d(52)},false,true);y+=d(64);}
        }
        if(count==0)text.label(canvas,u"沒有符合的設定，請試試其他關鍵字。",{left,y,width,d(40)},d(18),p.muted);
        max_scroll=std::max(0,y+scroll-content.y-content.h);
        scroll=std::clamp(scroll,0,max_scroll);
        canvas.clip=old_clip;
        if(max_scroll>0){
            int track=content.h,thumb=std::max(d(30),track*track/(track+max_scroll));
            canvas.rounded({canvas.width-d(18),content.y+(track-thumb)*scroll/max_scroll,d(5),thumb},d(3),p.border);
        }
        int footer_y=canvas.height-d(126);
        canvas.fill({left,footer_y,width,d(1)},p.border);
        auto status=input_error.empty()?screen.status:input_error;
        if(ui.stopped())status=u"介面因操作錯誤停止，請重新啟動。";
        text.label(canvas,status,{left,footer_y+d(14),width,d(25)},small,input_error.empty()?p.muted:p.error);
        int x=left;
        for(auto id:{"undo","redo","reset","discard","save"}){
            int w=d(std::string_view(id)=="save"?124:std::string_view(id)=="undo"||std::string_view(id)=="redo"?72:108);
            button(*ui.find(id),{x,footer_y+d(56),w,d(44)},std::string_view(id)=="save",false);x+=w+d(10);
        }
        if(ui.modal()){
            for(int yy=0;yy<canvas.height;++yy)for(int xx=0;xx<canvas.width;++xx)canvas.blend(xx,yy,{20,25,35},125);
            Rect box{(canvas.width-d(620))/2,(canvas.height-d(250))/2,d(620),d(250)};
            canvas.rounded(box,d(18),p.panel);
            text.label(canvas,screen.dialog_title,{box.x+d(28),box.y+d(28),box.w-d(56),d(40)},d(25),p.text);
            text.label(canvas,screen.dialog_message,{box.x+d(28),box.y+d(91),box.w-d(56),d(50)},d(16),p.muted);
            button(*ui.find("dialog.cancel"),{box.x+box.w-d(244),box.y+box.h-d(72),d(96),d(44)},false,false);
            button(*ui.find("dialog.confirm"),{box.x+box.w-d(132),box.y+box.h-d(72),d(104),d(44)},true,false);
        }
        sync_ime();
    }
    void draw(const std::filesystem::path& screenshot={}){
        RECT r{};GetClientRect(window,&r);if(r.right<=0||r.bottom<=0)return;
        Canvas canvas(r.right,r.bottom,appearance.palette().background);frame(canvas);
        if(!screenshot.empty())renderer->capture(canvas,screenshot);
        renderer->draw(canvas);
    }
    void sync_ime(){
        auto* e=editor();auto* i=item(focus);
        if(!e||!i||!ui.available(focus)||GetFocus()!=window){
            if(caret_height){DestroyCaret();caret_height=0;}
            return;
        }
        if(caret_height!=i->editor.h){
            if(caret_height)DestroyCaret();
            caret_height=CreateCaret(window,nullptr,d(1),i->editor.h)?i->editor.h:0;
        }
        auto layout=text.shape(e->visible(),d(get_screen().font_size));
        int x=i->editor.x+static_cast<int>(layout.at(e->visible_caret())-horizontal[focus]);
        int y=i->editor.y;SetCaretPos(x,y);
        auto imc=ImmGetContext(window);
        if(imc){
            CANDIDATEFORM cf{};cf.dwStyle=CFS_EXCLUDE;cf.ptCurrentPos={x,y+i->editor.h};cf.rcArea={x,y,x+d(2),y+i->editor.h};
            COMPOSITIONFORM cm{};cm.dwStyle=CFS_POINT;cm.ptCurrentPos={x,y};
            ImmSetCandidateWindow(imc,&cf);ImmSetCompositionWindow(imc,&cm);ImmReleaseContext(window,imc);
        }
    }
    void report(Result result){if(!result)input_error=result.message;else input_error.clear();invalidate();}
    void activate(const Id& id){
        if(!ui.available(id))return;
        cancel_composition();auto old_page=get_screen().page;
        report(ui.invoke(id));
        if(get_screen().page!=old_page)scroll=0;
        if(id=="dialog.cancel")closing=false;
        if(id=="dialog.confirm"&&closing&&!get_screen().dirty)PostQuitMessage(0);
    }
    void replace_selection(std::u16string_view value){
        auto* e=editor();if(!e)return;
        auto next=e->text;auto first=e->begin();next.replace(first,e->end()-first,value);
        auto result=ui.set_value(focus,std::move(next));
        if(result)e->select(first+static_cast<unsigned>(value.size()),first+static_cast<unsigned>(value.size()));
        report(result);
    }
    unsigned mouse_offset(int x){
        auto* e=editor();auto* i=item(focus);if(!e||!i)return 0;
        return text.shape(e->visible(),d(get_screen().font_size)).hit(std::clamp(x,i->editor.x,i->editor.x+i->editor.w)-i->editor.x+horizontal[focus]);
    }
    void move(int direction,bool extend){
        auto* e=editor();if(!e)return;
        auto layout=text.shape(e->text,d(get_screen().font_size));unsigned pos=e->selection.caret;
        if(!extend && e->begin()!=e->end())pos=direction<0?e->begin():e->end();
        else if(direction<0){auto it=layout.carets.lower_bound(pos);if(it!=layout.carets.begin())pos=std::prev(it)->first;}
        else {auto it=layout.carets.upper_bound(pos);if(it!=layout.carets.end())pos=it->first;}
        e->select(extend?e->selection.anchor:pos,pos);invalidate();
    }
    bool copy(){
        auto* e=editor();if(!e||e->begin()==e->end()||!OpenClipboard(window))return false;
        auto value=e->text.substr(e->begin(),e->end()-e->begin());
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(char16_t));
        if(!memory){CloseClipboard();return false;}
        auto data=GlobalLock(memory);
        if(!data){GlobalFree(memory);CloseClipboard();return false;}
        memcpy(data,value.c_str(),(value.size()+1)*sizeof(char16_t));GlobalUnlock(memory);
        bool ok=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
        if(!ok)GlobalFree(memory);CloseClipboard();return ok;
    }
    void paste(){
        if(!OpenClipboard(window))return;
        auto memory=GetClipboardData(CF_UNICODETEXT);
        if(memory){
            auto data=static_cast<const char16_t*>(GlobalLock(memory));
            if(data){
                auto maximum=std::min<std::size_t>(GlobalSize(memory)/sizeof(char16_t),4097);
                std::size_t n=0;while(n<maximum&&data[n])++n;
                std::u16string value(data,n);GlobalUnlock(memory);CloseClipboard();
                if(n==maximum||!valid_single_line(value)){report({Status::invalid_value,u"請貼上有效單行文字（最多 4096 UTF-16 單位）"});return;}
                replace_selection(value);return;
            }
        }
        CloseClipboard();
    }
    void tab(bool backwards){
        std::vector<Item*> order;
        for(auto& i:items)if(ui.available(i.id))order.push_back(&i);
        if(order.empty())return;
        auto it=std::find_if(order.begin(),order.end(),[&](auto* i){return i->id==focus;});
        int index=it==order.end()?(backwards?0:-1):static_cast<int>(it-order.begin());
        index=(index+(backwards?-1:1)+static_cast<int>(order.size()))%static_cast<int>(order.size());
        auto target=*order[index];set_focus(target.id);
        if(target.in_content){
            if(target.bounds.y<content.y)scroll=std::max(0,scroll+target.bounds.y-content.y);
            else if(target.bounds.y+target.bounds.h>content.y+content.h)scroll=std::min(max_scroll,scroll+target.bounds.y+target.bounds.h-content.y-content.h);
        }
    }
    LRESULT message(UINT msg,WPARAM w,LPARAM l){
        switch(msg){
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(window,&ps);try{if(renderer)draw();}catch(...){EndPaint(window,&ps);throw;}EndPaint(window,&ps);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:case WM_MOVE:invalidate();return 0;
        case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);invalidate();return 0;}
        case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={d(960),d(660)};return 0;}
        case WM_SETFOCUS:invalidate();return 0;
        case WM_KILLFOCUS:cancel_composition();end_drag();DestroyCaret();caret_height=0;invalidate();return 0;
        case WM_CLOSE:
            if(ui.stopped()){PostQuitMessage(1);return 0;}
            if(ui.modal()) {activate("dialog.cancel");return 0;}
            if(get_screen().dirty){closing=true;activate("discard");return 0;}
            PostQuitMessage(0);return 0;
        case WM_MOUSEWHEEL:cancel_composition();scroll=std::clamp(scroll-GET_WHEEL_DELTA_WPARAM(w)*d(44)/WHEEL_DELTA,0,max_scroll);invalidate();return 0;
        case WM_LBUTTONDOWN:{
            if(editor()&&editor()->composing)return 0;
            SetFocus(window);pending_high=0;pressed.clear();
            for(auto it=items.rbegin();it!=items.rend();++it)if(it->bounds.contains(GET_X_LPARAM(l),GET_Y_LPARAM(l))&&
                (!it->in_content||content.contains(GET_X_LPARAM(l),GET_Y_LPARAM(l)))&&ui.available(it->id)){
                set_focus(it->id);
                if(auto e=editor()){auto pos=mouse_offset(GET_X_LPARAM(l));e->select(w&MK_SHIFT?e->selection.anchor:pos,pos);dragging=true;SetCapture(window);}
                else {pressed=it->id;SetCapture(window);}
                invalidate();break;
            }
            return 0;
        }
        case WM_MOUSEMOVE:{
            if(dragging){
                if(!(w&MK_LBUTTON)){end_drag();return 0;}
                if(auto e=editor())e->select(e->selection.anchor,mouse_offset(GET_X_LPARAM(l)));invalidate();return 0;
            }
            Id next;
            for(auto& i:items)if(i.bounds.contains(GET_X_LPARAM(l),GET_Y_LPARAM(l))&&(!i.in_content||content.contains(GET_X_LPARAM(l),GET_Y_LPARAM(l)))&&ui.available(i.id))next=i.id;
            if(next!=hover){hover=next;invalidate();}
            return 0;
        }
        case WM_LBUTTONUP:{
            if(dragging){if(auto e=editor())e->select(e->selection.anchor,mouse_offset(GET_X_LPARAM(l)));end_drag();invalidate();}
            else if(!pressed.empty()){auto id=pressed;pressed.clear();ReleaseCapture();auto i=item(id);if(i&&i->bounds.contains(GET_X_LPARAM(l),GET_Y_LPARAM(l)))activate(id);}
            return 0;
        }
        case WM_CAPTURECHANGED:dragging=false;pressed.clear();pending_high=0;return 0;
        case WM_CANCELMODE:end_drag();pressed.clear();pending_high=0;cancel_composition();return 0;
        case WM_IME_SETCONTEXT:return DefWindowProcW(window,msg,w,l&~ISC_SHOWUICOMPOSITIONWINDOW);
        case WM_IME_STARTCOMPOSITION:
            if(auto e=editor();e&&ui.available(focus)&&!ui.find(focus)->read_only){end_drag();pending_high=0;e->start_composition();invalidate();return 0;}break;
        case WM_IME_COMPOSITION:{
            auto e=editor();if(!e||!ui.available(focus))return 0;
            auto imc=ImmGetContext(window);if(!imc)return 0;
            if(l&GCS_RESULTSTR){auto value=composition(imc,GCS_RESULTSTR);e->cancel_composition();replace_selection(value);}
            if(l&GCS_COMPSTR){auto value=composition(imc,GCS_COMPSTR);auto caret=ImmGetCompositionStringW(imc,GCS_CURSORPOS,nullptr,0);e->update_composition(value,static_cast<unsigned>(std::max(0L,caret)));}
            ImmReleaseContext(window,imc);invalidate();return 0;
        }
        case WM_IME_ENDCOMPOSITION:if(auto e=editor())e->cancel_composition();invalidate();return 0;
        case WM_IME_CHAR:return 0;
        case WM_IME_REQUEST:
            if(w==IMR_QUERYCHARPOSITION&&l){
                auto e=editor();auto i=item(focus);auto info=reinterpret_cast<IMECHARPOSITION*>(l);
                if(!e||!i||info->dwSize<sizeof(IMECHARPOSITION))return 0;
                auto layout=text.shape(e->visible(),d(get_screen().font_size));
                auto offset=e->composing?e->begin()+std::min(info->dwCharPos,static_cast<DWORD>(e->preedit.size())):e->selection.caret;
                info->pt={i->editor.x+static_cast<LONG>(layout.at(offset)-horizontal[focus]),i->editor.y};ClientToScreen(window,&info->pt);
                info->cLineHeight=i->editor.h;RECT r{};GetWindowRect(window,&r);info->rcDocument=r;return 1;
            }break;
        case WM_KEYDOWN:{
            bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            auto e=editor();
            if(e&&e->composing){if(w==VK_ESCAPE){cancel_composition();invalidate();return 0;}break;}
            pending_high=0;
            if(w==VK_TAB){tab(shift);return 0;}
            if(w==VK_ESCAPE){if(ui.modal())activate("dialog.cancel");else if(!ui.find("search")->value.empty()){report(ui.set_value("search",u""));scroll=0;}return 0;}
            if(ctrl&&w=='S'){activate("save");return 0;}
            if(ctrl&&w=='Z'){activate(shift?"redo":"undo");return 0;}
            if(ctrl&&w=='Y'){activate("redo");return 0;}
            if((w==VK_RETURN||w==VK_SPACE)&&!e){activate(focus);return 0;}
            if(w==VK_RETURN&&e){activate("save");return 0;}
            if(!e||!ui.available(focus))break;
            if(ctrl&&w=='A')e->select(0,static_cast<unsigned>(e->text.size()));
            else if(ctrl&&w=='C')copy();
            else if(ctrl&&w=='X'){if(copy())replace_selection(u"");}
            else if(ctrl&&w=='V')paste();
            else if(w==VK_LEFT)move(-1,shift);
            else if(w==VK_RIGHT)move(1,shift);
            else if(w==VK_HOME)e->select(shift?e->selection.anchor:0,0);
            else if(w==VK_END)e->select(shift?e->selection.anchor:static_cast<unsigned>(e->text.size()),static_cast<unsigned>(e->text.size()));
            else if(w==VK_BACK||w==VK_DELETE){if(e->begin()==e->end())move(w==VK_BACK?-1:1,true);if(e->begin()!=e->end())replace_selection(u"");}
            else break;
            invalidate();return 0;
        }
        case WM_CHAR:{
            auto e=editor();if(!e||e->composing||!ui.available(focus)||w<32||w==127)return 0;
            auto ch=static_cast<char16_t>(w);
            if(ch>=0xd800&&ch<=0xdbff){pending_high=ch;return 0;}
            if(ch>=0xdc00&&ch<=0xdfff){if(pending_high){std::u16string pair{pending_high,ch};pending_high=0;replace_selection(pair);}return 0;}
            pending_high=0;replace_selection(std::u16string(1,ch));return 0;
        }
        }
        return DefWindowProcW(window,msg,w,l);
    }
};
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto host=reinterpret_cast<Host*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){host=static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);host->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(host));}
    if(!host)return DefWindowProcW(window,message,w,l);
    try{return host->message(message,w,l);}
    catch(const std::exception& e){std::cerr<<"Desktop error: "<<e.what()<<'\n';host->failed=true;PostQuitMessage(1);return 0;}
    catch(...){host->failed=true;PostQuitMessage(1);return 0;}
}
}
int run_desktop(Ui& ui,const Appearance& appearance,std::function<Screen()> screen,DesktopOptions options){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Host host(ui,appearance,std::move(screen));host.test_mode=options.smoke_test;
    WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LcarsUI.Mvp";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Window class registration failed");
    auto window=CreateWindowExW(options.smoke_test?WS_EX_NOACTIVATE:0,wc.lpszClassName,L"LcarsUI — 設定管理 MVP",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1200,960,nullptr,nullptr,wc.hInstance,&host);
    if(!window)throw std::runtime_error("Window creation failed");
    struct Cleanup {
        Host& host;
        ~Cleanup(){host.cancel_composition();host.end_drag();SetWindowLongPtrW(host.window,GWLP_USERDATA,0);host.renderer.reset();DestroyWindow(host.window);}
    } cleanup{host};
    host.renderer=std::make_unique<Renderer>(window);
    host.scale=GetDpiForWindow(window)/96.f;
    RECT wanted{0,0,host.d(1040),host.d(820)};
    AdjustWindowRectExForDpi(&wanted,WS_OVERLAPPEDWINDOW,FALSE,0,GetDpiForWindow(window));
    SetWindowPos(window,nullptr,0,0,wanted.right-wanted.left,wanted.bottom-wanted.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    if(options.smoke_test){
        auto evidence=[&](const char* suffix){
            if(options.screenshot.empty()){host.draw();return;}
            auto path=options.screenshot.parent_path()/(options.screenshot.stem().string()+suffix+".bmp");
            host.draw(path);
        };
        host.draw(options.screenshot);
        auto e=ui.edit("profile.name");auto original=e->text;
        host.focus="profile.name";e->select(0,static_cast<unsigned>(e->text.size()));
        host.message(WM_CHAR,u'測',0);host.message(WM_CHAR,u'試',0);
        if(e->text!=u"測試")throw std::runtime_error("WM_CHAR must update the semantic value");
        host.activate("undo");host.activate("undo");
        if(e->text!=original)throw std::runtime_error("Keyboard edits must share application undo");
        ui.invoke("reset");evidence("-dialog");
        if(ui.set_value("profile.name",u"blocked").status!=Status::blocked_by_modal)throw std::runtime_error("Modal must block semantic writes");
        host.activate("dialog.cancel");
        ui.invoke("nav.preferences");evidence("-preferences");
        ui.invoke("preferences.larger");ui.invoke("preferences.font");evidence("-font");
        ui.invoke("undo");ui.invoke("undo");
        ui.invoke("nav.general");host.draw();
        ui.set_value("search",u"字型");evidence("-search");
        if(!host.item("preferences.font")||host.item("profile.name"))throw std::runtime_error("Search must filter logical controls across pages");
        ui.set_value("search",u"");host.draw();
        host.focus="profile.name";e->select(0,static_cast<unsigned>(e->text.size()));
        host.message(WM_CHAR,0xd840,0);host.message(WM_CHAR,0xdc00,0);
        if(e->text!=u"\U00020000")throw std::runtime_error("Surrogate pair must be committed together");
        host.activate("undo");
        if(e->text!=original)throw std::runtime_error("Surrogate pair must be one undo step");
        e->select(0,0);host.message(WM_CHAR,0xd840,0);host.message(WM_KEYDOWN,VK_END,0);host.message(WM_CHAR,0xdc00,0);
        if(e->text!=original)throw std::runtime_error("Navigation must invalidate pending surrogate");
        SetWindowPos(window,nullptr,0,0,host.d(960),host.d(660),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        evidence("-compact");
        std::cout<<"PASS desktop input, undo, modal, navigation, font and size smoke\n";
        return host.failed?1:0;
    }
    ShowWindow(window,SW_SHOW);SetFocus(window);
    if(!options.screenshot.empty())host.draw(options.screenshot);
    MSG message{};BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
    return result<0||host.failed?1:0;
}
}
