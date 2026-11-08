#pragma once
#include "component_type.hpp"
#include "lcars/layout.hpp"
#include "lcars/appearance.hpp"
#include <cstdint>
#include <vector>
#include <string>
namespace lcars::detail {
struct Rgba { unsigned char r,g,b,a; };
struct DrawCommand {
    enum class Type { clear, clip, fill, rounded, glyph, begin_group, end_group } type;
    Rect rect{}; Rgba color{}; int radius = 0;
    std::vector<unsigned char> coverage;
    PaintRole role=PaintRole::content;
    std::string owner; bool popup=false, tooltip=false;
};
struct DrawObservation {
    enum class Type { text, fill, rounded } type;
    std::string owner; std::u16string text;
    Rect bounds, clip; Color color;
    bool popup, tooltip;
    std::size_t begin, end;
};
struct DrawList {
    std::vector<DrawCommand> commands;
    std::vector<DrawObservation> observations;
    void clear(Rgba color) { commands.push_back({DrawCommand::Type::clear,{},color,0,{},PaintRole::content,{},false,false}); }
    void set_clip(Rect rect) { commands.push_back({DrawCommand::Type::clip,rect,{},0,{},PaintRole::content,{},false,false}); }
    void fill_rect(Rect rect,Rgba color) { commands.push_back({DrawCommand::Type::fill,rect,color,0,{},PaintRole::content,{},false,false}); }
    void rounded_rect(Rect rect,int radius,Rgba color) { commands.push_back({DrawCommand::Type::rounded,rect,color,radius,{},PaintRole::content,{},false,false}); }
};
struct RecordingCanvas {
    int width, height; Rect clip; PaintRole current_role=PaintRole::content; bool count_ink=false; DrawList list;
    RecordingCanvas(int w,int h,Color color);
    void fill(Rect,Color);
    void rounded(Rect,int,Color);
    void glyph(Rect,Color,std::vector<unsigned char>);
    void blend(int,int,Color,unsigned);
    void begin_group(std::string owner,bool popup,bool tooltip);
    void end_group();
};
struct Image { int width,height; std::vector<unsigned char> pixels; };
Image render_cpu(const DrawList&,int width,int height);
bool command_ink(const DrawList&, std::size_t begin, std::size_t end, Rect clip, int width, int height);
}
