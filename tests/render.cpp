#include "render.hpp"
#include "glyph_atlas.hpp"
#include <stdexcept>
using namespace lcars::detail;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    DrawList list;
    list.clear({10, 20, 30, 255});
    list.set_clip({1, 0, 1, 2});
    list.fill_rect({0, 0, 2, 1}, {200, 100, 50, 255});
    auto image = render_cpu(list, 2, 2);
    const std::vector<unsigned char> expected{10, 20, 30, 255, 200, 100, 50, 255,
                                              10, 20, 30, 255, 10, 20, 30, 255};
    require(image.pixels == expected, "top-down pixels and clipping");

    require(command_ink(list, 1, list.commands.size(), {0, 0, 2, 2}, 2, 2), "visible draw command has ink");
    DrawList clipped;
    clipped.set_clip({0, 0, 0, 0});
    clipped.fill_rect({0, 0, 2, 2}, {255, 255, 255, 255});
    require(!command_ink(clipped, 0, clipped.commands.size(), {0, 0, 2, 2}, 2, 2), "empty clip has no ink");

    DrawList translucent;
    translucent.clear({200, 100, 50, 255});
    translucent.fill_rect({0, 0, 1, 1}, {0, 0, 0, 110});
    auto blended = render_cpu(translucent, 1, 1);
    require(blended.pixels == std::vector<unsigned char>{113, 56, 28, 255}, "alpha blending");

    DrawList transparent;
    transparent.clear({0, 0, 0, 0});
    transparent.fill_rect({0, 0, 1, 1}, {255, 0, 0, 128});
    auto partial = render_cpu(transparent, 2, 1);
    require(partial.pixels == std::vector<unsigned char>{128, 0, 0, 128, 0, 0, 0, 0}, "transparent clear and resulting alpha");

    RecordingCanvas atlas_canvas(2, 1, {0, 0, 0});
    auto page = std::make_shared<AtlasPage>();
    page->width = 2; page->height = 1; page->pixels = {64, 255};
    atlas_canvas.glyph({0, 0, 2, 1}, {255, 0, 0}, page, {0, 0, 2, 1});
    require(render_cpu(atlas_canvas.list, 2, 1).pixels ==
            std::vector<unsigned char>{64, 0, 0, 255, 255, 0, 0, 255}, "glyph samples atlas coverage");

    GlyphAtlas atlas(8, 1);
    int rasterizations = 0;
    auto rasterize = [&] { ++rasterizations; return GlyphBitmap{2, 2, 0, 2, {1, 2, 3, 4}}; };
    auto first = atlas.get({1, 42, 18}, rasterize);
    auto again = atlas.get({1, 42, 18}, rasterize);
    require(rasterizations == 1 && first.region.page == again.region.page, "repeated glyph is cached");
    atlas.clear();
    atlas.get({1, 42, 18}, rasterize);
    require(rasterizations == 2 && atlas.clears() == 1, "clear forces a new rasterization");

    GlyphAtlas tiny(4, 1);
    RecordingCanvas batches(2, 1, {0, 0, 0});
    auto one = tiny.get({0, 1, 18}, [] { return GlyphBitmap{1, 1, 0, 0, {64}}; });
    batches.glyph({0, 0, 1, 1}, {255, 0, 0}, one.region.page, one.region.rect);
    auto two = tiny.get({0, 2, 18}, [] { return GlyphBitmap{1, 1, 0, 0, {255}}; });
    batches.glyph({1, 0, 1, 1}, {255, 0, 0}, two.region.page, two.region.rect);
    require(tiny.batches() == 1 && render_cpu(batches.list, 2, 1).pixels ==
            std::vector<unsigned char>{64, 0, 0, 255, 255, 0, 0, 255}, "eviction preserves earlier batch pixels");

    const unsigned char gray[] = {30, 40, 10, 20};
    require(bitmap_coverage(BitmapMode::gray, 2, 2, -2, gray) == std::vector<unsigned char>{10,20,30,40}, "gray bitmap with negative pitch");
    const unsigned char mono[] = {0xa0};
    const unsigned char bgra[] = {2, 3, 4, 55, 6, 7, 8, 200};
    require(bitmap_coverage(BitmapMode::mono, 3, 1, 1, mono) == std::vector<unsigned char>{255,0,255}, "monochrome bitmap conversion");
    require(bitmap_coverage(BitmapMode::bgra, 2, 1, 8, bgra) == std::vector<unsigned char>{55,200}, "color glyph uses alpha");
    require(bitmap_coverage(BitmapMode::unsupported, 2, 1, 8, bgra).empty(), "unsupported glyph is empty");

    DrawList rounded;
    rounded.clear({0, 0, 0, 255});
    rounded.rounded_rect({0, 0, 4, 4}, 2, {255, 255, 255, 255});
    auto corner = render_cpu(rounded, 4, 4);
    require(corner.pixels[0] == 96 && corner.pixels[4] == 234 && corner.pixels[20] == 255,
            "rounded corner coverage and solid interior");
}
