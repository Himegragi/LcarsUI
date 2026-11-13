#include "text.hpp"
#include "utf.hpp"
#include "glyph_atlas.hpp"
#include "font_files.hpp"
#include <windows.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include <hb.h>
#include <hb-ft.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <set>
#include <mutex>
#include <atomic>
namespace lcars::detail {
namespace {
std::string utf8(std::wstring_view value) {
    return utf8_from_utf16(std::u16string_view(reinterpret_cast<const char16_t*>(value.data()), value.size()));
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
struct Box { double left, top, right, bottom; };
// How much a synthetic bold look thickens an outline, in pixels: a bold face is wider, so a synthesized one grows
// the advance by the same amount.
float synthetic_bold_pixels(int pixels) { return std::max(1.f,pixels/16.f); }
struct Font {
    HANDLE file=INVALID_HANDLE_VALUE, mapping=nullptr;
    void* data=nullptr;
    FT_Face face=nullptr;
    hb_font_t* hb=nullptr;
    long error=0;
    std::filesystem::path path;
    Font(FT_Library library,const FontFile& source) {
        path=source.path;
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
    void apply(const GlyphTransform& t) {
        if(t.identity()) { FT_Set_Transform(face,nullptr,nullptr); return; }
        auto fixed=[](float v) { return static_cast<FT_Fixed>(std::lround(v*65536.0)); };
        // Rotating 90 degrees clockwise maps an outline point (x, y up) to (y, -x); the scales apply to the glyph's own axes.
        FT_Matrix matrix=t.rotated?FT_Matrix{0,fixed(t.scale_y),-fixed(t.scale_x),0}:FT_Matrix{fixed(t.scale_x),0,0,fixed(t.scale_y)};
        FT_Set_Transform(face,&matrix,nullptr);
    }
    // synth: 1 draws a heavier outline, 2 a sheared one, for a look this face does not have itself.
    FT_Error rasterize(unsigned glyph,int pixels,const GlyphTransform& transform={},unsigned synth=0) {
        if(!size(pixels)) return FT_Err_Invalid_Pixel_Size;
        apply(transform);
        FT_Error result=FT_Load_Glyph(face,glyph,FT_LOAD_DEFAULT);
        if(!result && synth && face->glyph->format==FT_GLYPH_FORMAT_OUTLINE) {
            if(synth&1) FT_Outline_Embolden(&face->glyph->outline,static_cast<FT_Pos>(std::lround(synthetic_bold_pixels(pixels)*64)));
            if(synth&2) FT_GlyphSlot_Oblique(face->glyph);
        }
        if(!result) result=FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL);
        FT_Set_Transform(face,nullptr,nullptr);
        return result;
    }
    // Where the ink of a transformed glyph lies relative to its origin, in pixels, y down.
    std::optional<Box> ink(unsigned glyph,int pixels,const GlyphTransform& transform) {
        if(!size(pixels)) return {};
        apply(transform);
        std::optional<Box> result;
        if(!FT_Load_Glyph(face,glyph,FT_LOAD_DEFAULT)) {
            auto* slot=face->glyph;
            if(slot->format==FT_GLYPH_FORMAT_OUTLINE) {
                FT_BBox box; FT_Outline_Get_CBox(&slot->outline,&box);
                if(box.xMax>box.xMin && box.yMax>box.yMin) result=Box{box.xMin/64.0,-box.yMax/64.0,box.xMax/64.0,-box.yMin/64.0};
            } else if(!FT_Render_Glyph(slot,FT_RENDER_MODE_NORMAL) && slot->bitmap.rows && slot->bitmap.width) {
                result=Box{double(slot->bitmap_left),-double(slot->bitmap_top),double(slot->bitmap_left+int(slot->bitmap.width)),double(int(slot->bitmap.rows)-slot->bitmap_top)};
            }
        }
        FT_Set_Transform(face,nullptr,nullptr);
        return result;
    }
};
}
struct Text::Impl {
    static constexpr std::array<std::u16string_view,10> names={u"Segoe UI",u"Microsoft JhengHei",u"MingLiU",u"MingLiU-ExtB",u"Microsoft YaHei",u"SimSun",u"Yu Gothic",u"Malgun Gothic",u"Segoe UI Symbol",u"Segoe UI Emoji"};
    // Font ids: 0..9 index `names` (the built-in fallback families; 9 is the emoji family), primary_font is the application's
    // own family (set_primary_family), no_font means no family has the character.
    static constexpr unsigned emoji_font=9, primary_font=10, no_font=11;
    FT_Library library=nullptr;
    GlyphAtlas atlas;
    unsigned rasterizations=0;
    std::array<std::array<std::unique_ptr<Font>,11>,4> fonts;  // [slot][font]: slot bit 0 bold, bit 1 italic
    std::array<std::array<bool,11>,4> attempted{};
    std::array<std::array<std::uint64_t,11>,4> face_ids{};
    std::function<FontFile(std::u16string_view,bool,bool)> styled_lookup;
    static std::uint64_t next_face_id() { static std::atomic<std::uint64_t> next{1}; return next.fetch_add(1, std::memory_order_relaxed); }
    std::u16string primary;
    std::set<unsigned> missing;
    int bitmap_mode=-1;
    std::function<FontFile(std::u16string_view)> lookup;
    std::function<void(const RenderDiagnostic&)> receiver;
    void report(std::string_view code,std::string message={},std::int64_t native=0) {
        RenderDiagnostic event; event.backend=u"FreeType"; event.code=code; event.native_code=native;
        event.native_message=std::move(message); event.kind=DiagnosticKind::resource;
        event.severity=code=="text.glyph_missing"?DiagnosticSeverity::info:DiagnosticSeverity::warning;
        receiver(event);
    }
    Impl(std::function<FontFile(std::u16string_view)> find, std::function<void(const RenderDiagnostic&)> emit)
        : lookup(std::move(find)), receiver(std::move(emit)) {
        if(FT_Init_FreeType(&library)) library=nullptr;
        if(lookup(u"Segoe UI").path.empty()) report("font.default_missing","Segoe UI");
        if(lookup(u"Microsoft JhengHei").path.empty()) report("font.default_missing","Microsoft JhengHei");
    }
    ~Impl(){for(auto& slot:fonts)for(auto& font:slot)font.reset();if(library)FT_Done_FreeType(library);}
    TextLayout shape_directed(std::u16string_view text,int size,hb_direction_t direction,FontStyle style={},bool ligatures=true);
    struct Look { Font* font=nullptr; unsigned slot=0, synth=0; };
    // The face for a bold/italic request: the real file of that look, else the nearest real file plus the looks it
    // still lacks as synth bits (1 heavier, 2 sheared), else the regular face with both.
    Look resolve(unsigned id,bool bold,bool italic) {
        const unsigned want=(bold?1u:0u)|(italic?2u:0u);
        if(!want) return {font(id,0),0,0};
        if(auto exact=font(id,want)) return {exact,want,0};
        if(want==3) {
            if(auto only_bold=font(id,1)) return {only_bold,1,2};
            if(auto only_italic=font(id,2)) return {only_italic,2,1};
        }
        return {font(id,0),0,want};
    }
    Font* font(unsigned id,unsigned slot=0) {
        if(id>=fonts[0].size() || slot>=fonts.size()) return nullptr;
        if(!attempted[slot][id]) {
            attempted[slot][id]=true;
            const auto family=id==primary_font?std::u16string_view(primary):names[id];
            auto file=slot==0?lookup(family):styled_lookup?styled_lookup(family,slot&1,(slot&2)!=0):FontFile{};
            if(!file.path.empty()) { fonts[slot][id]=std::make_unique<Font>(library,file); if(!fonts[slot][id]->face && first_unreadable_file(file.path)) report("font.file_unreadable",utf8(file.path.wstring()),fonts[slot][id]->error); }
        }
        if (fonts[slot][id] && fonts[slot][id]->face && !face_ids[slot][id]) face_ids[slot][id] = next_face_id();
        return fonts[slot][id] && fonts[slot][id]->face ? fonts[slot][id].get():nullptr;
    }
};
Text::Text():Text(system_font_file,[](std::string_view code) { std::wstring wide(code.begin(),code.end()); diagnostic(wide.c_str()); }){ impl_->styled_lookup=system_styled_font_file; }
Text::Text(std::function<FontFile(std::u16string_view)> lookup,std::function<void(std::string_view)> report)
    :Text(std::move(lookup),std::function<void(const RenderDiagnostic&)>([report=std::move(report)](const auto& event) { report(event.code); })){}
Text::Text(std::function<FontFile(std::u16string_view)> lookup,std::function<void(const RenderDiagnostic&)> report)
    :impl_(std::make_unique<Impl>(std::move(lookup),std::move(report))){}
Text::~Text()=default;
void Text::test_bitmap_mode(int mode) { if (impl_->bitmap_mode!=mode) { impl_->bitmap_mode=mode; impl_->atlas.clear(); } }
void Text::set_primary_family(std::u16string family, bool new_generation) { if (impl_->primary==family && !new_generation) return; impl_->atlas.clear(); for(auto& ids:impl_->face_ids) ids.fill(0); impl_->primary=std::move(family); for(unsigned slot=0;slot<4;++slot) { impl_->fonts[slot][10].reset(); impl_->attempted[slot][10]=false; } }
std::optional<float> Text::test_axis_coordinate(unsigned id,std::uint32_t t) {
    auto font=impl_->font(id); if(!font) return {};
    FT_MM_Var* variations=nullptr; if(FT_Get_MM_Var(font->face,&variations) || !variations) return {};
    std::vector<FT_Fixed> coords(variations->num_axis);
    std::optional<float> result;
    if(!FT_Get_Var_Design_Coordinates(font->face,static_cast<FT_UInt>(coords.size()),coords.data())) {
        auto tag=((t&255)<<24)|((t&0xff00)<<8)|((t&0xff0000)>>8)|((t>>24)&255);
        for(unsigned i=0;i<variations->num_axis;++i) if(variations->axis[i].tag==tag) result=coords[i]/65536.f;
    }
    FT_Done_MM_Var(impl_->library,variations); return result;
}
float TextLayout::at(unsigned offset) const {
    auto it=carets.upper_bound(offset);return it==carets.begin()?0:std::prev(it)->second;
}
unsigned TextLayout::hit(float x) const {
    unsigned best=0;float distance=1e30f;
    for(auto [offset,position]:carets)if(std::abs(position-x)<distance){distance=std::abs(position-x);best=offset;}
    return best;
}
TextLayout Text::shape(std::u16string_view text,int size){ return shape(text,size,FontStyle{}); }
void Text::set_styled_lookup(std::function<FontFile(std::u16string_view,bool,bool)> lookup) { impl_->styled_lookup=std::move(lookup); }
TextLayout Text::shape(std::u16string_view text,int size,FontStyle style){ return impl_->shape_directed(text,size,HB_DIRECTION_LTR,style); }
// Optional ligatures (liga, clig) off: every letter of "ffi" stays a cluster of its own; kerning is untouched.
TextLayout Text::shape_apart(std::u16string_view text,int size,FontStyle style){ return impl_->shape_directed(text,size,HB_DIRECTION_LTR,style,false); }
// Top-to-bottom shaping applies the font's vertical alternates ('vert') and reports vertical advances: advance is the
// distance moved down, dx and dy the offset (y up) from the pen at the top center of the cell to the glyph origin.
TextLayout Text::Impl::shape_directed(std::u16string_view text,int size,hb_direction_t direction,FontStyle style,bool ligatures){
    auto& e=*this;
    struct Run{unsigned begin,end,font;hb_script_t script;};std::vector<Run> runs;
    auto unicode=hb_unicode_funcs_get_default();
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
        if(emoji && supports(emoji_font)) selected=emoji_font;
        else if(!e.primary.empty() && supports(primary_font)) selected=primary_font;
        else for(unsigned n=0;n<names.size();++n) if(supports(n)){selected=n;break;}
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
        hb_buffer_set_direction(buffer,direction);hb_buffer_set_script(buffer,run.script);
        hb_buffer_set_language(buffer,hb_language_from_string("zh-TW",-1));
        hb_buffer_add_utf16(buffer,reinterpret_cast<const uint16_t*>(text.data()),static_cast<int>(text.size()),run.begin,run.end-run.begin);
        const auto look=e.resolve(run.font,style.bold,style.italic);
        auto f=look.font; f->size(size);
        const hb_feature_t apart[]={{HB_TAG('l','i','g','a'),0,0,static_cast<unsigned>(-1)},{HB_TAG('c','l','i','g'),0,0,static_cast<unsigned>(-1)}};
        if(ligatures) hb_shape(f->hb,buffer,nullptr,0); else hb_shape(f->hb,buffer,apart,2);
        const float heavier=(look.synth&1)?synthetic_bold_pixels(size):0.f;  // a synthesized bold advances like a bold face would
        unsigned count=0;auto info=hb_buffer_get_glyph_infos(buffer,&count);auto pos=hb_buffer_get_glyph_positions(buffer,&count);
        for(unsigned i=0;i<count;++i){
            float advance=(direction==HB_DIRECTION_TTB?-pos[i].y_advance/64.f:pos[i].x_advance/64.f)+heavier;
            result.carets.try_emplace(info[i].cluster,result.width);
            result.glyphs.push_back({run.font,info[i].codepoint,info[i].cluster,result.width,pos[i].x_offset/64.f,pos[i].y_offset/64.f,advance,e.face_ids[look.slot][run.font],GlyphTransform{},static_cast<unsigned char>(look.slot),static_cast<unsigned char>(look.synth)});
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
void Canvas::fill(Rect r,Color color,unsigned alpha){
    int left=std::max({0,r.x,clip.x}),right=std::min({width,r.x+r.w,clip.x+clip.w});
    int top=std::max({0,r.y,clip.y}),bottom=std::min({height,r.y+r.h,clip.y+clip.h});
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)blend(x,y,color,alpha);
}
void Canvas::rounded(Rect r,int radius,Color color,unsigned alpha){
    radius=std::max(0,std::min({radius,r.w/2,r.h/2}));
    // Three bands that do not overlap, so a translucent fill (hover) blends each pixel once, as the draw-list renderer does.
    fill({r.x+radius,r.y,r.w-radius*2,r.h},color,alpha);
    fill({r.x,r.y+radius,radius,r.h-radius*2},color,alpha);
    fill({r.x+r.w-radius,r.y+radius,radius,r.h-radius*2},color,alpha);
    for(int y=0;y<radius;++y)for(int x=0;x<radius;++x){
        float dx=radius-x-0.5f,dy=radius-y-0.5f;
        float coverage=std::clamp(radius+0.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f);
        unsigned edge=static_cast<unsigned>(coverage*255)*alpha/255;
        blend(r.x+x,r.y+y,color,edge);blend(r.x+r.w-1-x,r.y+y,color,edge);
        blend(r.x+x,r.y+r.h-1-y,color,edge);blend(r.x+r.w-1-x,r.y+r.h-1-y,color,edge);
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
        auto font=impl_->font(glyph.font,glyph.slot);
        if (glyph.face_id && impl_->face_ids[glyph.slot][glyph.font] != glyph.face_id) font = nullptr;
        auto error=font?font->rasterize(glyph.id,layout.size,glyph.transform,glyph.synth):FT_Err_Invalid_Face_Handle;
        if(error) {
            impl_->report("text.glyph_load_failed", "face " + (font?utf8(font->path.wstring()):std::string{}) + " glyph " + std::to_string(glyph.id),error);box();continue;
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
unsigned GlyphTransform::key() const {
    if (identity()) return 0;
    return 1u | (rotated ? 2u : 0u) | (static_cast<unsigned>(std::lround(scale_x * 1024.f)) << 2) | (static_cast<unsigned>(std::lround(scale_y * 1024.f)) << 13);
}
TextLayout Text::shape_vertical(std::u16string_view value,VerticalForm form,int size,Rect cell) {
    auto& e=*impl_;
    TextLayout out; out.size=size;
    const double cx=cell.x+cell.w/2.0, cy=cell.y+cell.h/2.0;
    // Glyph origins are placed in canvas coordinates; draw(layout, 0, 0) then puts each origin on that pixel.
    auto put=[&](Glyph g,double X,double Y) { g.x=static_cast<float>(std::lround(X)); g.dx=0; g.dy=-static_cast<float>(std::lround(Y)); out.glyphs.push_back(g); };
    auto ink=[&](const Glyph& g) -> std::optional<Box> {
        if(!g.id) return std::nullopt;
        auto* font=e.font(g.font); return font?font->ink(g.id,size,g.transform):std::nullopt;
    };
    // Places a group of glyphs (positions relative to the group, screen axes) so that the center of their ink box is (cx, cy).
    struct Part { Glyph glyph; double x, y; };
    auto centered=[&](const std::vector<Part>& parts) {
        double left=1e30,top=1e30,right=-1e30,bottom=-1e30;
        for(const auto& part:parts) if(auto box=ink(part.glyph)) {
            left=std::min(left,part.x+box->left); right=std::max(right,part.x+box->right);
            top=std::min(top,part.y+box->top); bottom=std::max(bottom,part.y+box->bottom);
        }
        double ox=0,oy=0;
        if(right>=left) { ox=cx-(left+right)/2; oy=cy-(top+bottom)/2; }
        for(const auto& part:parts) put(part.glyph,ox+part.x,oy+part.y);
    };
    auto horizontal_parts=[&](const TextLayout& h,GlyphTransform transform) {
        std::vector<Part> parts;
        for(auto g:h.glyphs) {
            g.transform=transform;
            // Rotating clockwise turns "forward" into "down" and "up" into "right".
            parts.push_back({g,transform.rotated?double(g.dy):(g.x+g.dx)*transform.scale_x,transform.rotated?(g.x+g.dx)*transform.scale_x:-double(g.dy)});
        }
        return parts;
    };
    switch(form) {
    case VerticalForm::point: case VerticalForm::middle_dot:
        centered(horizontal_parts(shape(value,size),{}));
        break;
    case VerticalForm::line:
        centered(horizontal_parts(shape(value,size),{true,1.f,1.f}));
        break;
    case VerticalForm::combined: {
        auto h=shape(value,size);
        float scale=h.width>cell.w && h.width>0?cell.w/h.width:1.f;
        centered(horizontal_parts(h,{false,scale,1.f}));
        break;
    }
    case VerticalForm::sideways: {
        // The horizontal line box turned clockwise: its top edge becomes the right edge of the column.
        auto h=shape(value,size);
        const int line=(size*3+1)/2;
        const double baseline_x=cell.x+(cell.w-line)/2.0+line-ShapedMeasure::baseline(size);
        for(auto g:h.glyphs) { g.transform.rotated=true; put(g,baseline_x+g.dy,cell.y+g.x+g.dx); }
        break;
    }
    case VerticalForm::upright: case VerticalForm::bracket: {
        auto h=shape(value,size);
        bool emoji=std::any_of(h.glyphs.begin(),h.glyphs.end(),[](const Glyph& g) { return g.font==Impl::emoji_font && g.id; });
        if(emoji) {
            // Segoe UI Emoji has no vertical metrics and is wider than a cell: scale it into the cell and center it.
            auto parts=horizontal_parts(h,{});
            double left=1e30,top=1e30,right=-1e30,bottom=-1e30;
            for(const auto& part:parts) if(auto box=ink(part.glyph)) {
                left=std::min(left,part.x+box->left); right=std::max(right,part.x+box->right);
                top=std::min(top,part.y+box->top); bottom=std::max(bottom,part.y+box->bottom);
            }
            double widest=std::max(right-left,bottom-top);
            float scale=widest>cell.w && widest>0?static_cast<float>(cell.w/widest):1.f;
            centered(horizontal_parts(h,{false,scale,scale}));
            break;
        }
        auto v=e.shape_directed(value,size,HB_DIRECTION_TTB);
        bool substituted=v.glyphs.size()!=h.glyphs.size();
        for(std::size_t i=0;!substituted && i<v.glyphs.size();++i) substituted=v.glyphs[i].id!=h.glyphs[i].id;
        if(form==VerticalForm::bracket && !substituted) {
            // The font has no vertical form for this bracket: turn the horizontal one a quarter turn.
            centered(horizontal_parts(h,{true,1.f,1.f}));
            break;
        }
        double pen=cell.y;
        for(auto g:v.glyphs) { put(g,cx+g.dx,pen-g.dy); pen+=g.advance; }
        break;
    }
    }
    return out;
}
void Text::draw_vertical(Canvas& canvas,std::u16string_view value,VerticalForm form,int pixels,Rect cell,Color color) { draw(canvas,shape_vertical(value,form,pixels,cell),0,0,color); }
void Text::draw_vertical(RecordingCanvas& canvas,std::u16string_view value,VerticalForm form,int pixels,Rect cell,Color color) { draw(canvas,shape_vertical(value,form,pixels,cell),0,0,color); }
void Text::clear_atlas() { impl_->atlas.clear(); }
void Text::configure_atlas(int size,int pages) { impl_->atlas=GlyphAtlas(size,pages); }
unsigned Text::atlas_clears() const { return impl_->atlas.clears(); }
unsigned Text::atlas_batches() const { return impl_->atlas.batches(); }
unsigned Text::atlas_peak_pages() const { return impl_->atlas.peak_pages(); }
std::vector<const AtlasPage*> Text::atlas_pages() const { return impl_->atlas.pages(); }
unsigned Text::rasterizations() const { return impl_->rasterizations; }
void Text::draw(RecordingCanvas& canvas,const TextLayout& layout,int x,int baseline,Color color){
    for(const auto glyph:layout.glyphs) {
        const int size=layout.size;
        if (!glyph.id) {
            int left=x+static_cast<int>(std::round(glyph.x));
            int width=std::max(1,static_cast<int>(std::round(size*.6f)));
            int height=std::max(1,static_cast<int>(std::round(size*.8f)));
            int line=std::max(1,static_cast<int>(std::round(size/16.f)));
            canvas.fill({left,baseline-height,width,line},color); canvas.fill({left,baseline-line,width,line},color);
            canvas.fill({left,baseline-height,line,height},color); canvas.fill({left+width-line,baseline-height,line,height},color);
            continue;
        }
        canvas.glyph_deferred(color,[this,glyph,size,x,baseline] {
            const auto before=impl_->atlas.batches();
            auto entry=impl_->atlas.get({glyph.face_id,glyph.id,size,glyph.transform.key(),glyph.synth},[&] {
                ++impl_->rasterizations;
                auto box=[&] {
                    int w=std::max(1,static_cast<int>(std::round(size*.6f)));
                    int h=std::max(1,static_cast<int>(std::round(size*.8f)));
                    int line=std::max(1,static_cast<int>(std::round(size/16.f)));
                    GlyphBitmap result{w,h,static_cast<int>(std::round(glyph.x))-static_cast<int>(std::round(glyph.x+glyph.dx)),
                                       h-static_cast<int>(std::round(glyph.dy)),std::vector<unsigned char>(static_cast<std::size_t>(w)*h)};
                    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                        if(y<line || y>=h-line || x<line || x>=w-line) result.pixels[static_cast<std::size_t>(y)*w+x]=255;
                    return result;
                };
                auto font=impl_->font(glyph.font,glyph.slot);
                if (glyph.face_id && impl_->face_ids[glyph.slot][glyph.font] != glyph.face_id) font = nullptr;
                auto error=font?font->rasterize(glyph.id,size,glyph.transform,glyph.synth):FT_Err_Invalid_Face_Handle;
                if(error) {
                    impl_->report("text.glyph_load_failed", "face " + (font?utf8(font->path.wstring()):std::string{}) + " glyph " + std::to_string(glyph.id),error);
                    return box();
                }
                const auto& bitmap=font->face->glyph->bitmap;
                unsigned mode=impl_->bitmap_mode<0?bitmap.pixel_mode:static_cast<unsigned>(impl_->bitmap_mode);
                if(mode!=FT_PIXEL_MODE_GRAY && mode!=FT_PIXEL_MODE_MONO && mode!=FT_PIXEL_MODE_GRAY2 && mode!=FT_PIXEL_MODE_GRAY4 && mode!=FT_PIXEL_MODE_BGRA && bitmap.width && bitmap.rows) {
                    impl_->report("text.glyph_format_unsupported", "pixel mode " + std::to_string(mode),mode); return box();
                }
                GlyphBitmap result{static_cast<int>(bitmap.width),static_cast<int>(bitmap.rows),font->face->glyph->bitmap_left,
                                   font->face->glyph->bitmap_top,std::vector<unsigned char>(static_cast<std::size_t>(bitmap.width)*bitmap.rows)};
                std::vector<unsigned char> row;
                for(unsigned y=0;y<bitmap.rows;++y) {
                    unsigned source=bitmap.pitch>=0?y:bitmap.rows-1-y;
                    bitmap_coverage(mode,bitmap.buffer+source*std::abs(bitmap.pitch),bitmap.width,row);
                    std::copy(row.begin(),row.end(),result.pixels.begin()+static_cast<std::size_t>(y)*bitmap.width);
                }
                return result;
            });
            int left=x+static_cast<int>(std::round(glyph.x+glyph.dx))+entry.left;
            int top=baseline-static_cast<int>(std::round(glyph.dy))-entry.top;
            return ResolvedGlyph{{left,top,entry.region.rect.w,entry.region.rect.h},entry.region,
                                 impl_->atlas.batches(),impl_->atlas.batches()!=before};
        });
    }
}
TextRun ShapedMeasure::measure(std::u16string_view text, int pixels) { return measure(text, pixels, FontStyle{}); }
static TextRun run_of(const TextLayout& layout) {
    TextRun run;
    run.stops.assign(layout.carets.begin(), layout.carets.end());
    run.width = layout.width;
    return run;
}
TextRun ShapedMeasure::measure(std::u16string_view text, int pixels, FontStyle style) { return run_of(text_.shape(text, pixels, style)); }
TextRun ShapedMeasure::measure_apart(std::u16string_view text, int pixels, FontStyle style) { return run_of(text_.shape_apart(text, pixels, style)); }
void Text::label(Canvas& canvas,std::u16string_view value,Rect rect,int size,Color color){
    auto old=canvas.clip;
    int x=std::max(old.x,rect.x),y=std::max(old.y,rect.y);
    canvas.clip={x,y,std::max(0,std::min(old.x+old.w,rect.x+rect.w)-x),std::max(0,std::min(old.y+old.h,rect.y+rect.h)-y)};
    auto layout=shape(value,size);draw(canvas,layout,rect.x,rect.y+size,color);canvas.clip=old;
}
}
