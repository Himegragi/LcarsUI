// Reuse the already-tested GL bootstrap only; this is disposable probe composition.
#define main minimal_build_entry_not_used
#include "../minimal-build/gl_probe.cpp"
#undef main
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <algorithm>
#include <map>
#include <memory>
#include <sstream>

struct Font {
    FT_Face face{}; hb_font_t* hb{};
    Font(FT_Library lib,const char* path) {
        require(FT_New_Face(lib,path,0,&face)==0,"System font unavailable");
        hb=hb_ft_font_create_referenced(face);
    }
    ~Font(){hb_font_destroy(hb); FT_Done_Face(face);}
    void size(int pixels){require(FT_Set_Pixel_Sizes(face,0,pixels)==0,"Font size failed");hb_ft_font_changed(hb);}
};
struct Glyph { unsigned font,id,cluster; float x,dx,dy,advance; };
struct Layout {
    std::vector<Glyph> glyphs;
    std::map<unsigned,float> carets;
    float width=0; unsigned missing=0,fallback=0;
};
struct TextEngine {
    FT_Library library{};
    std::unique_ptr<Font> latin,traditional;
    bool ming=false;
    TextEngine(){require(FT_Init_FreeType(&library)==0,"FreeType initialization failed");reload(false);}
    ~TextEngine(){traditional.reset();latin.reset();FT_Done_FreeType(library);}
    void reload(bool useMing){
        ming=useMing;
        latin=std::make_unique<Font>(library,"C:/Windows/Fonts/segoeui.ttf");
        traditional=std::make_unique<Font>(library,ming?"C:/Windows/Fonts/mingliu.ttc":"C:/Windows/Fonts/msjh.ttc");
    }
    Font& font(unsigned id){return id?*traditional:*latin;}
    Layout shape(const std::wstring& text,int pixels) {
        latin->size(pixels);traditional->size(pixels);
        struct Run{unsigned begin,end,font;hb_script_t script;};
        std::vector<Run> runs;
        auto unicode=hb_unicode_funcs_get_default();
        for(unsigned i=0;i<text.size();) {
            const unsigned begin=i; unsigned cp=text[i++];
            if(cp>=0xd800 && cp<=0xdbff && i<text.size() && text[i]>=0xdc00 && text[i]<=0xdfff)
                cp=0x10000+((cp-0xd800)<<10)+(text[i++]-0xdc00);
            auto script=hb_unicode_script(unicode,cp);
            unsigned selected=FT_Get_Char_Index(latin->face,cp)?0:1;
            if(script==HB_SCRIPT_INHERITED && !runs.empty() && FT_Get_Char_Index(font(runs.back().font).face,cp)) selected=runs.back().font;
            if(script==HB_SCRIPT_COMMON || script==HB_SCRIPT_INHERITED) script=runs.empty()?HB_SCRIPT_LATIN:runs.back().script;
            if(!runs.empty() && runs.back().font==selected && runs.back().script==script) runs.back().end=i;
            else runs.push_back({begin,i,selected,script});
        }
        Layout result;
        result.carets[0]=0;
        for(const auto& run:runs) {
            auto buffer=hb_buffer_create();
            hb_buffer_set_direction(buffer,HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer,run.script);
            hb_buffer_set_language(buffer,hb_language_from_string("zh-TW",-1));
            hb_buffer_add_utf16(buffer,reinterpret_cast<const uint16_t*>(text.data()),static_cast<int>(text.size()),run.begin,run.end-run.begin);
            hb_shape(font(run.font).hb,buffer,nullptr,0);
            unsigned count{};auto info=hb_buffer_get_glyph_infos(buffer,&count);auto pos=hb_buffer_get_glyph_positions(buffer,&count);
            for(unsigned i=0;i<count;++i) {
                const float advance=pos[i].x_advance/64.f;
                result.carets.try_emplace(info[i].cluster,result.width);
                result.glyphs.push_back({run.font,info[i].codepoint,info[i].cluster,result.width,pos[i].x_offset/64.f,pos[i].y_offset/64.f,advance});
                result.width+=advance;
                result.missing+=info[i].codepoint==0;
                result.fallback+=run.font==1;
            }
            hb_buffer_destroy(buffer);
        }
        result.carets[static_cast<unsigned>(text.size())]=result.width;
        return result;
    }
    unsigned hit(const Layout& layout,float x) {
        unsigned best=0;float distance=1e30f;
        for(auto [offset,position]:layout.carets)if(std::abs(position-x)<distance){distance=std::abs(position-x);best=offset;}
        return best;
    }
};
static HWND editor{},description{};
static int pixel_size=28;
static TextEngine* engine=nullptr;
static std::wstring edit_text();
static LRESULT CALLBACK text_proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_LBUTTONDOWN && static_cast<short>(HIWORD(l))>=120) {
        auto layout=engine->shape(edit_text(),pixel_size);
        auto offset=engine->hit(layout,static_cast<short>(LOWORD(l))-24.f);
        SendMessageW(editor,EM_SETSEL,offset,offset);SetFocus(editor);return 0;
    }
    if(m==WM_COMMAND && HIWORD(w)==BN_CLICKED) {
        if(LOWORD(w)==101)engine->reload(!engine->ming);
        if(LOWORD(w)==102)pixel_size=std::max(12,pixel_size-4);
        if(LOWORD(w)==103)pixel_size=std::min(64,pixel_size+4);
        SetFocus(editor);
        return 0;
    }
    return wnd_proc(h,m,w,l);
}
static std::wstring edit_text(){
    int n=GetWindowTextLengthW(editor);std::wstring t(n+1,L'\0');GetWindowTextW(editor,t.data(),n+1);t.resize(n);return t;
}
static std::vector<unsigned char> paint(TextEngine& e,const Layout& layout,int width,int height,unsigned begin,unsigned end,int size) {
    std::vector<unsigned char> image(static_cast<size_t>(width)*height*4);
    for(size_t i=0;i<image.size();i+=4){image[i]=18;image[i+1]=25;image[i+2]=38;image[i+3]=255;}
    auto fill=[&](int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b){
        for(int yy=std::max(y,0);yy<std::min(y+h,height);++yy)for(int xx=std::max(x,0);xx<std::min(x+w,width);++xx){
            size_t p=(static_cast<size_t>(height-1-yy)*width+xx)*4;image[p]=r;image[p+1]=g;image[p+2]=b;
        }
    };
    const int left=24,baseline=40+size;
    auto nearest=[&](unsigned offset){auto it=layout.carets.upper_bound(offset);if(it==layout.carets.begin())return 0.f;return std::prev(it)->second;};
    if(begin!=end)fill(left+static_cast<int>(nearest(begin)),baseline-size,static_cast<int>(nearest(end)-nearest(begin)),size+10,42,78,123);
    fill(left,baseline+2,std::min(width-left-20,static_cast<int>(layout.width)),1,58,82,108);
    for(const auto& glyph:layout.glyphs) {
        auto& font=e.font(glyph.font);
        require(FT_Load_Glyph(font.face,glyph.id,FT_LOAD_DEFAULT)==0,"Load glyph failed");
        require(FT_Render_Glyph(font.face->glyph,FT_RENDER_MODE_NORMAL)==0,"Rasterize glyph failed");
        const auto& b=font.face->glyph->bitmap;
        const int gx=left+static_cast<int>(std::round(glyph.x+glyph.dx))+font.face->glyph->bitmap_left;
        const int gy=baseline-static_cast<int>(std::round(glyph.dy))-font.face->glyph->bitmap_top;
        for(unsigned y=0;y<b.rows;++y)for(unsigned x=0;x<b.width;++x){
            int xx=gx+static_cast<int>(x),yy=gy+static_cast<int>(y);if(xx<0||yy<0||xx>=width||yy>=height)continue;
            unsigned row=b.pitch>=0?y:b.rows-1-y;
            unsigned alpha=0;
            if(b.pixel_mode==FT_PIXEL_MODE_GRAY)alpha=b.buffer[row*std::abs(b.pitch)+x];
            else if(b.pixel_mode==FT_PIXEL_MODE_MONO)alpha=(b.buffer[row*std::abs(b.pitch)+x/8]&(0x80>>(x%8)))?255:0;
            else throw std::runtime_error("Unsupported fixture glyph pixel format");
            size_t p=(static_cast<size_t>(height-1-yy)*width+xx)*4;
            for(int c=0;c<3;++c){unsigned ink=glyph.id?235:(c==0?255:70);image[p+c]=static_cast<unsigned char>((ink*alpha+image[p+c]*(255-alpha))/255);}
        }
    }
    fill(left+static_cast<int>(nearest(end)),baseline-size,2,size+9,96,219,200);
    return image;
}
static void display(Context& c,Scene& scene,TextEngine& e,const std::wstring& text,unsigned begin,unsigned end) {
    RECT r{};GetClientRect(c.window,&r);const int width=r.right,height=std::max(1L,r.bottom-120);
    auto layout=e.shape(text,pixel_size);auto pixels=paint(e,layout,width,height,begin,end,pixel_size);
    auto& g=c.gl;
    g.Viewport(0,0,width,height);g.Disable(GL_SCISSOR_TEST);g.Disable(GL_BLEND);
    g.BindTexture(GL_TEXTURE_2D,scene.texture);g.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    g.UseProgram(scene.program);g.BindVertexArray(scene.vao);g.DrawArrays(GL_TRIANGLES,0,3);
    g.Finish();require(g.GetError()==GL_NO_ERROR,"Text GL error");
    std::vector<unsigned char> readback(pixels.size());
    g.ReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,readback.data());
    for(size_t i=0;i<pixels.size();++i)if(i%4!=3)
        require(std::abs(int(readback[i])-int(pixels[i]))<=1,"Text GPU readback differs from CPU rasterization");
    std::wostringstream message;message<<L"HB "<<hb_version_string()<<L" | px "<<pixel_size<<L" | glyphs "<<layout.glyphs.size()<<L" | fallback "<<layout.fallback<<L" | missing "<<layout.missing<<L" | UTF16 selection "<<begin<<L":"<<end;
    SetWindowTextW(description,message.str().c_str());
}
static void tests(TextEngine& e){
    auto check=[](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';require(ok,label);};
    std::cout<<"HarfBuzz="<<hb_version_string()<<" FreeType="<<FREETYPE_MAJOR<<'.'<<FREETYPE_MINOR<<'.'<<FREETYPE_PATCH<<'\n';
    auto mixed=e.shape(L"繁體中文，設定 ABC 123",28);
    std::ofstream glyphs("glyph-map.tsv");glyphs<<"family\tglyph\tutf16_cluster\tx\tadvance\n";
    for(const auto& glyph:mixed.glyphs)glyphs<<e.font(glyph.font).face->family_name<<'\t'<<glyph.id<<'\t'<<glyph.cluster<<'\t'<<glyph.x<<'\t'<<glyph.advance<<'\n';
    check(mixed.missing==0 && mixed.fallback>0,"Traditional Chinese fallback and mixed text have no missing glyphs");
    auto latin=e.shape(L"office A\u0301",28);
    check(latin.missing==0,"Latin ligature/combining fixture shapes");
    check(!latin.carets.contains(8),"combining mark is not exposed as separate caret stop");
    for(auto [offset,x]:mixed.carets)check(e.hit(mixed,x)==offset,"layout position/hit-test round trip");
    auto larger=e.shape(L"繁體中文，設定 ABC 123",42);
    check(larger.width>mixed.width*1.3f,"size change triggers new metrics");
    e.reload(true);auto other=e.shape(L"繁體中文，設定 ABC 123",28);
    check(other.missing==0,"alternate Traditional Chinese font renders fixture");
    auto missing=e.shape(L"\U0010ffff",28);
    check(missing.missing>0,"unsupported character produces explicit missing-glyph result");
    auto supplementary=e.shape(L"A\U00020000B",28);
    check(!supplementary.carets.contains(2),"UTF16 surrogate pair is not split into caret stops");
    e.reload(false);
}
int main(int argc,char** argv){
    try{
        std::cout<<std::unitbuf;
        bool interactive=argc>1 && std::string(argv[1])=="--interactive";
        TextEngine text;engine=&text;tests(text);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=wnd_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LcarsBuildProbe";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
        Context c;c.open();SetWindowLongPtrW(c.window,GWL_STYLE,GetWindowLongPtrW(c.window,GWL_STYLE)|WS_CLIPCHILDREN);
        SetWindowLongPtrW(c.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(text_proc));
        SetWindowTextW(c.window,L"LcarsUI TEXT PROTOTYPE | native input above / HarfBuzz + FreeType + OpenGL preview below");
        auto make=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id){return CreateWindowW(cls,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,c.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),wc.hInstance,nullptr);};
        editor=make(L"EDIT",L"繁體中文，設定 ABC 123 office A\u0301",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,18,12,890,32,100);
        make(L"BUTTON",L"切換繁中字型",BS_PUSHBUTTON,18,52,160,28,101);
        make(L"BUTTON",L"字級 -",BS_PUSHBUTTON,190,52,110,28,102);
        make(L"BUTTON",L"字級 +",BS_PUSHBUTTON,312,52,110,28,103);
        description=make(L"STATIC",L"",0,18,88,890,22,104);
        SetFocus(editor);SendMessageW(editor,EM_SETSEL,0,4);
        const std::wstring fixture=L"繁體中文，設定 ABC 123 office A\u0301";
        SendMessageW(editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"測試"));
        require(edit_text().starts_with(L"測試"),"Native selection replacement failed");
        require(SendMessageW(editor,EM_UNDO,0,0)!=0 && edit_text()==fixture,"Native undo failed");
        std::cout<<"PASS native selection replacement/undo harness (not custom text editing)\n";
        SendMessageW(editor,EM_SETSEL,0,4);
        {
            Scene scene(c.gl);scene.init();
            for(int size:{20,28,42}){pixel_size=size;display(c,scene,text,edit_text(),0,4);RECT r{};GetClientRect(c.window,&r);std::string file="text-"+std::to_string(size)+".bmp";capture(c.gl,r.right,r.bottom-120,file.c_str());require(SwapBuffers(c.dc),"Swap failed");}
            pixel_size=28;
            std::wstring last;DWORD oldBegin=~0u,oldEnd=~0u;int oldSize=-1;bool oldFont=false;RECT oldRect{};
            while(interactive&&!quitting){
                MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                DWORD begin{},end{};SendMessageW(editor,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
                RECT r{};GetClientRect(c.window,&r);auto value=edit_text();
                if(r.right>0&&r.bottom>120&&(value!=last||begin!=oldBegin||end!=oldEnd||oldSize!=pixel_size||oldFont!=text.ming||r.right!=oldRect.right||r.bottom!=oldRect.bottom)){
                    display(c,scene,text,value,begin,end);SwapBuffers(c.dc);last=value;oldBegin=begin;oldEnd=end;oldSize=pixel_size;oldFont=text.ming;oldRect=r;
                }
                Sleep(16);
            }
        }
        require(c.release(),"Context cleanup failed");
        std::cout<<"PASS text shaping/rasterization/OpenGL preview. Native input is a harness, not proof of custom IME/UIA.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
