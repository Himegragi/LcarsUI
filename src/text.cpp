#include "text.hpp"
#include "glyph_atlas.hpp"
#include <windows.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace lcars::detail {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Font {
    std::vector<unsigned char> bytes;
    FT_Face face=nullptr;
    hb_font_t* hb=nullptr;
    Font(FT_Library library,const std::filesystem::path& path) {
        std::ifstream stream(path,std::ios::binary);
        require(bool(stream),"Required Windows font unavailable (Segoe UI / Microsoft JhengHei / MingLiU)");
        bytes.assign(std::istreambuf_iterator<char>(stream),{});
        require(FT_New_Memory_Face(library,bytes.data(),static_cast<FT_Long>(bytes.size()),0,&face)==0,"Font load failed");
        hb=hb_ft_font_create_referenced(face);
    }
    ~Font(){if(hb)hb_font_destroy(hb);if(face)FT_Done_Face(face);}
    void size(int pixels){require(FT_Set_Pixel_Sizes(face,0,pixels)==0,"Font size failed");hb_ft_font_changed(hb);}
};
}
struct Text::Impl {
    FT_Library library=nullptr;
    std::unique_ptr<Font> latin,traditional;
    std::filesystem::path fonts;
    bool ming=false;
    GlyphAtlas atlas;
    unsigned rasterizations=0;
    Impl() {
        wchar_t path[MAX_PATH]{};
        require(GetWindowsDirectoryW(path,MAX_PATH)>0,"Windows font directory unavailable");
        fonts=std::filesystem::path(path)/"Fonts";
        require(FT_Init_FreeType(&library)==0,"FreeType initialization failed");
        try {latin=std::make_unique<Font>(library,fonts/"segoeui.ttf");traditional=std::make_unique<Font>(library,fonts/"msjh.ttc");}
        catch(...){traditional.reset();latin.reset();FT_Done_FreeType(library);throw;}
    }
    ~Impl(){traditional.reset();latin.reset();FT_Done_FreeType(library);}
    Font& font(unsigned id){return id?*traditional:*latin;}
};
Text::Text():impl_(std::make_unique<Impl>()){}
Text::~Text()=default;
void Text::set_ming(bool ming){
    if(impl_->ming==ming)return;
    auto replacement=std::make_unique<Font>(impl_->library,impl_->fonts/(ming?"mingliu.ttc":"msjh.ttc"));
    impl_->traditional=std::move(replacement);impl_->ming=ming; impl_->atlas.clear();
}
float TextLayout::at(unsigned offset) const {
    auto it=carets.upper_bound(offset);return it==carets.begin()?0:std::prev(it)->second;
}
unsigned TextLayout::hit(float x) const {
    unsigned best=0;float distance=1e30f;
    for(auto [offset,position]:carets)if(std::abs(position-x)<distance){distance=std::abs(position-x);best=offset;}
    return best;
}
TextLayout Text::shape(std::u16string_view text,int size){
    auto& e=*impl_;e.latin->size(size);e.traditional->size(size);
    struct Run{unsigned begin,end,font;hb_script_t script;};std::vector<Run> runs;
    auto unicode=hb_unicode_funcs_get_default();
    for(unsigned i=0;i<text.size();){
        unsigned begin=i,cp=text[i++];
        if(cp>=0xd800&&cp<=0xdbff&&i<text.size()&&text[i]>=0xdc00&&text[i]<=0xdfff)cp=0x10000+((cp-0xd800)<<10)+(text[i++]-0xdc00);
        auto script=hb_unicode_script(unicode,cp);
        unsigned selected=FT_Get_Char_Index(e.latin->face,cp)?0:1;
        if(script==HB_SCRIPT_INHERITED&&!runs.empty()&&FT_Get_Char_Index(e.font(runs.back().font).face,cp))selected=runs.back().font;
        if(script==HB_SCRIPT_COMMON||script==HB_SCRIPT_INHERITED)script=runs.empty()?HB_SCRIPT_LATIN:runs.back().script;
        if(!runs.empty()&&runs.back().font==selected&&runs.back().script==script)runs.back().end=i;
        else runs.push_back({begin,i,selected,script});
    }
    TextLayout result;result.size=size;result.carets[0]=0;
    for(auto& run:runs){
        auto buffer=hb_buffer_create();
        hb_buffer_set_direction(buffer,HB_DIRECTION_LTR);hb_buffer_set_script(buffer,run.script);
        hb_buffer_set_language(buffer,hb_language_from_string("zh-TW",-1));
        hb_buffer_add_utf16(buffer,reinterpret_cast<const uint16_t*>(text.data()),static_cast<int>(text.size()),run.begin,run.end-run.begin);
        hb_shape(e.font(run.font).hb,buffer,nullptr,0);
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
void Text::draw(Canvas& canvas,const TextLayout& layout,int x,int baseline,Color color){
    for(auto& glyph:layout.glyphs){
        auto& font=impl_->font(glyph.font);font.size(layout.size);
        require(FT_Load_Glyph(font.face,glyph.id,FT_LOAD_DEFAULT)==0,"Glyph load failed");
        require(FT_Render_Glyph(font.face->glyph,FT_RENDER_MODE_NORMAL)==0,"Glyph rasterization failed");
        auto& bitmap=font.face->glyph->bitmap;
        int left=x+static_cast<int>(std::round(glyph.x+glyph.dx))+font.face->glyph->bitmap_left;
        int top=baseline-static_cast<int>(std::round(glyph.dy))-font.face->glyph->bitmap_top;
        for(unsigned row=0;row<bitmap.rows;++row)for(unsigned col=0;col<bitmap.width;++col){
            unsigned source=bitmap.pitch>=0?row:bitmap.rows-1-row,alpha=0;
            if(bitmap.pixel_mode==FT_PIXEL_MODE_GRAY)alpha=bitmap.buffer[source*std::abs(bitmap.pitch)+col];
            else if(bitmap.pixel_mode==FT_PIXEL_MODE_MONO)alpha=(bitmap.buffer[source*std::abs(bitmap.pitch)+col/8]&(0x80>>(col%8)))?255:0;
            else throw std::runtime_error("Unsupported font bitmap format");
            canvas.blend(left+col,top+row,glyph.id?color:Color{195,35,48},alpha);
        }
    }
}
void Text::clear_atlas() { impl_->atlas.clear(); }
void Text::configure_atlas(int size,int pages) { impl_->atlas=GlyphAtlas(size,pages); }
unsigned Text::atlas_clears() const { return impl_->atlas.clears(); }
unsigned Text::atlas_batches() const { return impl_->atlas.batches(); }
unsigned Text::rasterizations() const { return impl_->rasterizations; }
void Text::draw(RecordingCanvas& canvas,const TextLayout& layout,int x,int baseline,Color color){
    for(const auto& glyph:layout.glyphs){
        auto entry=impl_->atlas.get({glyph.font,glyph.id,layout.size},[&] {
            ++impl_->rasterizations;
            GlyphBitmap result{0,0,0,0,{}};
            auto& font=impl_->font(glyph.font);
            if(FT_Set_Pixel_Sizes(font.face,0,layout.size) || FT_Load_Glyph(font.face,glyph.id,FT_LOAD_DEFAULT) ||
               FT_Render_Glyph(font.face->glyph,FT_RENDER_MODE_NORMAL)) {
                OutputDebugStringA("LcarsUI: glyph rasterization failed\n"); return result;
            }
            const auto& bitmap=font.face->glyph->bitmap;
            result.width=static_cast<int>(bitmap.width); result.height=static_cast<int>(bitmap.rows);
            result.left=font.face->glyph->bitmap_left; result.top=font.face->glyph->bitmap_top;
            if(bitmap.pixel_mode!=FT_PIXEL_MODE_GRAY && bitmap.pixel_mode!=FT_PIXEL_MODE_MONO && bitmap.pixel_mode!=FT_PIXEL_MODE_BGRA && bitmap.width && bitmap.rows) {
                OutputDebugStringA("LcarsUI: unsupported glyph bitmap format\n"); result.width=result.height=0; return result;
            }
            auto mode=bitmap.pixel_mode==FT_PIXEL_MODE_GRAY?BitmapMode::gray:
                bitmap.pixel_mode==FT_PIXEL_MODE_MONO?BitmapMode::mono:BitmapMode::bgra;
            result.pixels=bitmap_coverage(mode,bitmap.width,bitmap.rows,bitmap.pitch,bitmap.buffer);
            return result;
        });
        if(!entry.region.page) continue;
        int left=x+static_cast<int>(std::round(glyph.x+glyph.dx))+entry.left;
        int top=baseline-static_cast<int>(std::round(glyph.dy))-entry.top;
        canvas.glyph({left,top,entry.region.rect.w,entry.region.rect.h},glyph.id?color:Color{195,35,48},entry.region.page,entry.region.rect);
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
