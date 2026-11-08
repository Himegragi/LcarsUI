#include "text.hpp"
#include "font_files.hpp"
#include <windows.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <hb.h>
#include <hb-ft.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <set>
#include <mutex>
namespace lcars::detail {
namespace {
std::string utf8(std::wstring_view value) {
    if(value.empty()) return {};
    int size=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(size<=0) return {};
    std::string result(size,'\0'); WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr); return result;
}
bool first_unreadable_file(const std::filesystem::path& path) {
    static std::mutex mutex;
    static std::set<std::wstring> reported;
    auto key=path.wstring();
    std::transform(key.begin(),key.end(),key.begin(),::towlower);
    std::lock_guard lock(mutex);
    return reported.insert(std::move(key)).second;
}
void diagnostic(const wchar_t* message) {
#ifndef NDEBUG
    OutputDebugStringW(message); OutputDebugStringW(L"\n");
#else
    (void)message;
#endif
}
struct Font {
    HANDLE file=INVALID_HANDLE_VALUE, mapping=nullptr;
    void* data=nullptr;
    FT_Face face=nullptr;
    hb_font_t* hb=nullptr;
    long error=0;
    Font(FT_Library library,const FontFile& source) {
        if (!library || source.path.empty()) return;
        file=CreateFileW(source.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        LARGE_INTEGER length{};
        if(file==INVALID_HANDLE_VALUE || !GetFileSizeEx(file,&length) || length.QuadPart<=0 || length.QuadPart>LONG_MAX ||
           !(mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr)) || !(data=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0)) ||
           (error=FT_New_Memory_Face(library,static_cast<const FT_Byte*>(data),static_cast<FT_Long>(length.QuadPart),source.face_index,&face))) {
            if(!error) error=static_cast<long>(GetLastError());
            return;
        }
        if(!source.axes.empty() && FT_HAS_MULTIPLE_MASTERS(face)) {
            FT_MM_Var* variations=nullptr;
            if(!FT_Get_MM_Var(face,&variations) && variations) {
                std::vector<FT_Fixed> coords(variations->num_axis);
                for(unsigned i=0;i<variations->num_axis;++i) {
                    coords[i]=variations->axis[i].def;
                    for(const auto& axis:source.axes) {
                        auto t=axis.tag;
                        std::uint32_t tag=((t&255)<<24)|((t&0xff00)<<8)|((t&0xff0000)>>8)|((t>>24)&255);
                        if(tag==variations->axis[i].tag) coords[i]=static_cast<FT_Fixed>(std::lround(axis.value*65536.0));
                    }
                }
                FT_Set_Var_Design_Coordinates(face,static_cast<FT_UInt>(coords.size()),coords.data());
                FT_Done_MM_Var(library,variations);
            }
        }
        hb=hb_ft_font_create_referenced(face);
    }
    ~Font(){if(hb)hb_font_destroy(hb);if(face)FT_Done_Face(face);if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    bool size(int pixels){if(!face || FT_Set_Pixel_Sizes(face,0,pixels))return false;hb_ft_font_changed(hb);return true;}
};
}
struct Text::Impl {
    static constexpr std::array<std::u16string_view,10> names={u"Segoe UI",u"Microsoft JhengHei",u"MingLiU",u"MingLiU-ExtB",u"Microsoft YaHei",u"SimSun",u"Yu Gothic",u"Malgun Gothic",u"Segoe UI Symbol",u"Segoe UI Emoji"};
    FT_Library library=nullptr;
    std::array<std::unique_ptr<Font>,10> fonts;
    std::array<bool,10> attempted{};
    std::set<unsigned> missing;
    int bitmap_mode=-1;
    std::function<FontFile(std::u16string_view)> lookup;
    std::function<void(const RenderDiagnostic&)> receiver;
    void report(std::string_view code,std::string message={},std::int64_t native=0) {
        RenderDiagnostic event; event.backend=u"FreeType"; event.code=code; event.native_code=native;
        event.native_message=std::move(message); event.category=DiagnosticCategory::resource;
        event.severity=code=="text.glyph_missing"?DiagnosticSeverity::info:DiagnosticSeverity::warning;
        receiver(event);
    }
    Impl(std::function<FontFile(std::u16string_view)> find, std::function<void(const RenderDiagnostic&)> emit)
        : lookup(std::move(find)), receiver(std::move(emit)) {
        if(FT_Init_FreeType(&library)) library=nullptr;
        if(lookup(u"Segoe UI").path.empty()) report("font.default_missing","Segoe UI");
        if(lookup(u"Microsoft JhengHei").path.empty()) report("font.default_missing","Microsoft JhengHei");
    }
    ~Impl(){for(auto& font:fonts)font.reset();if(library)FT_Done_FreeType(library);}
    Font* font(unsigned id) {
        if(id>=fonts.size()) return nullptr;
        if(!attempted[id]) { attempted[id]=true; auto file=lookup(names[id]); if(!file.path.empty()) { fonts[id]=std::make_unique<Font>(library,file); if(!fonts[id]->face && first_unreadable_file(file.path)) report("font.file_unreadable",utf8(file.path.wstring()),fonts[id]->error); } }
        return fonts[id] && fonts[id]->face ? fonts[id].get():nullptr;
    }
};
Text::Text():Text(system_font_file,[](std::string_view code) { std::wstring wide(code.begin(),code.end()); diagnostic(wide.c_str()); }){}
Text::Text(std::function<FontFile(std::u16string_view)> lookup,std::function<void(std::string_view)> report)
    :Text(std::move(lookup),std::function<void(const RenderDiagnostic&)>([report=std::move(report)](const auto& event) { report(event.code); })){}
Text::Text(std::function<FontFile(std::u16string_view)> lookup,std::function<void(const RenderDiagnostic&)> report)
    :impl_(std::make_unique<Impl>(std::move(lookup),std::move(report))){}
Text::~Text()=default;
void Text::test_bitmap_mode(int mode) { impl_->bitmap_mode=mode; }
float TextLayout::at(unsigned offset) const {
    auto it=carets.upper_bound(offset);return it==carets.begin()?0:std::prev(it)->second;
}
unsigned TextLayout::hit(float x) const {
    unsigned best=0;float distance=1e30f;
    for(auto [offset,position]:carets)if(std::abs(position-x)<distance){distance=std::abs(position-x);best=offset;}
    return best;
}
TextLayout Text::shape(std::u16string_view text,int size){
    auto& e=*impl_;
    struct Run{unsigned begin,end,font;hb_script_t script;};std::vector<Run> runs;
    auto unicode=hb_unicode_funcs_get_default();
    constexpr unsigned no_font=10;
    for(unsigned i=0;i<text.size();){
        unsigned begin=i,cp=text[i++];
        if(cp>=0xd800&&cp<=0xdbff&&i<text.size()&&text[i]>=0xdc00&&text[i]<=0xdfff)cp=0x10000+((cp-0xd800)<<10)+(text[i++]-0xdc00);
        auto script=hb_unicode_script(unicode,cp);
        bool follows=script==HB_SCRIPT_INHERITED || (cp>=0xfe00&&cp<=0xfe0f) || (cp>=0xe0100&&cp<=0xe01ef) || cp==0x200c || cp==0x200d;
        if(follows && !runs.empty()) {runs.back().end=i;continue;}
        if(follows) continue;
        unsigned selected=no_font;
        bool emoji=(cp>=0x1f000&&cp<=0x1faff) || (i<text.size() && text[i]==0xfe0f);
        auto supports=[&](unsigned n) {auto f=e.font(n);return f && f->size(size) && FT_Get_Char_Index(f->face,cp);};
        if(emoji && supports(9)) selected=9;
        else for(unsigned n=0;n<10;++n) if(supports(n)){selected=n;break;}
        if(script==HB_SCRIPT_COMMON||script==HB_SCRIPT_INHERITED)script=runs.empty()?HB_SCRIPT_LATIN:runs.back().script;
        if(selected!=no_font && !runs.empty()&&runs.back().font==selected&&runs.back().script==script)runs.back().end=i;
        else runs.push_back({begin,i,selected,script});
    }
    TextLayout result;result.size=size;result.carets[0]=0;
    for(auto& run:runs){
        if(run.font==no_font) {
            unsigned cp=text[run.begin]; if(cp>=0xd800&&cp<=0xdbff && run.begin+1<text.size()) cp=0x10000+((cp-0xd800)<<10)+(text[run.begin+1]-0xdc00);
            if(e.missing.insert(cp).second)e.report("text.glyph_missing", "Unicode code point " + std::to_string(cp), cp);
            float advance=cp<128?size*.6f:static_cast<float>(size);
            result.carets.try_emplace(run.begin,result.width); result.glyphs.push_back({no_font,0,run.begin,result.width,0,0,advance}); result.width+=advance; ++result.missing; continue;
        }
        auto buffer=hb_buffer_create();
        hb_buffer_set_direction(buffer,HB_DIRECTION_LTR);hb_buffer_set_script(buffer,run.script);
        hb_buffer_set_language(buffer,hb_language_from_string("zh-TW",-1));
        hb_buffer_add_utf16(buffer,reinterpret_cast<const uint16_t*>(text.data()),static_cast<int>(text.size()),run.begin,run.end-run.begin);
        auto f=e.font(run.font); f->size(size); hb_shape(f->hb,buffer,nullptr,0);
        unsigned count=0;auto info=hb_buffer_get_glyph_infos(buffer,&count);auto pos=hb_buffer_get_glyph_positions(buffer,&count);
        for(unsigned i=0;i<count;++i){
            float advance=pos[i].x_advance/64.f;
            result.carets.try_emplace(info[i].cluster,result.width);
            result.glyphs.push_back({run.font,info[i].codepoint,info[i].cluster,result.width,pos[i].x_offset/64.f,pos[i].y_offset/64.f,advance});
            result.width+=advance;result.missing+=info[i].codepoint==0;
        }
        hb_buffer_destroy(buffer);
    }
    result.carets[static_cast<unsigned>(text.size())]=result.width;return result;
}
Canvas::Canvas(int w,int h,Color color):width(w),height(h),pixels(static_cast<std::size_t>(w)*h*4),clip{0,0,w,h}{fill(clip,color);}
void Canvas::blend(int x,int y,Color color,unsigned alpha){
    if(!clip.contains(x,y)||x<0||y<0||x>=width||y>=height)return;
    if(count_ink && alpha) ++ink;
    auto index=(static_cast<std::size_t>(y)*width+x)*4;
    const unsigned char ink[]={color.r,color.g,color.b};
    for(int c=0;c<3;++c)pixels[index+c]=static_cast<unsigned char>((ink[c]*alpha+pixels[index+c]*(255-alpha))/255);
    pixels[index+3]=255;
}
void Canvas::fill(Rect r,Color color){
    int left=std::max({0,r.x,clip.x}),right=std::min({width,r.x+r.w,clip.x+clip.w});
    int top=std::max({0,r.y,clip.y}),bottom=std::min({height,r.y+r.h,clip.y+clip.h});
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)blend(x,y,color,255);
}
void Canvas::rounded(Rect r,int radius,Color color){
    radius=std::max(0,std::min({radius,r.w/2,r.h/2}));
    fill({r.x+radius,r.y,r.w-radius*2,r.h},color);
    fill({r.x,r.y+radius,r.w,r.h-radius*2},color);
    for(int y=0;y<radius;++y)for(int x=0;x<radius;++x){
        float dx=radius-x-0.5f,dy=radius-y-0.5f;
        float coverage=std::clamp(radius+0.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f);
        unsigned alpha=static_cast<unsigned>(coverage*255);
        blend(r.x+x,r.y+y,color,alpha);blend(r.x+r.w-1-x,r.y+y,color,alpha);
        blend(r.x+x,r.y+r.h-1-y,color,alpha);blend(r.x+r.w-1-x,r.y+r.h-1-y,color,alpha);
    }
}
bool bitmap_coverage(unsigned mode,const unsigned char* row,unsigned width,std::vector<unsigned char>& coverage) {
    if(mode!=FT_PIXEL_MODE_GRAY && mode!=FT_PIXEL_MODE_MONO && mode!=FT_PIXEL_MODE_GRAY2 && mode!=FT_PIXEL_MODE_GRAY4 && mode!=FT_PIXEL_MODE_BGRA) return false;
    coverage.resize(width);
    for(unsigned col=0;col<width;++col) {
        unsigned alpha=0;
        if(mode==FT_PIXEL_MODE_GRAY)alpha=row[col];
        else if(mode==FT_PIXEL_MODE_MONO)alpha=(row[col/8]&(0x80>>(col%8)))?255:0;
        else if(mode==FT_PIXEL_MODE_GRAY2)alpha=((row[col/4]>>(6-2*(col%4)))&3)*85;
        else if(mode==FT_PIXEL_MODE_GRAY4)alpha=((row[col/2]>>(4-4*(col%2)))&15)*17;
        else alpha=row[col*4+3];
        coverage[col]=static_cast<unsigned char>(alpha);
    }
    return true;
}
void Text::draw(Canvas& canvas,const TextLayout& layout,int x,int baseline,Color color){
    for(auto& glyph:layout.glyphs){
        auto box=[&] {
            int left=x+static_cast<int>(std::round(glyph.x));
            int width=std::max(1,static_cast<int>(std::round(layout.size*.6f)));
            int height=std::max(1,static_cast<int>(std::round(layout.size*.8f)));
            int line=std::max(1,static_cast<int>(std::round(layout.size/16.f)));
            canvas.fill({left,baseline-height,width,line},color); canvas.fill({left,baseline-line,width,line},color);
            canvas.fill({left,baseline-height,line,height},color); canvas.fill({left+width-line,baseline-height,line,height},color);
        };
        if (!glyph.id) {box();continue;}
        auto font=impl_->font(glyph.font);
        if(!font || !font->size(layout.size) || FT_Load_Glyph(font->face,glyph.id,FT_LOAD_DEFAULT) || FT_Render_Glyph(font->face->glyph,FT_RENDER_MODE_NORMAL)) {
            impl_->report("text.glyph_load_failed", "glyph " + std::to_string(glyph.id),glyph.id);box();continue;
        }
        auto& bitmap=font->face->glyph->bitmap;
        unsigned mode=impl_->bitmap_mode<0?bitmap.pixel_mode:static_cast<unsigned>(impl_->bitmap_mode);
        if(mode!=FT_PIXEL_MODE_GRAY && mode!=FT_PIXEL_MODE_MONO && mode!=FT_PIXEL_MODE_GRAY2 && mode!=FT_PIXEL_MODE_GRAY4 && mode!=FT_PIXEL_MODE_BGRA && bitmap.width && bitmap.rows) {
            impl_->report("text.glyph_format_unsupported", "pixel mode " + std::to_string(mode),mode);box();continue;
        }
        int left=x+static_cast<int>(std::round(glyph.x+glyph.dx))+font->face->glyph->bitmap_left;
        int top=baseline-static_cast<int>(std::round(glyph.dy))-font->face->glyph->bitmap_top;
        std::vector<unsigned char> coverage;
        for(unsigned row=0;row<bitmap.rows;++row){
            unsigned source=bitmap.pitch>=0?row:bitmap.rows-1-row; auto data=bitmap.buffer+source*std::abs(bitmap.pitch);
            bitmap_coverage(mode,data,bitmap.width,coverage);
            for(unsigned col=0;col<bitmap.width;++col) canvas.blend(left+col,top+row,color,coverage[col]);
        }
    }
}
void Text::draw(RecordingCanvas& canvas,const TextLayout& layout,int x,int baseline,Color color){
    for(const auto& glyph:layout.glyphs) {
        auto box=[&] {
            int left=x+static_cast<int>(std::round(glyph.x));
            int width=std::max(1,static_cast<int>(std::round(layout.size*.6f)));
            int height=std::max(1,static_cast<int>(std::round(layout.size*.8f)));
            int line=std::max(1,static_cast<int>(std::round(layout.size/16.f)));
            canvas.fill({left,baseline-height,width,line},color); canvas.fill({left,baseline-line,width,line},color);
            canvas.fill({left,baseline-height,line,height},color); canvas.fill({left+width-line,baseline-height,line,height},color);
        };
        if(!glyph.id) {box();continue;}
        auto font=impl_->font(glyph.font);
        if(!font || !font->size(layout.size) || FT_Load_Glyph(font->face,glyph.id,FT_LOAD_DEFAULT) || FT_Render_Glyph(font->face->glyph,FT_RENDER_MODE_NORMAL)) {
            impl_->report("text.glyph_load_failed", "glyph " + std::to_string(glyph.id),glyph.id); box();continue;
        }
        const auto& bitmap=font->face->glyph->bitmap;
        unsigned mode=impl_->bitmap_mode<0?bitmap.pixel_mode:static_cast<unsigned>(impl_->bitmap_mode);
        if(mode!=FT_PIXEL_MODE_GRAY && mode!=FT_PIXEL_MODE_MONO && mode!=FT_PIXEL_MODE_GRAY2 && mode!=FT_PIXEL_MODE_GRAY4 && mode!=FT_PIXEL_MODE_BGRA && bitmap.width && bitmap.rows) {
            impl_->report("text.glyph_format_unsupported", "pixel mode " + std::to_string(mode),mode); box();continue;
        }
        int left=x+static_cast<int>(std::round(glyph.x+glyph.dx))+font->face->glyph->bitmap_left;
        int top=baseline-static_cast<int>(std::round(glyph.dy))-font->face->glyph->bitmap_top;
        std::vector<unsigned char> coverage(static_cast<std::size_t>(bitmap.width)*bitmap.rows), row;
        for(unsigned y=0;y<bitmap.rows;++y) {
            unsigned source=bitmap.pitch>=0?y:bitmap.rows-1-y;
            bitmap_coverage(mode,bitmap.buffer+source*std::abs(bitmap.pitch),bitmap.width,row);
            std::copy(row.begin(),row.end(),coverage.begin()+static_cast<std::size_t>(y)*bitmap.width);
        }
        canvas.glyph({left,top,static_cast<int>(bitmap.width),static_cast<int>(bitmap.rows)},color,std::move(coverage));
    }
}
TextRun ShapedMeasure::measure(std::u16string_view text, int pixels) {
    auto layout = text_.shape(text, pixels);
    TextRun run;
    run.stops.assign(layout.carets.begin(), layout.carets.end());
    run.width = layout.width;
    return run;
}
void Text::label(Canvas& canvas,std::u16string_view value,Rect rect,int size,Color color){
    auto old=canvas.clip;
    int x=std::max(old.x,rect.x),y=std::max(old.y,rect.y);
    canvas.clip={x,y,std::max(0,std::min(old.x+old.w,rect.x+rect.w)-x),std::max(0,std::min(old.y+old.h,rect.y+rect.h)-y)};
    auto layout=shape(value,size);draw(canvas,layout,rect.x,rect.y+size,color);canvas.clip=old;
}
}
