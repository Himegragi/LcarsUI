#pragma once
#include "render.hpp"
#include <functional>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <tuple>
namespace lcars::detail {
enum class BitmapMode { gray, mono, bgra, unsupported };
inline std::vector<unsigned char> bitmap_coverage(BitmapMode mode, unsigned width, unsigned height, int pitch, const unsigned char* buffer) {
    if (mode == BitmapMode::unsupported || !width || !height || !buffer) return {};
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height);
    for (unsigned y=0; y<height; ++y) {
        auto* line=buffer+(pitch>=0?y:height-(y+1))*std::abs(pitch);
        for (unsigned x=0; x<width; ++x)
            pixels[static_cast<std::size_t>(y)*width+x]=mode==BitmapMode::gray?line[x]:mode==BitmapMode::mono?
                ((line[x/8]&(0x80>>(x%8)))?255:0):line[x*4+3];
    }
    return pixels;
}
struct GlyphKey {
    std::uint64_t face; unsigned glyph; int size; unsigned synth = 0;  // synth: the looks drawn by the rasterizer (see Glyph)
    bool operator<(const GlyphKey& other) const { return std::tie(face,glyph,size,synth) < std::tie(other.face,other.glyph,other.size,other.synth); }
};
struct GlyphBitmap { int width, height, left, top; std::vector<unsigned char> pixels; };
struct AtlasGlyph { AtlasRegion region; int left=0, top=0; };
class GlyphAtlas {
    struct ShelfPage { std::unique_ptr<AtlasPage> page; int x=1, y=1, row=0; };
    int size_, max_pages_; std::vector<ShelfPage> pages_; std::map<GlyphKey,AtlasGlyph> glyphs_; unsigned clears_=0, batches_=0, peak_pages_=0;
public:
    explicit GlyphAtlas(int size=1024,int max_pages=4)
        :size_(std::clamp(size,3,1024)),max_pages_(std::clamp(max_pages,1,4)) {}
    void clear() { pages_.clear(); glyphs_.clear(); ++clears_; }
    unsigned clears() const { return clears_; }
    unsigned batches() const { return batches_; }
    unsigned peak_pages() const { return peak_pages_; }
    std::vector<const AtlasPage*> pages() const { std::vector<const AtlasPage*> result; for (const auto& p : pages_) result.push_back(p.page.get()); return result; }
    AtlasGlyph get(GlyphKey key,const std::function<GlyphBitmap()>& rasterize) {
        auto found=glyphs_.find(key); if(found!=glyphs_.end()) return found->second;
        auto bitmap=rasterize(); AtlasGlyph result{{},bitmap.left,bitmap.top};
        if(bitmap.width<=0 || bitmap.height<=0 || bitmap.width+2>size_ || bitmap.height+2>size_) return glyphs_.emplace(key,result).first->second;
        auto place=[&]() -> ShelfPage* {
            for(auto& p:pages_) {
                if(p.x+bitmap.width+1>size_) { p.x=1; p.y+=p.row+1; p.row=0; }
                if(p.y+bitmap.height+1<=size_) return &p;
            }
            if(static_cast<int>(pages_.size())>=max_pages_) return nullptr;
            auto page=std::make_unique<AtlasPage>(); page->width=page->height=size_; page->pixels.resize(static_cast<std::size_t>(size_)*size_);
            pages_.push_back({std::move(page)}); peak_pages_=std::max(peak_pages_,static_cast<unsigned>(pages_.size())); return &pages_.back();
        };
        auto* p=place(); if(!p) { clear(); ++batches_; p=place(); }
        if(!p) return result;
        result.region={p->page.get(),{p->x,p->y,bitmap.width,bitmap.height}};
        for(int y=0;y<bitmap.height;++y) for(int x=0;x<bitmap.width;++x)
            p->page->pixels[static_cast<std::size_t>(p->y+y)*size_+p->x+x]=bitmap.pixels[static_cast<std::size_t>(y)*bitmap.width+x];
        p->x+=bitmap.width+1; p->row=std::max(p->row,bitmap.height);
        glyphs_[key]=result; return result;
    }
};
}
