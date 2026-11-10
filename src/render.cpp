#include "render.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace lcars::detail {
namespace {
unsigned rounded_coverage(Rect rect, int radius, int x, int y) {
    radius = std::max(0, std::min({radius, rect.w / 2, rect.h / 2}));
    if (!radius) return 255;
    float dx = std::max(0.f, float(radius) - std::min(x - rect.x + 0.5f, rect.x + rect.w - x - 0.5f));
    float dy = std::max(0.f, float(radius) - std::min(y - rect.y + 0.5f, rect.y + rect.h - y - 0.5f));
    if (!dx || !dy) return 255;
    return static_cast<unsigned>(std::clamp(radius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f) * 255);
}
}
RecordingCanvas::RecordingCanvas(int w, int h, Color color)
    : width(w), height(h), clip{0, 0, w, h} {
    list.clear({color.r, color.g, color.b, 255});
}
void RecordingCanvas::fill(Rect rect, Color color) {
    list.set_clip(clip);
    list.fill_rect(rect, {color.r, color.g, color.b, 255});
    list.commands.back().role = current_role;

}
void RecordingCanvas::rounded(Rect rect, int radius, Color color) {
    list.set_clip(clip);
    list.rounded_rect(rect, radius, {color.r, color.g, color.b, 255});
    list.commands.back().role = current_role;

}
void RecordingCanvas::glyph(Rect rect, Color color, std::shared_ptr<const AtlasPage> page, Rect region) {
    list.set_clip(clip);
    DrawCommand command{DrawCommand::Type::glyph, rect, {color.r, color.g, color.b, 255}, 0,
                        {page.get(), region}, current_role, {}, false, false, {}, {}};
    command.page_owner = std::move(page);
    list.commands.push_back(std::move(command));
}
void RecordingCanvas::glyph_deferred(Color color, std::function<ResolvedGlyph()> resolve) {
    list.set_clip(clip);
    DrawCommand command{DrawCommand::Type::glyph, {}, {color.r,color.g,color.b,255}, 0, {}, current_role, {}, false, false, {}, {}};
    command.resolve = std::move(resolve);
    list.commands.push_back(std::move(command));
}
void RecordingCanvas::blend(int x, int y, Color color, unsigned alpha) {
    list.set_clip(clip);
    list.fill_rect({x, y, 1, 1}, {color.r, color.g, color.b, static_cast<unsigned char>(alpha)});
}
void RecordingCanvas::begin_group(std::string owner, bool popup, bool tooltip) {
    DrawCommand command{DrawCommand::Type::begin_group, {}, {}, 0, {}, PaintRole::content, std::move(owner), popup, tooltip, {}, {}};
    list.commands.push_back(std::move(command));
}
void RecordingCanvas::end_group() {
    list.commands.push_back({DrawCommand::Type::end_group, {}, {}, 0, {}, PaintRole::content, {}, false, false, {}, {}});
}
bool command_ink(const DrawList& list, std::size_t begin, std::size_t end, Rect clip, int width, int height) {
    clip = intersect(clip, {0, 0, width, height});
    for (std::size_t i = begin; i < end; ++i) {
        const auto& command = list.commands[i];
        if (command.type == DrawCommand::Type::clip) { clip = intersect(command.rect, {0, 0, width, height}); continue; }
        if (command.type != DrawCommand::Type::fill && command.type != DrawCommand::Type::rounded && command.type != DrawCommand::Type::glyph) continue;
        if (!command.color.a) continue;
        auto resolved = command.resolve ? command.resolve() : ResolvedGlyph{command.rect, command.coverage};
        if (command.type == DrawCommand::Type::glyph && !resolved.region.page) continue;
        auto bounds = intersect(resolved.rect, clip);
        for (int y = bounds.y; y < bounds.y + bounds.h; ++y) for (int x = bounds.x; x < bounds.x + bounds.w; ++x) {
            if (command.type == DrawCommand::Type::fill) return true;
            if (command.type == DrawCommand::Type::rounded && rounded_coverage(command.rect, command.radius, x, y)) return true;
            if (command.type == DrawCommand::Type::glyph && resolved.region.page->pixels[static_cast<std::size_t>(resolved.region.rect.y + y - resolved.rect.y) * resolved.region.page->width + resolved.region.rect.x + x - resolved.rect.x]) return true;
        }
    }
    return false;
}

ImageComparison compare_images(const Image& expected, const Image& actual, const std::vector<Rect>& regions) {
    ImageComparison result;
    if (expected.width != actual.width || expected.height != actual.height || expected.pixels.size() != actual.pixels.size()) {
        result.regions_ok = false; result.different_pixels = expected.pixels.size(); return result;
    }
    std::vector<bool> different(static_cast<std::size_t>(expected.width) * expected.height);
    for (std::size_t pixel = 0; pixel < different.size(); ++pixel) {
        for (int c = 0; c < 4; ++c) {
            int delta = std::abs(int(expected.pixels[pixel*4+c]) - int(actual.pixels[pixel*4+c]));
            result.maximum_difference = std::max(result.maximum_difference, delta);
            different[pixel] = different[pixel] || delta > 8;
        }
        result.different_pixels += different[pixel];
    }
    for (auto region : regions) {
        bool passed = true;
        region = intersect({region.x-1, region.y-1, region.w+2, region.h+2}, {0,0,expected.width,expected.height});
        for (int y=region.y; y<region.y+region.h; ++y) for(int x=region.x; x<region.x+region.w; ++x)
            if (different[static_cast<std::size_t>(y)*expected.width+x]) passed=false;
        result.region_pass.push_back(passed);
        result.regions_ok = result.regions_ok && passed;
    }
    return result;
}

Image render_cpu(const DrawList& list, int width, int height) {
    Image image{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4)};
    // Preflight once; if accumulated entries filled the atlas, retry from a clean
    // generation. If this frame itself is too large, execute it incrementally.
    bool restarted = false;
    for (;;) {
        bool overflow = false;
        for (const auto& command : list.commands) if (command.resolve && command.resolve().reset) { overflow = true; break; }
        if (!overflow || restarted) break;
        restarted = true;
    }
    Rect clip{0, 0, width, height};
    for (const auto& command : list.commands) {
        if (command.type == DrawCommand::Type::clip) {
            clip = intersect(command.rect, {0, 0, width, height});
            continue;
        }
        if (command.type != DrawCommand::Type::clear && command.type != DrawCommand::Type::fill &&
            command.type != DrawCommand::Type::rounded && command.type != DrawCommand::Type::glyph) continue;
        auto resolved = command.resolve ? command.resolve() : ResolvedGlyph{command.rect, command.coverage};
        if (command.type == DrawCommand::Type::glyph && !resolved.region.page) continue;
        Rect rect = command.type == DrawCommand::Type::clear ? Rect{0, 0, width, height} : resolved.rect;
        Rect bounds = intersect(rect, command.type == DrawCommand::Type::clear ? Rect{0, 0, width, height} : clip);
        for (int y = bounds.y; y < bounds.y + bounds.h; ++y) {
            for (int x = bounds.x; x < bounds.x + bounds.w; ++x) {
                unsigned coverage = 255;
                if (command.type == DrawCommand::Type::rounded) coverage = rounded_coverage(rect, command.radius, x, y);
                if (command.type == DrawCommand::Type::glyph)
                    coverage = resolved.region.page->pixels[static_cast<std::size_t>(resolved.region.rect.y + y - rect.y) * resolved.region.page->width + resolved.region.rect.x + x - rect.x];
                unsigned alpha = coverage * command.color.a / 255;
                auto index = (static_cast<std::size_t>(y) * width + x) * 4;
                unsigned char values[] = {command.color.r, command.color.g, command.color.b};
                if (command.type == DrawCommand::Type::clear) {
                    for (int channel = 0; channel < 3; ++channel) image.pixels[index + channel] = values[channel];
                    image.pixels[index + 3] = command.color.a;
                    continue;
                }
                for (int channel = 0; channel < 3; ++channel)
                    image.pixels[index + channel] = static_cast<unsigned char>((values[channel] * alpha + image.pixels[index + channel] * (255 - alpha)) / 255);
                image.pixels[index + 3] = static_cast<unsigned char>(alpha + image.pixels[index + 3] * (255 - alpha) / 255);
            }
        }
    }
    return image;
}
}
