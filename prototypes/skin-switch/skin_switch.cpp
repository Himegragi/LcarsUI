// THROWAWAY PROTOTYPE: switch two already-compiled TextField structures (A/B) in one process while the logical
// page state (field ids, editing models, focus, undo history) stays untouched. Not a framework, template engine or
// a decision to ship runtime skin switching. No layout cache and no glyph atlas: every structural switch reshapes
// every realized node with HarfBuzz and every frame rasterizes visible nodes with FreeType on the CPU.
#define LCARS_TEXT_HELPERS_ONLY
#include "../text-pipeline/text_probe.cpp"
#include "../text-pipeline/editing_model.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>

// ---------- logical page state (never touched by a skin switch) ----------
struct Field { int id; std::u16string label; editing::Model model; };
struct Page { std::vector<Field> fields; int focused=0; unsigned revision=0; };
struct ModelState {
    std::u16string text,preedit; unsigned anchor,caret,preedit_caret; bool composing;
    bool operator==(const ModelState&) const=default;
};
static ModelState state_of(const editing::Model& m){return {m.state.text,m.preedit,m.state.anchor,m.state.caret,m.preedit_caret,m.composing};}
struct Snapshot {
    std::vector<int> ids; std::vector<const editing::Model*> identities; std::vector<ModelState> models; int focused; unsigned revision;
    bool operator==(const Snapshot&) const=default;
};
static Snapshot snapshot(const Page& p){
    Snapshot s{{},{},{},p.focused,p.revision};
    for(auto& f:p.fields){s.ids.push_back(f.id);s.identities.push_back(&f.model);s.models.push_back(state_of(f.model));}
    return s;
}
static std::u16string u16(const std::wstring& w){return std::u16string(w.begin(),w.end());}
static std::u16string expected_label(int i){return u"欄位 "+u16(std::to_wstring(i));}
static std::u16string expected_text(int i){return u"繁體新中文 Field ABC "+u16(std::to_wstring(i));}
static Page make_page(int count){
    Page p;
    for(int i=0;i<count;++i){
        const std::wstring number=std::to_wstring(i);
        Field f{1000+i,u"欄位 "+u16(number),{}};
        f.model.state.text=u"繁體中文 Field ABC "+u16(number);
        f.model.select(2,2);f.model.replace(u"新");     // undo record
        if(i%3==0){f.model.select(0,0);f.model.replace(u"X");f.model.undo();}  // leaves a known redo record
        f.model.select(1,4);                              // a non-empty selection
        p.fields.push_back(std::move(f));
    }
    if(count>1){p.fields[1].model.start();p.fields[1].model.update(u"測試",1);}  // model preedit fixture, not an IME
    p.focused=std::min(3,count-1);
    // Fixture content is asserted before any timing.
    for(int i=0;i<count;++i){
        auto& m=p.fields[i].model;
        require(p.fields[i].label==expected_label(i),"fixture label mismatch");
        require(m.state.text==expected_text(i)&&m.state.anchor==1&&m.state.caret==4,"fixture text/selection mismatch");
    }
    return p;
}
// Drain full undo then full redo on COPIES and record every state (selection, preedit, preedit cursor, composing).
static std::vector<ModelState> history_trace(editing::Model m){
    std::vector<ModelState> t{state_of(m)};
    for(int k=0;k<16&&m.undo();++k)t.push_back(state_of(m));
    t.push_back({u"--redo--",{},0,0,0,false});
    for(int k=0;k<16&&m.redo();++k)t.push_back(state_of(m));
    return t;
}
static std::vector<std::vector<ModelState>> history_of(const Page& p){std::vector<std::vector<ModelState>> h;for(auto& f:p.fields)h.push_back(history_trace(f.model));return h;}

// ---------- visual tree (the only thing a structural switch rebuilds) ----------
// Adornment is a decorative trailing glyph only (no click handler, not a clear button).
enum class Role{Container,Label,Outline,Underline,Edit,Icon,Adornment};
struct Node { Role role; int field; RECT box; std::wstring text; Layout layout; bool shaped=false; };
struct Tokens { unsigned char bg[3],label[3],text[3],line[3],accent[3],selection[3],focus[3]; };
static const Tokens token_sets[2]={
    {{18,25,38},{150,170,195},{235,235,235},{70,95,125},{96,219,200},{42,78,123},{240,180,60}},
    {{34,22,30},{210,160,180},{250,240,235},{140,80,100},{255,140,90},{110,50,70},{120,200,255}}};
static constexpr int glyph_px=18,row_a=70,row_b=44,margin=16;
static int row_height(int skin){return skin==0?row_a:row_b;}
// Skin A: Container > Label (above) > Outline > Edit.   Skin B: Container > Icon > Label (beside) > Edit > Underline > Adornment.
static void build(int skin,const Page& p,int width,std::vector<Node>& out){
    out.clear();out.reserve(p.fields.size()*(skin==0?4:6));
    for(size_t i=0;i<p.fields.size();++i){
        const int f=static_cast<int>(i),top=margin+f*row_height(skin),right=width-margin;
        std::wstring label(p.fields[i].label.begin(),p.fields[i].label.end());
        out.push_back({Role::Container,f,{margin,top,right,top+row_height(skin)-6},L""});
        if(skin==0){
            out.push_back({Role::Label,f,{margin+4,top,right,top+22},label});
            out.push_back({Role::Outline,f,{margin,top+24,right,top+62},L""});
            out.push_back({Role::Edit,f,{margin+10,top+28,right-10,top+58},L""});
        }else{
            out.push_back({Role::Icon,f,{margin,top+8,margin+20,top+28},L""});
            out.push_back({Role::Label,f,{margin+28,top+6,margin+150,top+30},label});
            out.push_back({Role::Edit,f,{margin+156,top+4,right-40,top+34},L""});
            out.push_back({Role::Underline,f,{margin+156,top+35,right-40,top+37},L""});
            out.push_back({Role::Adornment,f,{right-34,top+6,right,top+32},L"×"});
        }
    }
}
static unsigned shape_all(TextEngine& e,const Page& p,std::vector<Node>& nodes){
    unsigned glyphs=0;
    for(auto& n:nodes){
        if(n.role==Role::Edit){auto v=p.fields[n.field].model.visible();n.text.assign(v.begin(),v.end());}
        if(n.role==Role::Label||n.role==Role::Edit||n.role==Role::Adornment){n.layout=e.shape(n.text,glyph_px);n.shaped=true;glyphs+=static_cast<unsigned>(n.layout.glyphs.size());}
    }
    return glyphs;
}
struct Raster { unsigned nodes=0,glyphs=0; };
static Raster raster(TextEngine& e,const Page& p,const std::vector<Node>& nodes,const Tokens& t,int w,int h,std::vector<unsigned char>& image){
    image.assign(static_cast<size_t>(w)*h*4,255);
    auto fill=[&](int x0,int y0,int x1,int y1,const unsigned char* c){
        for(int y=std::max(y0,0);y<std::min(y1,h);++y)for(int x=std::max(x0,0);x<std::min(x1,w);++x){size_t q=(static_cast<size_t>(h-1-y)*w+x)*4;image[q]=c[0];image[q+1]=c[1];image[q+2]=c[2];}};
    fill(0,0,w,h,t.bg);
    e.latin->size(glyph_px);e.traditional->size(glyph_px);
    Raster r;
    auto text=[&](const Node& n,const unsigned char* c){
        const int baseline=n.box.top+glyph_px+2;
        for(const auto& g:n.layout.glyphs){
            auto& font=e.font(g.font);
            require(FT_Load_Glyph(font.face,g.id,FT_LOAD_DEFAULT)==0&&FT_Render_Glyph(font.face->glyph,FT_RENDER_MODE_NORMAL)==0,"glyph raster failed");
            const auto& b=font.face->glyph->bitmap;++r.glyphs;
            const int gx=n.box.left+static_cast<int>(std::round(g.x+g.dx))+font.face->glyph->bitmap_left,gy=baseline-static_cast<int>(std::round(g.dy))-font.face->glyph->bitmap_top;
            for(unsigned y=0;y<b.rows;++y)for(unsigned x=0;x<b.width;++x){
                int xx=gx+static_cast<int>(x),yy=gy+static_cast<int>(y);if(xx<0||yy<0||xx>=w||yy>=h||xx>=n.box.right)continue;
                unsigned a=b.pixel_mode==FT_PIXEL_MODE_GRAY?b.buffer[y*std::abs(b.pitch)+x]:((b.buffer[y*std::abs(b.pitch)+x/8]&(0x80>>(x%8)))?255:0);
                size_t q=(static_cast<size_t>(h-1-yy)*w+xx)*4;for(int k=0;k<3;++k)image[q+k]=static_cast<unsigned char>((c[k]*a+image[q+k]*(255-a))/255);
            }
        }
    };
    for(const auto& n:nodes){
        if(n.box.bottom<0||n.box.top>=h)continue;  // only rows inside the fixed viewport are rasterized
        ++r.nodes;const auto& m=p.fields[n.field].model;const bool focused=n.field==p.focused;
        switch(n.role){
        case Role::Container:break;
        case Role::Label:text(n,t.label);break;
        case Role::Outline:{auto c=focused?t.focus:t.line;fill(n.box.left,n.box.top,n.box.right,n.box.top+2,c);fill(n.box.left,n.box.bottom-2,n.box.right,n.box.bottom,c);
            fill(n.box.left,n.box.top,n.box.left+2,n.box.bottom,c);fill(n.box.right-2,n.box.top,n.box.right,n.box.bottom,c);break;}
        case Role::Underline:fill(n.box.left,n.box.top,n.box.right,n.box.bottom,focused?t.focus:t.line);break;
        case Role::Icon:fill(n.box.left+4,n.box.top+4,n.box.right-4,n.box.bottom-4,t.accent);break;
        case Role::Adornment:text(n,t.label);break;
        case Role::Edit:{
            // Positions clipped to the Edit rect (no scrolling in this prototype).
            auto x=[&](unsigned o){auto it=n.layout.carets.upper_bound(o);float v=it==n.layout.carets.begin()?0.f:std::prev(it)->second;
                return std::clamp(static_cast<int>(n.box.left)+static_cast<int>(v),static_cast<int>(n.box.left),static_cast<int>(n.box.right));};
            unsigned b=m.begin(),en=m.composing?m.begin()+(unsigned)m.preedit.size():m.end();
            if(b!=en)fill(x(b),n.box.top+2,x(en),n.box.bottom-2,t.selection);
            text(n,t.text);
            if(m.composing)fill(x(b),n.box.bottom-3,x(en),n.box.bottom-1,t.accent);
            if(focused){int cx=std::min(x(m.visible_caret()),static_cast<int>(n.box.right)-2);fill(cx,n.box.top+3,cx+2,n.box.bottom-3,t.accent);}
            break;}
        }
    }
    return r;
}

// ---------- staged frame with CPU wall-clock timing (not scanout/GPU latency) ----------
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct Sample { std::string kind;int iter;size_t destroyed,built,shaped_nodes;unsigned glyphs_shaped;Raster r;double build,shape,raster,upload,swap,total; };
struct App {
    Context& c;Scene& scene;TextEngine& e;Page page;int skin=0,tokens=0;std::vector<Node> nodes;std::vector<unsigned char> image;int w=0,h=0;
    Sample frame(bool structural,int next_skin,int next_tokens,const char* kind,int iter,bool present=true){
        Sample s{kind,iter};RECT rc{};GetClientRect(c.window,&rc);w=rc.right;h=rc.bottom;
        auto t0=Clock::now();
        if(structural){s.destroyed=nodes.size();std::vector<Node>().swap(nodes);skin=next_skin;build(skin,page,w,nodes);s.built=nodes.size();}
        tokens=next_tokens;
        auto t1=Clock::now();
        if(structural){s.glyphs_shaped=shape_all(e,page,nodes);for(auto& n:nodes)s.shaped_nodes+=n.shaped;}
        auto t2=Clock::now();
        s.r=raster(e,page,nodes,token_sets[tokens],w,h,image);
        auto t3=Clock::now();
        auto& g=c.gl;g.Viewport(0,0,w,h);g.Disable(GL_SCISSOR_TEST);g.Disable(GL_BLEND);
        g.BindTexture(GL_TEXTURE_2D,scene.texture);g.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        g.UseProgram(scene.program);g.BindVertexArray(scene.vao);g.DrawArrays(GL_TRIANGLES,0,3);
        auto t4=Clock::now();
        if(present)require(SwapBuffers(c.dc)!=FALSE,"SwapBuffers failed");
        auto t5=Clock::now();
        s.build=ms(t0,t1);s.shape=ms(t1,t2);s.raster=ms(t2,t3);s.upload=ms(t3,t4);s.swap=ms(t4,t5);s.total=ms(t0,t5);
        return s;
    }
};
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static double pct(std::vector<double> v,double p){std::sort(v.begin(),v.end());return v[std::min(v.size()-1,static_cast<size_t>(p*(v.size()-1)+0.5))];}

static int benchmark(Context& c,Scene& scene,TextEngine& e){
    std::ofstream csv("skin-switch-samples-v2.csv",std::ios::binary),log("skin-switch-summary-v2.log",std::ios::binary);
    csv<<"fields,kind,iter,nodes_destroyed,nodes_built,shaped_nodes,glyphs_shaped,raster_nodes,raster_glyphs,destroy_build_place_ms,hb_shape_ms,cpu_raster_ms,upload_submit_ms,swapbuffers_ms,total_cpu_ms\n";
    log<<"THROWAWAY skin-switch benchmark v2 (fixed fixture). CPU wall time via steady_clock; no glFinish/readback in timed path; not GPU/scanout latency.\n";
    log<<"Stages: destroy old nodes + build/place new nodes | HarfBuzz shaping of label/edit/adornment text | CPU FreeType raster of visible nodes | texture upload + draw submit | SwapBuffers call. Swap interval not set or measured.\n";
    log<<"GL_RENDERER="<<c.gl.GetString(GL_RENDERER)<<" client="<<[&]{RECT r{};GetClientRect(c.window,&r);return std::to_string(r.right)+"x"+std::to_string(r.bottom);}()<<" dpi="<<GetDpiForWindow(c.window)<<"\n";
    for(int count:{24,128,512}){
        App app{c,scene,e,make_page(count)};
        const Snapshot base=snapshot(app.page);const auto base_history=history_of(app.page);
        // The fixture must actually contain undo records, redo records (every 3rd field) and a composing field.
        for(size_t i=0;i<base_history.size();++i){
            auto marker=std::find_if(base_history[i].begin(),base_history[i].end(),[](const ModelState& m){return m.text==u"--redo--";});
            const auto undo_steps=marker-base_history[i].begin()-1,redo_steps=base_history[i].end()-marker-1;
            require(undo_steps>=1&&(i%3!=0||redo_steps>=1),"fixture lacks expected undo/redo records");
        }
        require(count<2||app.page.fields[1].model.composing,"fixture lacks composing field");
        app.frame(true,0,0,"init",0);pump();  // initialization frame (fonts/GL already created) - not reported
        std::vector<Sample> samples;
        auto check=[&](const Sample& s){
            if(!(snapshot(app.page)==base))throw std::runtime_error("logical state/identity changed after "+s.kind);
            if(history_of(app.page)!=base_history)throw std::runtime_error("undo/redo history changed after "+s.kind);};
        auto run=[&](bool structural,int sk,int tk,const char* kind,int i){auto s=app.frame(structural,sk,tk,kind,i);pump();check(s);samples.push_back(s);};
        run(true,1,0,"first_structural_A_to_B",0);
        for(int i=0;i<30;++i){run(true,0,0,"structural_B_to_A",i);run(true,1,0,"structural_A_to_B",i);}
        run(true,0,0,"normalize_B_to_A_unreported",0);  // returns to A for the token comparator; in CSV, excluded from summary
        run(false,0,1,"first_tokens_only",0);
        for(int i=0;i<30;++i){run(false,0,0,"tokens_only",i*2);run(false,0,1,"tokens_only",i*2+1);}
        for(auto& s:samples)csv<<count<<','<<s.kind<<','<<s.iter<<','<<s.destroyed<<','<<s.built<<','<<s.shaped_nodes<<','<<s.glyphs_shaped<<','<<s.r.nodes<<','<<s.r.glyphs<<','
            <<s.build<<','<<s.shape<<','<<s.raster<<','<<s.upload<<','<<s.swap<<','<<s.total<<'\n';
        log<<"\nfields="<<count<<" fixture=asserted(labels/text/selection) state_check=PASS(all "<<samples.size()<<" switches: ids, model identities, text/selection/preedit/preedit cursor/composing, focus, revision equal; full undo+redo drains on copies equal)\n";
        for(const char* kind:{"first_structural_A_to_B","structural_A_to_B","structural_B_to_A","first_tokens_only","tokens_only"}){
            std::vector<const Sample*> k;for(auto& s:samples)if(s.kind==kind)k.push_back(&s);
            auto col=[&](double Sample::*m){std::vector<double> v;for(auto p:k)v.push_back(p->*m);std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<pct(v,.5)<<'/'<<pct(v,.95)<<'/'<<*std::max_element(v.begin(),v.end());return o.str();};
            log<<"  "<<kind<<" n="<<k.size()<<" destroyed="<<k[0]->destroyed<<" built="<<k[0]->built<<" shaped_nodes="<<k[0]->shaped_nodes<<" glyphs_shaped="<<k[0]->glyphs_shaped
               <<" raster_nodes="<<k[0]->r.nodes<<" raster_glyphs="<<k[0]->r.glyphs<<"\n    p50/p95/max ms: destroy+build+place "<<col(&Sample::build)<<" | HB shaping "<<col(&Sample::shape)
               <<" | cpu raster "<<col(&Sample::raster)<<" | upload+submit "<<col(&Sample::upload)<<" | SwapBuffers "<<col(&Sample::swap)<<" | total "<<col(&Sample::total)<<"\n";
        }
        size_t glyphs=0,glyph_capacity=0,carets=0;for(auto& n:app.nodes){glyphs+=n.layout.glyphs.size();glyph_capacity+=n.layout.glyphs.capacity();carets+=n.layout.carets.size();}
        log<<"  element counts (not a memory footprint): nodes="<<app.nodes.size()<<" node_capacity="<<app.nodes.capacity()<<" glyphs="<<glyphs<<" glyph_capacity="<<glyph_capacity
           <<" caret_entries="<<carets<<" frame_image_bytes="<<app.image.size()<<"\n";
        if(count==24){
            // Untimed witness captures, outside measurement.
            auto read=reinterpret_cast<PFNGLREADBUFFERPROC>(symbol("glReadBuffer"));
            for(int sk:{0,1}){app.frame(true,sk,0,"witness",0,false);c.gl.Finish();read(GL_BACK);capture(c.gl,app.w,app.h,sk?"skin-B-v2.bmp":"skin-A-v2.bmp");require(SwapBuffers(c.dc)!=FALSE,"swap");pump();}
            if(!(snapshot(app.page)==base)||history_of(app.page)!=base_history)throw std::runtime_error("state changed after witness");
        }
    }
    log<<"\nNo layout cache and no glyph atlas implemented. Visible rows only are rasterized; all realized fields are shaped on structural switch.\n";
    std::cout<<"PASS benchmark written to skin-switch-samples-v2.csv / skin-switch-summary-v2.log\n";return 0;
}

// ---------- interactive (launched by a person; limited editing: BMP characters, no IME) ----------
static App* live=nullptr;static std::string last_report="init";
static void retitle(){std::wostringstream t;t<<L"THROWAWAY skin switch | skin "<<(live->skin?L"B":L"A")<<L" tokens "<<live->tokens<<L" | F5 structure F6 tokens Tab focus | "<<std::wstring(last_report.begin(),last_report.end());SetWindowTextW(live->c.window,t.str().c_str());}
static void report(const Sample& s,const Snapshot& before){
    std::ostringstream o;o<<s.kind<<" "<<std::fixed<<std::setprecision(2)<<s.total<<"ms (destroy+build+place "<<s.build<<" HB "<<s.shape<<" raster "<<s.raster<<") state "<<(snapshot(live->page)==before?"preserved":"CHANGED");
    last_report=o.str();retitle();
}
static LRESULT CALLBACK skin_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(!live)return wnd_proc(h,m,w,l);
    auto& p=live->page;auto& model=p.fields[p.focused].model;
    auto edited=[&]{++p.revision;live->frame(true,live->skin,live->tokens,"edit",0);retitle();};
    if(m==WM_KEYDOWN){
        bool ctrl=GetKeyState(VK_CONTROL)&0x8000,shift=GetKeyState(VK_SHIFT)&0x8000;
        if(w==VK_F5){auto b=snapshot(p);report(live->frame(true,1-live->skin,live->tokens,"structural",0),b);return 0;}
        if(w==VK_F6){auto b=snapshot(p);report(live->frame(false,live->skin,1-live->tokens,"tokens_only",0),b);return 0;}
        if(w==VK_TAB){p.focused=(p.focused+1)%static_cast<int>(p.fields.size());live->frame(false,live->skin,live->tokens,"focus",0);return 0;}
        if(ctrl&&w=='Z'){model.undo();edited();return 0;}if(ctrl&&w=='Y'){model.redo();edited();return 0;}
        if(w==VK_LEFT||w==VK_RIGHT){unsigned c=model.state.caret;if(w==VK_LEFT&&c)--c;if(w==VK_RIGHT&&c<model.state.text.size())++c;model.select(shift?model.state.anchor:c,c);edited();return 0;}
        if(w==VK_BACK){if(model.begin()==model.end()&&model.state.caret)model.select(model.state.caret-1,model.state.caret);model.replace(u"");edited();return 0;}
    }
    if(m==WM_CHAR&&w>=32&&w!=127&&!(w>=0xd800&&w<=0xdfff)){model.replace(std::u16string(1,static_cast<char16_t>(w)));edited();return 0;}
    if(m==WM_LBUTTONDOWN){
        int x=static_cast<short>(LOWORD(l)),y=static_cast<short>(HIWORD(l));
        for(auto& n:live->nodes)if(n.role==Role::Edit&&x>=n.box.left&&x<n.box.right&&y>=n.box.top&&y<n.box.bottom){
            p.focused=n.field;auto o=live->e.hit(n.layout,static_cast<float>(x-n.box.left));p.fields[n.field].model.select(o,o);edited();return 0;}
    }
    // Interactive only (live is null during the benchmark): repaint on exposure, re-place nodes on resize.
    if(m==WM_PAINT){ValidateRect(h,nullptr);live->frame(false,live->skin,live->tokens,"paint",0);return 0;}
    if(m==WM_SIZE&&LOWORD(l)>0&&HIWORD(l)>0){live->frame(true,live->skin,live->tokens,"resize",0);return 0;}
    return wnd_proc(h,m,w,l);
}
// Clears the raw window-procedure pointer before the stack App is destroyed, including on exceptions.
struct LiveBinding {
    explicit LiveBinding(App& app){live=&app;}
    ~LiveBinding(){live=nullptr;}
    LiveBinding(const LiveBinding&)=delete;LiveBinding& operator=(const LiveBinding&)=delete;
};

int main(int argc,char** argv){
    try{
        std::cout<<std::unitbuf;bool interactive=false,bench=false;
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--interactive")interactive=true;if(a=="--benchmark")bench=true;}
        require(interactive!=bench,"use exactly one of --interactive / --benchmark");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=skin_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LcarsBuildProbe";wc.hCursor=LoadCursorW(nullptr,IDC_IBEAM);RegisterClassW(&wc);
        TextEngine e;Context c;c.open(interactive);  // benchmark window: WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE, no SetFocus
        int code=0;
        {
            // Scene (GL objects) is destroyed while the GL context is still current, before c.release().
            Scene scene(c.gl);scene.init();
            if(bench)code=benchmark(c,scene,e);
            else{
                App app{c,scene,e,make_page(24)};LiveBinding binding(app);app.frame(true,0,0,"init",0);retitle();
                while(!quitting){pump();Sleep(5);}
            }
        }
        require(c.release(),"WGL cleanup failed");return code;
    }catch(const std::exception& x){live=nullptr;std::cerr<<"FAIL "<<x.what()<<'\n';return 1;}
}
