#include "component_type.hpp"
#include "core_internal.hpp"
#include "vertical.hpp"
#include "justify.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lcars::detail {
std::optional<OptionValue> focusOption(const ComponentSnapshot& snapshot) {
    if (snapshot.kind == ComponentKind::dropdown && !snapshot.expanded) return std::nullopt;
    auto item = std::find_if(snapshot.items.begin(), snapshot.items.end(), [&](const auto& option) {
        return option.enabled && (snapshot.kind == ComponentKind::radioGroup ? option.selected : snapshot.candidate == option.value);
    });
    if (item == snapshot.items.end() && snapshot.kind == ComponentKind::radioGroup)
        item = std::find_if(snapshot.items.begin(), snapshot.items.end(), [](const auto& option) { return option.enabled; });
    return item == snapshot.items.end() ? std::nullopt : std::optional{item->value};
}
// The per-type state and the engines a component type reaches into. UiState only declares this class, so nothing outside
// this file can use it: the maps, and the helpers of the engines the types share with the rest of the state.
struct UiState::Internals {
    template <class Map> static void retire(UiState& ui, Map& fields, const Id& id) {
        auto entry = fields.extract(id);
        if (!entry.empty() && entry.mapped().disconnect) {
            try { entry.mapped().disconnect(); } catch (...) { ui.stopped_ = true; }
        }
    }
    template <class U> static auto& edits(U& ui) { return ui.edits_; }
    template <class U> static auto& bools(U& ui) { return ui.bools_; }
    template <class U> static auto& radios(U& ui) { return ui.radios_; }
    template <class U> static auto& dropdowns(U& ui) { return ui.dropdowns_; }
    template <class U> static auto& tabs(U& ui) { return ui.tabs_; }
    template <class U> static auto& lists(U& ui) { return ui.lists_; }
    template <class U> static auto& numerics(U& ui) { return ui.numerics_; }
    template <class U> static auto& progresses(U& ui) { return ui.progresses_; }
    template <class U> static auto& errors(U& ui) { return ui.errors_; }
    template <class U> static auto& providers(U& ui) { return ui.providers_; }
    template <class U> static auto& shortcuts(U& ui) { return ui.shortcuts_; }
    template <class U, class F> static bool sourceMatches(const U& ui, const F& field) { return ui.sourceMatches(field); }
    template <class U, class F> static auto insertionStyle(const U& ui, const F& field, unsigned first, unsigned last) { return ui.insertionStyle(field, first, last); }
    template <class U> static const WordBreaker& dictionary(const U& ui) { return ui.dictionary(); }
    static Result selectOption(UiState& ui, const Id& id, OptionValue value) { return ui.selectOption(id, std::move(value)); }
    static Result selectDropdown(UiState& ui, const Id& id, OptionValue value) { return ui.selectDropdown(id, std::move(value)); }
    static double sliderDisplayRatio(const UiState& ui, std::string_view id) { return ui.sliderDisplayRatio(id); }
    static void sliderStartedMoving(UiState& ui, const Id& id, double from, int valueBefore) { ui.sliderStartedMoving(id, from, valueBefore); }
    static Result editKey(UiState& ui, KeyPress key) { return ui.editKey(key); }
    static Result deleteSelection(UiState& ui, std::string_view id) { return ui.deleteSelection(id); }
};
Palette ComponentPaint::palette() const { return ui.effectivePalette(appearance); }
ComponentPresentation ComponentPaint::presentation() const { return appearance.componentPresentation(ui.presentation().density); }
ComponentPresentation ComponentLayout::presentation() const { return appearance.componentPresentation(ui.presentation().density); }
// The width a line of text keeps when it starts at `textX` instead of at its natural `gap` (a ring pushes text inward) and `trailing`
// pixels stay free on the right.
static int roomAfter(int width, int trailing, int textX, int gap) { return std::max(0, width - trailing - (textX - gap)); }
int textRow(int lineHeight, double lineSpacing) {
    return std::max(lineHeight, static_cast<int>(std::lround(lineHeight * lineSpacing)));
}
int paragraphGap(const ComponentConfig& control, float scale) {
    return control.paragraphSpacing > 0 ? static_cast<int>(std::lround(control.paragraphSpacing * scale)) : 0;
}
bool ComponentPaint::hovered() const { return ui.hoverDrawn(placement.id, nullptr); }
bool ComponentPaint::hovered(const OptionValue& item) const { return ui.hoverDrawn(placement.id, &item); }
void ComponentPaint::hover(Rect rect, int radius) {
    auto p = palette();
    if (!p.hoverAlpha) return;
    role(PaintRole::hover);
    overlay(rect, radius, p.hover, p.hoverAlpha);
    role(PaintRole::content);
}
void ComponentPaint::overlay(Rect rect, int radius, Color color, unsigned char alpha) {
    overlayAlpha_ = alpha;
    if (radius > 0) rounded(rect, radius, color); else fill(rect, color);
    overlayAlpha_ = 255;
}

TextAlign drawnTextAlign(TextAlign align, const PresentationSettings& settings) {
    const bool stretched = align == TextAlign::justify || align == TextAlign::distribute;
    return stretched && settings.justifyAsLeft ? TextAlign::left : align;
}
TextAlign drawnTextAlign(const Component& control, const PresentationSettings& settings) { return drawnTextAlign(control.textAlign, settings); }
ComponentPaint::ComponentPaint(UiState& state, const Placement& item, const ComponentSnapshot& semantic,
                               const Appearance& look, float renderScale, bool activeWindow)
    : ui(state), placement(item), semantics(semantic), appearance(look), scale(renderScale), windowFocused(activeWindow),
      focusVisible(state.drawsFocusIndicator(semantic)) {}
namespace {
bool supports(ComponentState state, std::initializer_list<ComponentState> states) {
    return std::find(states.begin(), states.end(), state) != states.end();
}
using S = ComponentState;
std::vector<Line> block(TextMeasure& measure, std::u16string_view text, int size, int width) {
    std::vector<Line> lines;
    if (text.empty()) return lines;
    int y = 0, height = measure.lineHeight(size);
    for (auto& value : wrap(measure, text, size, width)) {
        lines.push_back({{0, y, width, height}, std::move(value)});
        y += height;
    }
    return lines;
}
int heightOf(const std::vector<Line>& lines) {
    return lines.empty() ? 0 : lines.back().bounds.y + lines.back().bounds.h - lines.front().bounds.y;
}
// A label's text laid out with its alignment, line spacing and paragraph spacing. Lines carry the box
// the glyphs occupy: x is where the (aligned) text starts, y its top inside the row, h the font's line
// height. A left-aligned line keeps the full width as before; the others are as wide as their text.
struct Flowed { std::vector<Line> lines; int height = 0; };
Flowed flowedBlock(TextMeasure& measure, std::u16string_view text, int size, int width, const Component& control, float scale, TextAlign align) {
    Flowed out;
    if (text.empty()) return out;
    const int line = measure.lineHeight(size), row = textRow(line, control.lineSpacing), gap = paragraphGap(control, scale);
    int y = 0;
    const auto pieces = wrapMarked(measure, text, size, width);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        Line placed{{0, y + (row - line) / 2, width, line}, pieces[i].text};
        if (align == TextAlign::center || align == TextAlign::right) {
            const int w = static_cast<int>(std::ceil(measure.measure(pieces[i].text, size).width));
            const int room = std::max(0, width - w);
            placed.bounds.x = align == TextAlign::center ? room / 2 : room;
            placed.bounds.w = w;
        } else if (align == TextAlign::distribute || (align == TextAlign::justify && pieces[i].soft)) {
            // Stretched to the full width, in pieces; a line with nowhere to stretch stays left-aligned.
            if (auto stretched = justifyLine(measure, pieces[i].text, size, measure.measure(pieces[i].text, size), static_cast<float>(width), control.justifyMethod))
                placed.segments = std::move(stretched->segments);
        }
        out.lines.push_back(std::move(placed));
        y += row;
        if (i + 1 < pieces.size() && !pieces[i].soft) y += gap;
    }
    out.height = y;
    return out;
}
std::u16string commandTitle(const ComponentLayout& c) {
    auto title = c.control.label;
    auto semantic = c.ui.describe(c.control.id);
    if (semantic && !semantic->enabled) title += c.presentation().disabledSuffix;
    if (semantic && semantic->executing) title += c.presentation().executingSuffix;
    return title;
}
int scaled(float scale, int value) { return std::max(1, static_cast<int>(std::lround(value * scale))); }
// A command's side padding never falls below the focus ring's thickness, so the ring cannot reach the title.
int commandPadding(const ComponentLayout& c) {
    return std::max(c.appearance.buttonPadding(c.scale, c.ui.presentation().density), focusRingWidth(c.ui, c.appearance, c.scale));
}
int commandWidth(const ComponentLayout& c) {
    return static_cast<int>(std::ceil(c.measure.measure(commandTitle(c), c.placement.textSize).width)) + 2 * commandPadding(c);
}
// The height of a button-like box around `content` pixels of title and indicator: the appearance's height, never
// less than the content plus a ring's thickness above and below it.
int commandHeight(const UiState& ui, const Appearance& appearance, float scale, int textHeight, int content) {
    return std::max(appearance.buttonHeight(textHeight, scale, ui.presentation().density), content + 2 * focusRingWidth(ui, appearance, scale));
}
// The box of a button-like component: the whole placement unless the component stacks error or conflict text below it.
Rect commandBox(const ComponentPaint& c, int indicator) {
    const auto& item = c.placement;
    const int lines = static_cast<int>(item.lines.size()), lineHeight = lines ? item.lines.front().bounds.h : 0;
    const int textHeight = lines * lineHeight;
    Rect box = item.bounds;
    box.h = std::min(box.h, commandHeight(c.ui, c.appearance, c.scale, textHeight, std::max(textHeight, indicator)));
    return box;
}
Rect offset(Rect rect, Rect origin) { return {rect.x + origin.x, rect.y + origin.y, rect.w, rect.h}; }
// The one place the lines of a message under a component are laid out: `text` wrapped to `width` pixels into `out`, the
// lines stacked from row `y` down and `inset` pixels in from the left. Returns the row below the last line.
int stackLines(ComponentLayout& c, std::vector<Line>& out, std::u16string_view text, int y, int inset, int width) {
    out = block(c.measure, text, c.placement.textSize, width);
    for (auto& line : out) { line.bounds.x += inset; line.bounds.y += y; }
    return y + heightOf(out);
}
// The source-conflict note under a component (a second line of text in the description lines), when there is a conflict.
int conflictLines(ComponentLayout& c, int y, bool conflict, int inset = 0) {
    if (!conflict) return y;
    return stackLines(c, c.placement.descriptionLines, c.presentation().conflictSuffix, y, inset, std::max(1, c.width - 2 * inset));
}
// The error message under a component, when it is invalid. Text set in from the edges keeps at least one pixel of width;
// text across the whole width takes the width as given, even none. (A text field does not use this: its appearance decides the
// rectangle of its error text, so arrangeTextField wraps the message to that rectangle and anchors it there.)
int errorLines(ComponentLayout& c, int y, const ComponentSnapshot& semantics, int inset = 0) {
    if (!semantics.invalid) return y;
    return stackLines(c, c.placement.errorLines, semantics.error, y, inset, inset > 0 ? std::max(1, c.width - 2 * inset) : c.width);
}
template<class F> void marked(ComponentPaint& c, PaintRole role, F&& paint) { c.role(role); paint(); c.role(PaintRole::content); }
void paintErrors(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    marked(c, PaintRole::errorText, [&] { for (const auto& line : lines) c.text(line.text, pixels, line.bounds.x, line.bounds.y, color); });
}
// A justified line is drawn piece by piece, every piece at its own x, so nothing (a ligature, kerning) spans the added width.
// Calls draw(text of the piece, x of the piece from the line's start) for each segment lying inside [from, to) of the line;
// the caller turns x into a pixel the way its component rounds it. Offsets are relative to the line text's start.
template<class Draw> void drawSegments(const std::vector<TextSegment>& segments, std::u16string_view lineText, unsigned from, unsigned to, Draw&& draw) {
    for (const auto& piece : segments)
        if (piece.begin >= from && piece.end <= to) draw(lineText.substr(piece.begin, piece.end - piece.begin), piece.x);
}
void paintLines(ComponentPaint& c, const std::vector<Line>& lines, int pixels, Color color) {
    for (const auto& line : lines) {
        if (line.segments.empty()) { c.text(line.text, pixels, line.bounds.x, line.bounds.y, color); continue; }
        drawSegments(line.segments, line.text, 0, static_cast<unsigned>(line.text.size()), [&](std::u16string_view piece, float x) {
            c.text(piece, pixels, line.bounds.x + static_cast<int>(std::floor(x)), line.bounds.y, color);  // a label floors, so a piece never starts past its place
        });
    }
}
// The focus ring of a text field is drawn at the edge of its surface, so whatever the surface holds (the editor and, in an
// appearance that puts it inside, the label) keeps a ring's thickness of room from every edge. Missing room is added: the
// content moves in from the sides, and the surface grows at the top and bottom, pushing what follows it down. It does this
// whether or not the field is focused, so focus never moves anything.
void keepRingRoom(TextFieldLayout& f, int ring) {
    const Rect surface = f.surface;
    if (surface.w <= 0 || surface.h <= 0) return;
    auto held = [&](const Rect& r) {
        return r.w > 0 && r.h > 0 && r.x >= surface.x && r.y >= surface.y && r.x + r.w <= surface.x + surface.w && r.y + r.h <= surface.y + surface.h;
    };
    Rect* contents[] = {&f.label, &f.editor};
    int left = ring, right = ring, top = ring, bottom = ring;  // the room each side already has, capped at what is needed
    for (const Rect* r : contents) {
        if (!held(*r)) continue;
        left = std::min(left, r->x - surface.x);
        right = std::min(right, surface.x + surface.w - (r->x + r->w));
        top = std::min(top, r->y - surface.y);
        bottom = std::min(bottom, surface.y + surface.h - (r->y + r->h));
    }
    const int addLeft = ring - left, addRight = ring - right, addTop = ring - top, addBottom = ring - bottom;
    if (!addLeft && !addRight && !addTop && !addBottom) return;
    const int grow = addTop + addBottom, below = surface.y + surface.h;
    for (Rect* r : contents)
        if (held(*r)) { r->x += addLeft; r->w = std::max(0, r->w - addLeft - addRight); r->y += addTop; }
    for (Rect* r : {&f.description, &f.error}) if (r->h > 0 && r->y >= below) r->y += grow;
    f.surface.h += grow;
    f.height += grow;
}
bool plainKey(KeyPress key) { return !key.ctrl || !key.letter; }
bool arrowKey(Key key) {
    return key == Key::left || key == Key::right || key == Key::up || key == Key::down || key == Key::home || key == Key::end;
}
template <class T> bool diverged(bool connected, const T& origin, const T& source, const T& value) { return connected && origin != source && value != source; }
// A text field the user can change now: not read only, not composing, with a source and a write timing. The edit operations
// and the editing items of the context menu both follow this one rule.
template <class Field> bool textEditable(const Component& control, const Field& field) {
    return !control.readOnly && !field.session.composing && field.writable();
}
std::u16string numberText(int value) { auto digits = std::to_string(value); return {digits.begin(), digits.end()}; }
template <class Fields> bool hasConfirmedWrite(const Fields& fields, const Component& c) {
    auto it = fields.find(c.id);
    return it != fields.end() && bool(it->second.confirmed);
}
bool isFlowVerb(Verb verb) { return verb == Verb::setTextAlign || verb == Verb::setJustifyMethod || verb == Verb::setLineSpacing || verb == Verb::setParagraphSpacing; }
// Labels and text fields share the text flow verbs; the same UiState entry points as the typed handles, so the same Status.
void flowVerbs(const VerbContext& c, std::vector<Verb>& out) {
    if (c.ui.rendererFailed()) return;
    for (auto flow : {Verb::setTextAlign, Verb::setJustifyMethod, Verb::setLineSpacing, Verb::setParagraphSpacing}) out.push_back(flow);
}
std::optional<Result> flowPerform(const PerformContext& c) {
    if (c.ref.item || !isFlowVerb(c.verb)) return std::nullopt;
    if (c.verb == Verb::setTextAlign) {
        if (auto align = std::get_if<TextAlign>(&c.argument)) return c.ui.setTextAlign(c.ref.id, *align);
    } else if (c.verb == Verb::setJustifyMethod) {
        if (auto method = std::get_if<JustifyMethod>(&c.argument)) return c.ui.setJustifyMethod(c.ref.id, *method);
    } else if (c.verb == Verb::setLineSpacing) {
        if (auto multiple = std::get_if<double>(&c.argument)) return c.ui.setLineSpacing(c.ref.id, *multiple);
    } else if (auto dip = std::get_if<int>(&c.argument)) return c.ui.setParagraphSpacing(c.ref.id, *dip);
    return c.invalidArgument();
}
// Tabs and navigation lists share their pages: the candidate, the options with their error marks, the selected page's label.
void describePages(const UiState& ui, const Component& c, ComponentSnapshot& s) {
    auto& all = UiState::Internals::tabs(ui);
    auto it = all.find(c.id);
    if (it == all.end()) return;
    const auto& t = it->second;
    s.candidate = t.pages[t.candidate].value;
    for (std::size_t i = 0; i < t.pages.size(); ++i) {
        const auto& page = t.pages[i];
        bool invalid = false;
        for (const auto& member : page.members) {
            auto memberId = ui.layoutId(member);
            if (memberId) {
                auto& errors = UiState::Internals::errors(ui);
                auto error = errors.find(*memberId);
                if (error != errors.end() && !error->second.empty()) { invalid = true; break; }
            }
        }
        s.items.push_back({page.value, page.label, page.enabled, i == t.active, invalid});
        if (i == t.active) s.value = page.label;
    }
}
// A release over the page the press armed selects it: tabs and navigation lists share their pages.
Result pageRelease(UiState& ui, const PointerRelease& release) {
    auto& pages = UiState::Internals::tabs(ui).at(release.id).pages;
    if (!release.pressedOption || release.pressedOption != release.option || *release.option >= pages.size()) return {};
    return ui.activateTab(release.id, pages[*release.option].value);
}
class Label final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::providers(ui), id); }
    void describe(const UiState&, const Component& c, ComponentSnapshot& s) const override {
        s.name = s.value; s.writingMode = c.writingMode; s.verticalDigits = c.verticalDigits;
    }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal}); }
    std::optional<std::u16string> displayText(const UiState& ui, const Component& c) const override {
        if (c.labelKind == LabelKind::error) {
            auto& errors = UiState::Internals::errors(ui);
            auto it = errors.find(c.target);
            return it == errors.end() ? std::u16string{} : it->second;
        }
        auto& providers = UiState::Internals::providers(ui);
        if (auto it = providers.find(c.id); it != providers.end()) return it->second.read();
        return std::nullopt;
    }
    bool acceptsTextProvider() const override { return true; }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { out.push_back(Verb::setWritingMode); out.push_back(Verb::setVerticalDigits); }
        ComponentType::verbs(c, out);
        if (!c.item) flowVerbs(c, out);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (!c.ref.item && (c.verb == Verb::setWritingMode || c.verb == Verb::setVerticalDigits)) {
            if (auto result = c.ui.available(c.ref.id); !result) return result;  // hidden, or behind a modal dialog
            auto handle = c.ui.layoutHandle(c.ref.id);
            const auto& label = std::get<LabelHandle>(handle);
            if (c.verb == Verb::setWritingMode) {
                if (const auto* mode = std::get_if<WritingMode>(&c.argument)) return label.setWritingMode(*mode);
            } else if (const auto* digits = std::get_if<int>(&c.argument)) return label.setVerticalDigits(*digits);
            return Result{Status::invalidValue, {}};
        }
        return flowPerform(c);
    }
    std::string_view name() const override { return "label"; }
    Role role() const override { return Role::label; }
    bool supportsTextFlow() const override { return true; }
    bool admitsDistribute() const override { return true; }
    bool supportsVerticalWriting() const override { return true; }
    bool acceptsError() const override { return false; }
    bool needsName() const override { return false; }
    bool handlesKey(const UiState&, const Component&, KeyPress) const override { return false; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<LabelHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        if (c.control.writingMode == WritingMode::vertical) { arrangeVertical(c); return; }  // justify/distribute read as top there (verticalParams)
        if (c.settle) c.ui.clearVerticalScroll(c.control.id);
        auto flowed = flowedBlock(c.measure, c.ui.text(c.control.id), p.textSize, c.width, c.control, c.scale, drawnTextAlign(c.control, c.ui.presentation()));
        p.lines = std::move(flowed.lines);
        p.bounds = {0, 0, c.width, flowed.height};
    }
    // Columns of the given height from right to left; the box is as wide as its columns or the space offered,
    // and scrolls sideways (offset counted from the right edge) when the columns are wider.
    static void arrangeVertical(ComponentLayout& c) {
        auto& p = c.placement;
        p.vertical = true;
        const int em = std::max(1, p.textSize);
        const auto params = verticalParams(c.control, c.scale);
        const int pitch = std::max(1, static_cast<int>(std::lround(c.measure.lineHeight(em) * params.lineSpacing)));
        const int bar = scaled(c.scale, 10), gap = scaled(c.scale, 4);
        const auto text = c.ui.text(c.control.id);
        auto contentWidth = [&](const std::vector<VerticalColumn>& columns) {
            int width = 0;
            for (std::size_t i = 0; i < columns.size(); ++i) width += pitch + (columns[i].lastInParagraph && i + 1 < columns.size() ? params.paragraphSpacing : 0);
            return width;
        };
        auto columns = flowVertical(c.measure, text, em, c.control.verticalDigits, c.verticalHeight);
        int content = contentWidth(columns);
        const bool sideways = content > c.width;
        // A label that scrolls can take focus and is framed by the ring at the edge of its box: the columns, the bar and
        // the reading position all keep a ring's thickness from that edge (the same room is kept focused or not).
        const int ring = sideways ? focusRingWidth(c.ui, c.appearance, c.scale) : 0;
        const int barRoom = sideways ? bar + gap : 0;
        if (sideways && c.verticalHeight > 0) {  // the bar and the ring take some of the height: wrap again for the shorter columns
            columns = flowVertical(c.measure, text, em, c.control.verticalDigits, std::max(em, c.verticalHeight - barRoom - 2 * ring));
            content = contentWidth(columns);
        }
        int tallest = 0;
        for (const auto& column : columns) tallest = std::max(tallest, column.bounds.h);
        const int area = c.verticalHeight > 0 ? std::max(0, c.verticalHeight - barRoom - 2 * ring) : tallest;
        const int width = std::min(content, c.width), view = std::max(0, width - 2 * ring), maximum = std::max(0, content - view);
        auto& state = c.ui.verticalScroll(c.control.id);
        const int offset = std::clamp(state.offset, 0, maximum);
        if (c.settle) { state.offset = offset; c.ui.setVerticalScroll(c.control.id, maximum, pitch); }
        int cursor = content;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            auto& column = columns[i];
            cursor -= pitch;
            int slack = std::max(0, area - column.bounds.h);
            int top = params.align == VerticalParams::Align::center ? slack / 2 : params.align == VerticalParams::Align::bottom ? slack : 0;
            column.bounds = {cursor - (content - view) + offset + ring, top + ring, pitch, column.bounds.h};
            p.lines.push_back({column.bounds, text.substr(column.begin, column.end - column.begin)});
            if (column.lastInParagraph && i + 1 < columns.size()) cursor -= params.paragraphSpacing;
        }
        p.bounds = {0, 0, width, area + barRoom + 2 * ring};
        auto& v = p.columnLayout;
        v.em = em; v.pitch = pitch; v.extent = content; v.maximum = maximum; v.offset = offset;
        if (maximum > 0) {
            v.track = {ring, p.bounds.h - ring - bar, view, bar};
            int thumb = std::min(view, std::max(scaled(c.scale, 24), view * view / std::max(1, content)));
            v.thumb = {ring + (view - thumb) * (maximum - offset) / maximum, v.track.y, thumb, bar};
        }
        v.columns = std::move(columns);
    }
    static void paintVertical(ComponentPaint& c, Color color) {
        const auto& v = c.placement.columnLayout;
        const auto text = c.ui.text(c.placement.id);
        auto paint = [&] {
            for (const auto& column : v.columns) {
                if (column.bounds.x + column.bounds.w <= c.placement.clip.x || column.bounds.x >= c.placement.clip.x + c.placement.clip.w) continue;
                for (const auto& piece : column.pieces) {
                    Rect cell = piece.form == VerticalForm::sideways ? Rect{column.bounds.x, column.bounds.y + piece.y, v.pitch, piece.advance}
                                                                     : Rect{column.bounds.x + (v.pitch - v.em) / 2, column.bounds.y + piece.y, v.em, v.em};
                    c.verticalText(std::u16string_view(text).substr(piece.begin, piece.end - piece.begin), piece.form, v.em, cell, color);
                }
            }
        };
        if (c.semantics.labelKind == LabelKind::error) marked(c, PaintRole::errorText, paint); else paint();
    }
    void paint(ComponentPaint& c) const override {
        if (c.focusVisible) {  // a scrolling text: the ring frames its viewport, the text slides under the band, never over it
            const auto shape = c.focusRing(c.placement.clip, scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
            c.clip(shape.inner);
        }
        if (c.placement.vertical) paintVertical(c, c.palette().text);
        else if (c.semantics.labelKind == LabelKind::error) paintErrors(c, c.placement.lines, c.placement.textSize, c.palette().text);
        else paintLines(c, c.placement.lines, c.placement.textSize, c.palette().text);
    }
    bool canFocus() const override { return false; }
    bool canTarget() const override { return true; }
    bool acceptsPointer() const override { return false; }
};
class Text final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::edits(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::readOnly, S::error, S::conflict}); }
    void adopt(UiState& ui, const Component& c) const override {
        if (!validTextControl(c)) throw std::invalid_argument("Invalid initial text");
        auto& field = UiState::Internals::edits(ui)[c.id];
        field.session.text = c.value; field.rich = c.rich;
    }
    bool focusAlwaysVisible(const ComponentSnapshot& s) const override { return !s.readOnly && s.enabled; }
    bool labelsInPlacement() const override { return true; }
    // The caret's own line, also in a multi-line field.
    std::optional<Rect> keyboardAnchor(UiState& state, const Placement& target, const std::optional<OptionValue>&, int) const override {
        if (const auto* session = state.edit(target.id)) {
            const auto caret = target.caretRect(session->visibleCaret());
            return Rect{caret.x, caret.y, 0, caret.h};
        }
        return Rect{target.bounds.x, target.bounds.y + target.textField.editor.y, 0, target.textField.editor.h};
    }
    bool hasBuiltinMenu() const override { return true; }
    void builtinMenuEntries(const UiState& ui, const Component& c, bool clipboardText, std::vector<UiState::MenuEntry>& raw) const override {
        const auto& field = UiState::Internals::edits(ui).at(c.id);
        const bool editable = textEditable(c, field);
        const bool selected = field.session.begin() != field.session.end();
        auto separator = [&] { UiState::MenuEntry entry; entry.separator = true; raw.push_back(std::move(entry)); };
        auto add = [&](const char* id, std::u16string label, char16_t key, std::u16string hint, bool enabled) {
            raw.push_back({id, std::move(label), std::move(hint), enabled, false, key});
        };
        add(menuUndo, u"復原(U)", u'U', u"Ctrl+Z", editable && !field.past.empty());
        add(menuRedo, u"重做(R)", u'R', u"Ctrl+Y", editable && !field.future.empty());
        separator();
        add(menuCut, u"剪下(T)", u'T', u"Ctrl+X", editable && selected);
        add(menuCopy, u"複製(C)", u'C', u"Ctrl+C", selected);
        add(menuPaste, u"貼上(P)", u'P', u"Ctrl+V", editable && clipboardText);
        add(menuDelete, u"刪除(D)", u'D', u"Del", editable && selected);
        separator();
        add(menuSelectAll, u"全選(A)", u'A', u"Ctrl+A", true);
        if (field.rich) {  // Windows rich-edit style: the character styles follow the editing items. U is shared with 復原(U).
            separator();
            add(menuBold, u"粗體(B)", u'B', u"Ctrl+B", editable);
            add(menuItalic, u"斜體(I)", u'I', u"Ctrl+I", editable);
            add(menuUnderline, u"底線(U)", u'U', u"Ctrl+U", editable);
        }
    }
    Result runBuiltinMenu(UiState& ui, const std::string& target, std::string_view command) const override {
        if (command == menuUndo) return ui.undo(target);
        if (command == menuRedo) return ui.redo(target);
        if (command == menuCut) return ui.cut(target);
        if (command == menuCopy) return ui.copy(target);
        if (command == menuPaste) return ui.pasteClipboard(target);
        if (command == menuDelete) return UiState::Internals::deleteSelection(ui, target);
        if (command == menuBold) return ui.toggleStyle(target, StyleAttribute::bold);
        if (command == menuItalic) return ui.toggleStyle(target, StyleAttribute::italic);
        if (command == menuUnderline) return ui.toggleStyle(target, StyleAttribute::underline);
        if (command == menuSelectAll) {
            auto& edits = UiState::Internals::edits(ui);
            auto it = edits.find(target);
            if (it == edits.end()) return {Status::invalidValue, u"元件不是文字欄位"};
            return ui.select(target, 0, static_cast<unsigned>(it->second.session.text.size()));
        }
        return {Status::notFound, u"沒有這個選單項目"};
    }
    std::optional<std::u16string> displayText(const UiState& ui, const Component& c) const override {
        auto& edits = UiState::Internals::edits(ui);
        if (auto it = edits.find(c.id); it != edits.end()) return it->second.session.text;
        return std::nullopt;
    }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        s.multiline = c.multiline;
        auto& edits = UiState::Internals::edits(ui);
        auto it = edits.find(c.id);
        if (it == edits.end()) return;
        const auto& field = it->second;
        if (c.multiline) {
            const auto& m = field.multi;
            s.scroll = TextFieldScroll{m.pendingFirst.value_or(m.first), c.visibleLines,
                                  m.placed ? static_cast<unsigned>(m.view.lines.size()) : 0};
        }
        s.sourceConflict = field.connected && (field.origin != field.sourceValue || field.originSpans != field.sourceSpans) && !UiState::Internals::sourceMatches(ui, field);
        s.rich = field.rich;
    }
    void describeAdmissions(const UiState& ui, const Component& c, const ComponentSnapshot&, std::vector<Admission>& out) const override {
        auto& edits = UiState::Internals::edits(ui);
        auto it = edits.find(c.id);
        if (it == edits.end()) return;
        if (c.multiline) out.push_back(Admission::scroll);
        out.insert(out.end(), {Admission::select, Admission::copy});
        const auto& field = it->second;
        if (textEditable(c, field)) {
            out.insert(out.end(), {Admission::edit, Admission::cut, Admission::paste});
            if (field.rich) out.push_back(Admission::style);
            if (!field.past.empty()) out.push_back(Admission::undo);
            if (!field.future.empty()) out.push_back(Admission::redo);
        }
    }
    void describeNode(const UiState& ui, const Component& c, const ComponentSnapshot&, const NodeRef& ref, NodeSnapshot& n) const override {
        auto& edits = UiState::Internals::edits(ui);
        auto it = edits.find(c.id);
        if (it == edits.end()) return;
        const auto& field = it->second;
        n.textSelection = field.session.selection; n.textRevision = field.session.revision;
        if (field.rich) {
            n.styles = field.spans;
            n.typingStyle = UiState::Internals::insertionStyle(ui, field, field.session.begin(), field.session.end());
        }
        if (!ref.item) {
            n.wordSegmentation = field.segmentation;
            n.wordSegmentationInUse = segmentationInUse(field.segmentation, UiState::Internals::dictionary(ui));
        }
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (c.item) return;
        auto add = [&](Verb verb, bool condition) { if (condition) out.push_back(verb); };
        add(Verb::setValue, c.admits(Admission::edit));
        add(Verb::select, c.admits(Admission::select));
        add(Verb::undo, c.admits(Admission::undo));
        add(Verb::redo, c.admits(Admission::redo));
        add(Verb::copy, c.admits(Admission::copy));
        add(Verb::cut, c.admits(Admission::cut));
        add(Verb::paste, c.admits(Admission::paste));
        add(Verb::scroll, c.semantics.multiline && c.admits(Admission::scroll));
        flowVerbs(c, out);
        add(Verb::setStyle, c.admits(Admission::style));
        add(Verb::setTypingStyle, c.admits(Admission::style));
        for (auto wordVerb : {Verb::setWordSegmentation, Verb::selectWord, Verb::selectParagraph, Verb::doubleClick, Verb::tripleClick})
            add(wordVerb, c.admits(Admission::select));
    }
    bool confirmPending(const UiState& ui, const Component& c) const override { return hasConfirmedWrite(UiState::Internals::edits(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        auto& ui = c.ui;
        if (auto result = flowPerform(c)) return result;
        if (c.verb == Verb::scroll) {
            if (!ui.find(c.ref.id)->multiline) return Result{Status::notSupported, {}};
            if (auto line = std::get_if<int>(&c.argument); line && *line >= 0) return ui.setScrollLine(c.ref.id, static_cast<unsigned>(*line));
            return c.invalidArgument();
        }
        if (c.verb == Verb::selectWord || c.verb == Verb::selectParagraph || c.verb == Verb::doubleClick || c.verb == Verb::tripleClick) {
            auto offset = std::get_if<int>(&c.argument);
            if (!offset || *offset < 0) return c.invalidArgument();
            const auto at = static_cast<unsigned>(*offset);
            return c.verb == Verb::selectWord ? ui.selectWord(c.ref.id, at) : c.verb == Verb::selectParagraph ? ui.selectParagraph(c.ref.id, at)
                 : c.verb == Verb::doubleClick ? ui.doubleClick(c.ref.id, at) : ui.tripleClick(c.ref.id, at);
        }
        if (c.verb == Verb::setWordSegmentation) {
            if (auto mode = std::get_if<WordSegmentation>(&c.argument)) return ui.setWordSegmentation(c.ref.id, *mode);
            return c.invalidArgument();
        }
        if (c.verb == Verb::setValue) {
            if (auto value = std::get_if<std::u16string>(&c.argument)) return ui.setValue(c.ref.id, *value);
            if (auto value = std::get_if<RichText>(&c.argument)) return ui.setRichValue(c.ref.id, *value);
            return c.invalidArgument();
        }
        if (c.verb == Verb::setStyle) {
            if (auto edit = std::get_if<StyleEdit>(&c.argument)) return ui.setStyle(c.ref.id, edit->begin, edit->end, edit->change);
            return c.invalidArgument();
        }
        if (c.verb == Verb::setTypingStyle) {
            if (auto style = std::get_if<TextStyle>(&c.argument)) return ui.setTypingStyle(c.ref.id, *style);
            return c.invalidArgument();
        }
        if (c.verb == Verb::select) {
            if (auto selection = std::get_if<Selection>(&c.argument)) return ui.select(c.ref.id, selection->anchor, selection->caret);
            return c.invalidArgument();
        }
        if (c.verb == Verb::undo) return ui.undo(c.ref.id);
        if (c.verb == Verb::redo) return ui.redo(c.ref.id);
        if (c.verb == Verb::copy) return ui.copy(c.ref.id);
        if (c.verb == Verb::cut) return ui.cut(c.ref.id);
        if (c.verb == Verb::paste) {
            if (auto value = std::get_if<std::u16string>(&c.argument)) return ui.paste(c.ref.id, *value);
            return c.invalidArgument();
        }
        return std::nullopt;
    }
    std::string_view name() const override { return "text"; }
    Role role() const override { return Role::textField; }
    bool supportsTextFlow() const override { return true; }
    bool supportsRichText() const override { return true; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<TextFieldHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        const auto& control = c.control;
        int line = c.measure.lineHeight(p.textSize);
        auto semantics = *c.ui.describe(control.id);
        auto name = semantics.name;
        if (control.readOnly) name += c.presentation().readOnlySuffix;
        if (!control.enabled) name += c.presentation().disabledSuffix;
        int caption = p.captionSize = std::max(1, p.textSize * 7 / 8);
        const int ring = focusRingWidth(c.ui, c.appearance, c.scale);
        auto probe = c.appearance.arrangeTextField(c.width, {line, line, line, line}, c.scale, c.ui.presentation().density);
        keepRingRoom(probe, ring);
        auto label = block(c.measure, name, caption, probe.label.w);
        auto descriptionText = semantics.description;
        if (semantics.sourceConflict) {
            if (!descriptionText.empty()) descriptionText += u"\n";
            descriptionText += c.presentation().conflictSuffix;
        }
        auto description = block(c.measure, descriptionText, caption, probe.description.w);
        auto error = block(c.measure, semantics.error, caption, probe.error.w);
        const int row = textRow(line, control.lineSpacing);
        // A fixed number of lines: every row, plus room for a paragraph gap between each pair of them.
        const int editorHeight = control.multiline ? row * static_cast<int>(control.visibleLines) + paragraphGap(control, c.scale) * static_cast<int>(control.visibleLines - 1) : row;
        p.textField = c.appearance.arrangeTextField(c.width, {heightOf(label), heightOf(description), heightOf(error), editorHeight}, c.scale, c.ui.presentation().density);
        keepRingRoom(p.textField, ring);
        auto anchor = [](std::vector<Line>& lines, Rect at) { for (auto& l : lines) { l.bounds.x += at.x; l.bounds.y += at.y; } };
        anchor(label, p.textField.label); anchor(description, p.textField.description); anchor(error, p.textField.error);
        p.labelLines = std::move(label); p.descriptionLines = std::move(description); p.errorLines = std::move(error);
        p.bounds = {0, 0, c.width, p.textField.height};
        const auto* session = c.ui.edit(control.id);
        if (control.multiline) { arrangeLines(c, line, row); return; }
        p.run = c.measure.measure(session->visible(), p.textSize);
        p.textTop = (row - line) / 2;
        float offset = c.ui.textOffset(control.id) * c.scale;
        if (c.ui.focused() != control.id) {
            auto anchor = c.ui.textAnchor(control.id, p.textSize);
            if (anchor) offset = p.run.x(anchor);
        }
        float room = static_cast<float>(p.textField.editor.w - static_cast<int>(std::lround(c.presentation().inset * c.scale)));
        if (c.ui.focused() == control.id) {
            float caret = p.run.x(session->visibleCaret());
            if (caret - offset > room) offset = caret - room;
            if (caret < offset) offset = caret;
        }
        offset = std::clamp(offset, 0.f, std::max(0.f, p.run.width - room));
        if (c.settle) {
            c.ui.setTextOffset(control.id, offset / c.scale);
            auto stop = std::find_if(p.run.stops.begin(), p.run.stops.end(), [offset](const auto& item) { return item.second >= offset - 0.01f; });
            c.ui.setTextAnchor(control.id, p.textSize, stop == p.run.stops.end() ? 0 : stop->first);
        }
        p.offset = offset;
        // Text that fits is placed by its alignment; text that does not fit scrolls from its start as before.
        // A single line is the last line of its paragraph, so justify leaves it left-aligned (#84).
        const auto align = drawnTextAlign(control, c.ui.presentation());
        if (p.run.width <= room && (align == TextAlign::center || align == TextAlign::right))
            p.shift = align == TextAlign::center ? std::max(0.f, (static_cast<float>(p.textField.editor.w) - p.run.width) / 2) : room - p.run.width;
    }
    // Multi-line: wrap at the edge, reserve the scrollbar's width whether or not it shows, and let
    // core decide the first visible line (scroll position, caret reveal, font-size anchor).
    static void arrangeLines(ComponentLayout& c, int line, int row) {
        auto& p = c.placement;
        auto& view = p.view;
        p.multiline = true;
        view.visible = c.control.visibleLines; view.lineHeight = line; view.rowHeight = row;
        const int bar = scaled(c.scale, 10), gap = scaled(c.scale, 4);
        const Rect editor = p.textField.editor;
        view.text = {editor.x, editor.y, std::max(0, editor.w - bar - gap), editor.h};
        view.track = {editor.x + editor.w - bar, editor.y, bar, editor.h};
        if (!c.settle) return;  // a measurement probe only needs the fixed height
        const int slack = scaled(c.scale, c.presentation().inset);  // room for a caret at a line end
        const int wrapWidth = std::max(1, view.text.w - slack);
        const auto visibleText = c.ui.edit(c.control.id)->visible();
        if (c.control.rich) {  // wrap and measure every run in its own face
            const auto styles = c.ui.visibleStyleSpans(c.control.id);
            view.lines = wrapLines(c.measure, visibleText, p.textSize, wrapWidth, &styles);
        } else view.lines = wrapLines(c.measure, visibleText, p.textSize, wrapWidth);
        const auto align = drawnTextAlign(c.control, c.ui.presentation());
        // Rows: one per line, a paragraph gap after each hard line. Alignment: centered in the text area,
        // right-aligned to the wrap edge (which keeps the caret's slack free at the right).
        const int paragraph = paragraphGap(c.control, c.scale);
        int top = 0;
        for (std::size_t i = 0; i < view.lines.size(); ++i) {
            auto& item = view.lines[i];
            item.top = top;
            top += row + (item.soft ? 0 : paragraph);
            if (align == TextAlign::center) item.shift = std::max(0.f, (static_cast<float>(view.text.w) - item.run.width) / 2);
            else if (align == TextAlign::right) item.shift = std::max(0.f, static_cast<float>(wrapWidth) - item.run.width);
            else if (align == TextAlign::justify && item.soft) {
                // Every wrapped piece but the paragraph's last is stretched to the wrap edge; its stops carry the
                // added width, so the caret, hits, selection and the input method follow the drawn pieces. A rich
                // line is also cut at every style boundary (no extra there), so each piece lies inside one style run.
                if (auto stretched = justifyLine(c.measure, std::u16string_view(visibleText).substr(item.begin, item.end - item.begin), p.textSize, item.run,
                                                  static_cast<float>(wrapWidth), c.control.justifyMethod, item.pieces.empty() ? nullptr : &item.pieces)) {
                    item.run = std::move(stretched->run);
                    item.segments = std::move(stretched->segments);
                    for (auto& piece : item.pieces) {  // a style run is drawn where its first piece starts and reaches the end of its last one
                        const TextSegment* first = nullptr; const TextSegment* last = nullptr;
                        for (const auto& segment : item.segments)
                            if (segment.begin >= piece.begin && segment.end <= piece.end) { if (!first) first = &segment; last = &segment; }
                        if (first) { piece.x = first->x; piece.width = last->x + last->width - first->x; }
                    }
                }
            }
            for (auto& piece : item.pieces) piece.x += item.shift;  // rich runs are drawn where the aligned line is (a justified line has no shift)
        }
        c.ui.settleLines(c.control.id, view, p.textSize, wrapWidth);
        const int count = static_cast<int>(view.lines.size()), visible = static_cast<int>(view.visible);
        if (count > visible) {
            const int extent = count * row, maximum = (count - visible) * row;
            const int thumb = std::min(view.track.h, std::max(scaled(c.scale, 24), view.track.h * view.track.h / std::max(1, extent)));
            view.thumb = {view.track.x, view.track.y + (view.track.h - thumb) * static_cast<int>(view.first) * row / maximum, view.track.w, thumb};
        }
    }
    static void paintLinesOf(ComponentPaint& c) {
        const auto& item = c.placement;
        const auto& view = item.view;
        const auto p = c.palette();
        const auto* session = c.ui.edit(item.id);
        const bool focused = c.semantics.focused, enabled = c.semantics.enabled;
        auto d = [&](int value) { return scaled(c.scale, value); };
        const int inset = d(c.presentation().inset);
        if (view.thumb.h) {
            c.rounded(view.track, view.track.w / 2, p.panel);
            c.rounded(view.thumb, view.thumb.w / 2, enabled ? p.outline : p.border);  // a control edge: outline when usable
        }
        c.clip(view.text);
        const auto text = session->visible();
        const unsigned first = session->begin();
        const unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
        const unsigned endLine = static_cast<unsigned>(std::min<std::size_t>(view.first + view.visible, view.lines.size()));
        for (unsigned i = view.first; i < endLine; ++i) {
            const auto& line = view.lines[i];
            const int y = view.rowY(i), row = view.rowHeight, dy = (row - view.lineHeight) / 2;  // the row; the glyphs sit in its middle
            auto xAt = [&](unsigned offset) { return view.text.x + static_cast<int>(std::lround(view.xInLine(i, offset))); };
            if (focused && last > first) {
                const unsigned a = std::max(first, line.begin), b = std::min(last, line.end);
                const bool coversBreak = !session->composing && !line.soft && line.end < text.size() && first <= line.end && last > line.end;
                const int extra = coversBreak ? d(6) : 0;  // a selected line break shows as a short block
                if (a < b || coversBreak) {
                    const int from = xAt(std::min(a, line.end)), to = xAt(std::max(a, b)) + extra;
                    if (session->composing) marked(c, PaintRole::composition, [&] { c.fill({from, y + row - dy - inset, to - from, inset}, p.accent); });
                    else marked(c, PaintRole::selection, [&] { c.fill({from, y, to - from, row}, p.selection); });
                }
            }
            const Color base = enabled ? p.text : p.muted;
            if (line.end > line.begin && line.pieces.empty()) {
                if (line.segments.empty()) c.text(std::u16string_view(text).substr(line.begin, line.end - line.begin), item.textSize, xAt(line.begin), y + dy, base);
                else drawSegments(line.segments, std::u16string_view(text).substr(line.begin, line.end - line.begin), 0, line.end - line.begin,  // a justified line, piece by piece (#84)
                                   [&](std::u16string_view piece, float x) { c.text(piece, item.textSize, view.text.x + static_cast<int>(std::lround(line.shift + x)), y + dy, base); });
            }
            // A rich line is drawn run by run: its face, its color (by name, for the theme of the moment) and its underline.
            // piece.x already includes the line's alignment shift; a justified line is drawn segment by segment (#84),
            // every segment inside one style run, and the run's underline spans the added gaps.
            for (const auto& piece : line.pieces) {
                const int left = view.text.x + static_cast<int>(std::lround(piece.x));
                Color color = base;  // a disabled field is muted whatever the colors say
                if (enabled)
                    if (auto named = c.ui.namedColor(item.id, piece.style.color)) color = *named;
                const FontStyle face{piece.style.bold, piece.style.italic};
                if (line.segments.empty())
                    c.styledText(std::u16string_view(text).substr(line.begin + piece.begin, piece.end - piece.begin), item.textSize, face, left, y + dy, color);
                else drawSegments(line.segments, std::u16string_view(text).substr(line.begin, line.end - line.begin), piece.begin, piece.end,
                                   [&](std::u16string_view part, float x) { c.styledText(part, item.textSize, face, view.text.x + static_cast<int>(std::lround(line.shift + x)), y + dy, color); });
                if (piece.style.underline) {
                    const int width = static_cast<int>(std::lround(piece.x + piece.width)) - static_cast<int>(std::lround(piece.x));
                    c.fill({left, y + dy + view.lineHeight * 5 / 6, width, std::max(1, item.textSize / 14)}, color);
                }
            }
        }
        if (focused && c.windowFocused && enabled) {
            const unsigned caretLine = view.lineOf(session->visibleCaret());
            if (caretLine >= view.first && caretLine < endLine)
                marked(c, PaintRole::caret, [&] { c.fill({item.caretX(session->visibleCaret()), item.caretY(session->visibleCaret()), d(c.presentation().thin) + 1, view.rowHeight}, p.accent); });
        }
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement;
        const auto& semantics = c.semantics;
        const auto& f = item.textField;
        auto p = c.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        auto surface = offset(f.surface, item.bounds), editor = offset(f.editor, item.bounds);
        bool focused = semantics.focused, enabled = semantics.enabled, cue = c.focusVisible;
        const int radius = d(c.appearance.radius()), thin = d(c.presentation().thin);
        const Color fill = enabled ? p.surface : p.background;
        // The edge identifies the field: outline when usable, the light divider color when disabled, error when invalid.
        const Color edge = semantics.invalid ? p.error : enabled ? p.outline : p.border;
        const PaintRole error = semantics.invalid ? PaintRole::errorIndicator : PaintRole::content;
        if (cue) {  // the focus ring replaces the edge, or the underline of the underline style
            const auto shape = c.focusRing(surface, radius, fill, semantics.invalid);
            if (c.hovered()) c.hover(shape.inner, shape.innerRadius);
        } else if (!f.underline) {
            const auto shape = c.frame(surface, radius, thin, edge, fill, error);
            if (c.hovered()) c.hover(shape.inner, shape.innerRadius);
        } else {
            c.rounded(surface, radius, fill);
            if (c.hovered()) c.hover(surface, radius);
            marked(c, error, [&] { c.fill({surface.x, surface.y + surface.h - 2 * thin, surface.w, 2 * thin}, edge); });
        }
        paintLines(c, item.labelLines, item.captionSize, semantics.invalid ? p.error : p.muted);
        paintLines(c, item.descriptionLines, item.captionSize, p.muted);
        paintErrors(c, item.errorLines, item.captionSize, p.error);
        if (item.multiline) { paintLinesOf(c); return; }
        const auto* session = c.ui.edit(item.id);
        c.clip(editor);
        auto at = [&](unsigned position) { return item.caretX(position); };
        if (focused) {
            unsigned first = session->begin();
            unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
            if (last > first) {
                if (session->composing) marked(c, PaintRole::composition, [&] { c.fill({at(first), editor.y + editor.h - item.textTop - d(c.presentation().inset), at(last) - at(first), d(c.presentation().inset)}, p.accent); });
                else marked(c, PaintRole::selection, [&] { c.fill({at(first), editor.y, at(last) - at(first), editor.h}, p.selection); });
            }
        }
        c.text(session->visible(), item.textSize, item.caretX(0), editor.y + item.textTop, enabled ? p.text : p.muted);
        if (focused && c.windowFocused && enabled) marked(c, PaintRole::caret, [&] { c.fill({at(session->visibleCaret()), editor.y, d(c.presentation().thin) + 1, editor.h}, p.accent); });
    }
    bool selectsText() const override { return true; }
    bool handlesKey(const UiState&, const Component& c, KeyPress key) const override {
        // Multi-line fields also take Enter and the vertical keys; Ctrl+Enter is left to shortcuts.
        const bool multi = c.multiline;
        // A rich field also takes Ctrl+B / I / U before any application shortcut on them.
        const bool styleKey = c.rich && (key.letter == u'B' || key.letter == u'I' || key.letter == u'U');
        return key.ctrl ? (key.letter == u'A' || key.letter == u'C' || key.letter == u'X' || key.letter == u'V' || key.letter == u'Z' || key.letter == u'Y' || styleKey ||
                           (!key.letter && (key.key == Key::left || key.key == Key::right || key.key == Key::backspace || key.key == Key::del)) ||
                           (multi && !key.letter && (key.key == Key::home || key.key == Key::end))) :
            (key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end || key.key == Key::backspace || key.key == Key::del ||
             (multi && (key.key == Key::enter || key.key == Key::up || key.key == Key::down || key.key == Key::pageUp || key.key == Key::pageDown)));
    }
    Result keyDown(UiState& ui, KeyPress key) const override { return UiState::Internals::editKey(ui, key); }
};
// Legacy toggle/navigation use the button presentation, but have no button activation behavior.
class Command : public ComponentType {
public:
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled}); }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<ButtonHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        int line = c.measure.lineHeight(p.textSize);
        int w = std::min(c.width, commandWidth(c));
        int room = std::max(1, w - 2 * commandPadding(c));
        auto lines = wrap(c.measure, commandTitle(c), p.textSize, room);
        int textHeight = static_cast<int>(lines.size()) * line;
        int h = commandHeight(c.ui, c.appearance, c.scale, textHeight, std::max(textHeight, indicatorPx(c.ui, c.appearance, c.scale)));
        p.bounds = {0, 0, w, h};
        int y = (h - textHeight) / 2;
        for (auto& text : lines) {
            int width = static_cast<int>(std::ceil(c.measure.measure(text, p.textSize).width));
            p.lines.push_back({{(w - width) / 2, y, width, line}, std::move(text)});
            y += line;
        }
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement;
        auto p = c.palette();
        auto d = [&](int value) { return scaled(c.scale, value); };
        bool focused = c.focusVisible, enabled = c.semantics.enabled;
        const int radius = d(c.appearance.radius());
        const Color fill = enabled ? p.surface : p.background;
        const Rect box = commandBox(c, indicatorPx(c.ui, c.appearance, c.scale));
        const auto shape = focused ? c.focusRing(box, radius, fill, c.semantics.invalid)
                                   : c.frame(box, radius, d(c.presentation().thin), enabled ? p.border : p.background, fill);
        if (c.hovered()) c.hover(shape.inner, shape.innerRadius);
        paintLines(c, item.lines, item.textSize, enabled ? p.text : p.muted);
    }
    int naturalWidth(const ComponentLayout& c) const override { return commandWidth(c); }
protected:
    // What the box must hold besides its title: the checkbox's square.
    virtual int indicatorPx(const UiState&, const Appearance&, float) const { return 0; }
};
class Button final : public Command {
public:
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::executing}); }
    void describeAdmissions(const UiState&, const Component&, const ComponentSnapshot& s, std::vector<Admission>& out) const override { if (!s.executing) out.push_back(Admission::activate); }
    void describeNode(const UiState& ui, const Component& c, const ComponentSnapshot&, const NodeRef& ref, NodeSnapshot& n) const override {
        if (ref.item) return;
        for (const auto& binding : UiState::Internals::shortcuts(ui)) {
            if (binding.target != c.id) continue;
            const auto& chord = binding.chord;
            if (chord.ctrl) n.acceleratorKey += u"Ctrl+";
            if (chord.shift) n.acceleratorKey += u"Shift+";
            if (chord.letter) n.acceleratorKey += chord.letter;
            else {
                switch (chord.key) {
                case Key::enter: n.acceleratorKey += u"Enter"; break;
                case Key::space: n.acceleratorKey += u"Space"; break;
                case Key::left: n.acceleratorKey += u"Left"; break;
                case Key::right: n.acceleratorKey += u"Right"; break;
                case Key::up: n.acceleratorKey += u"Up"; break;
                case Key::down: n.acceleratorKey += u"Down"; break;
                case Key::home: n.acceleratorKey += u"Home"; break;
                case Key::end: n.acceleratorKey += u"End"; break;
                case Key::pageUp: n.acceleratorKey += u"PageUp"; break;
                case Key::pageDown: n.acceleratorKey += u"PageDown"; break;
                case Key::backspace: n.acceleratorKey += u"Backspace"; break;
                case Key::del: n.acceleratorKey += u"Delete"; break;
                default: break;
                }
            }
            break;
        }
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (!c.item && c.ui.hasAction(c.control.id) && c.admits(Admission::activate)) out.push_back(Verb::invoke);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.verb == Verb::invoke) return c.ui.invoke(c.ref.id);
        return std::nullopt;
    }
    std::string_view name() const override { return "button"; }
    Role role() const override { return Role::button; }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override { return plainKey(key) && (key.key == Key::enter || key.key == Key::space); }
    Result keyDown(UiState& ui, KeyPress key) const override { return commandKeyDown(ui, key); }
    void pointerDown(UiState& ui, const Component&, std::optional<std::size_t>) const override { commandPointerDown(ui); }
};
class Checkbox final : public Command {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::bools(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::error, S::conflict}); }
    std::optional<std::u16string> displayText(const UiState& ui, const Component& c) const override {
        auto& bools = UiState::Internals::bools(ui);
        if (auto it = bools.find(c.id); it != bools.end()) return std::u16string(it->second.value ? u"true" : u"false");
        return std::nullopt;
    }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        auto& bools = UiState::Internals::bools(ui);
        if (auto it = bools.find(c.id); it != bools.end()) {
            s.checked = it->second.value;
            s.sourceConflict = diverged(it->second.connected, it->second.origin, it->second.sourceValue, it->second.value);
        }
    }
    void describeAdmissions(const UiState& ui, const Component& c, const ComponentSnapshot& s, std::vector<Admission>& out) const override {
        auto& bools = UiState::Internals::bools(ui);
        auto it = bools.find(c.id);
        if (!s.executing && (it == bools.end() || it->second.writable())) out.push_back(Admission::activate);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (!c.item && c.admits(Admission::activate)) out.push_back(Verb::toggle);
    }
    bool confirmPending(const UiState& ui, const Component& c) const override { return hasConfirmedWrite(UiState::Internals::bools(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.verb == Verb::toggle) return std::get<CheckboxHandle>(c.ui.layoutHandle(c.ref.id)).toggle();
        return std::nullopt;
    }
    std::string_view name() const override { return "checkbox"; }
    Role role() const override { return Role::checkbox; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<CheckboxHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        Command::arrange(c);
        int extra = scaled(c.scale, c.presentation().checkboxExtra);
        c.placement.bounds.w = std::min(c.width, c.placement.bounds.w + extra);
        for (auto& line : c.placement.lines) line.bounds.x += extra;
        auto semantics = *c.ui.describe(c.control.id);
        c.placement.bounds.h = conflictLines(c, c.placement.bounds.h, semantics.sourceConflict);
        c.placement.bounds.h = errorLines(c, c.placement.bounds.h, semantics);
    }
    void paint(ComponentPaint& c) const override {
        Command::paint(c);
        paintLines(c, c.placement.descriptionLines, c.placement.textSize, c.palette().muted);
        paintErrors(c, c.placement.errorLines, c.placement.textSize, c.palette().error);
        auto p = c.palette();
        int size = scaled(c.scale, c.presentation().indicator);
        int x = c.placement.bounds.x + std::max(scaled(c.scale, c.presentation().gap), focusRingWidth(c.ui, c.appearance, c.scale));  // never under the ring
        int y = c.placement.bounds.y + (commandBox(c, size).h - size) / 2;

        c.fill({x, y, size, size}, c.semantics.enabled ? p.outline : p.border);
        int inset = scaled(c.scale, c.presentation().inset);
        c.fill({x + inset, y + inset, size - inset * 2, size - inset * 2}, p.surface);
        if (c.semantics.checked.value_or(false))
            c.fill({x + 2 * inset, y + 2 * inset, size - 4 * inset, size - 4 * inset}, p.accent);
    }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override { return plainKey(key) && (key.key == Key::enter || key.key == Key::space); }
    Result keyDown(UiState& ui, KeyPress key) const override { return key.key == Key::space ? commandKeyDown(ui, key) : Result{}; }
    void pointerDown(UiState& ui, const Component&, std::optional<std::size_t>) const override { commandPointerDown(ui); }
protected:
    int indicatorPx(const UiState& ui, const Appearance& appearance, float scale) const override {
        return scaled(scale, appearance.componentPresentation(ui.presentation().density).indicator);
    }
};
class RadioGroup final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::radios(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::error, S::conflict}); }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        auto& radios = UiState::Internals::radios(ui);
        auto it = radios.find(c.id);
        if (it == radios.end()) return;
        s.sourceConflict = diverged(it->second.connected, it->second.origin, it->second.sourceValue, it->second.value);
        for (const auto& option : it->second.options) {
            bool selected = it->second.value && *it->second.value == option.value;
            s.items.push_back({option.value, option.label, option.enabled, selected});
            if (selected) s.value = option.label;
        }
    }
    void describeAdmissions(const UiState& ui, const Component& c, const ComponentSnapshot&, std::vector<Admission>& out) const override {
        auto& radios = UiState::Internals::radios(ui);
        auto it = radios.find(c.id);
        if (it == radios.end()) return;
        const auto& f = it->second;
        if (f.writable() && std::any_of(f.options.begin(), f.options.end(), [](const auto& option) { return option.enabled; }))
            out.push_back(Admission::select);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (c.item) { if (c.admits(Admission::select)) out.push_back(Verb::select); }
        else ComponentType::verbs(c, out);
    }
    bool confirmPending(const UiState& ui, const Component& c) const override { return hasConfirmedWrite(UiState::Internals::radios(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.ref.item && c.verb == Verb::select) return std::get<RadioGroupHandle>(c.ui.layoutHandle(c.ref.id)).select(*c.ref.item);
        return std::nullopt;
    }
    std::string_view name() const override { return "radio_group"; }
    Role role() const override { return Role::radioGroup; }
    std::optional<Role> itemRole() const override { return Role::radioOption; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<RadioGroupHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        // The ring frames one option row: the row is tall enough for the label and the circle plus a ring above and below,
        // and both keep a ring's thickness from the row's edges.
        const int ring = focusRingWidth(c.ui, c.appearance, c.scale), size = scaled(c.scale, c.presentation().indicator);
        int line = c.measure.lineHeight(p.textSize), row = std::max({scaled(c.scale, 24), line + scaled(c.scale, c.presentation().gap), line + 2 * ring, size + 2 * ring});
        const int circleX = std::max(scaled(c.scale, c.presentation().gapSmall), ring);
        const int labelX = std::max(scaled(c.scale, c.presentation().radioLabelOffset), circleX + size + scaled(c.scale, c.presentation().gap));
        p.lines.push_back({{0, 0, c.width, line}, semantics.name + (semantics.enabled ? u"" : c.presentation().disabledSuffix)});
        int y = line + scaled(c.scale, c.presentation().gapSmall);
        for (const auto& option : semantics.items) {
            p.options.push_back({0, y, c.width, row});
            p.lines.push_back({{labelX, y + (row - line) / 2, std::max(0, c.width - labelX - ring), line}, option.label + (option.enabled ? u"" : c.presentation().disabledSuffix)});
            y += row;
        }
        y = conflictLines(c, y, semantics.sourceConflict);
        y = errorLines(c, y, semantics);
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto p = c.palette();
        const auto& item = c.placement;
        if (!item.lines.empty()) c.text(item.lines[0].text, item.textSize, item.lines[0].bounds.x, item.lines[0].bounds.y, c.semantics.enabled ? p.text : p.muted);
        // The ring and keyboard input use the same enabled option, even when selection is disabled.
        std::optional<std::size_t> framed;
        if (c.focusVisible) {
            const auto target = focusOption(c.semantics);
            for (std::size_t i = 0; i < c.semantics.items.size() && i < item.options.size(); ++i)
                if (c.semantics.items[i].value == target) { framed = i; break; }
        }
        for (std::size_t i = 0; i < item.options.size(); ++i) {
            const auto& r = item.options[i]; const auto& option = c.semantics.items[i];
            int size = scaled(c.scale, c.presentation().indicator), inset = scaled(c.scale, c.presentation().focus);
            int x = r.x + std::max(scaled(c.scale, c.presentation().gapSmall), focusRingWidth(c.ui, c.appearance, c.scale)), y = r.y + (r.h - size) / 2;
            Rect shown = r; int shownRadius = 0;  // what hover covers: the row, or what the ring leaves of it
            if (framed == i) {
                const auto shape = c.focusRing(r, scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
                shown = shape.inner; shownRadius = shape.innerRadius;
            }
            if (c.hovered(option.value)) c.hover(shown, shownRadius);
            auto color = option.enabled && c.semantics.enabled ? p.outline : p.border;
            c.rounded({x, y, size, size}, size / 2, color);
            c.rounded({x + inset, y + inset, size - 2 * inset, size - 2 * inset}, size / 2, option.selected ? p.accent : p.surface);
            const auto& label = item.lines[i + 1];
            c.text(label.text, item.textSize, label.bounds.x, label.bounds.y, option.enabled && c.semantics.enabled ? p.text : p.muted);
        }
        paintLines(c, item.descriptionLines, item.textSize, p.muted);
        paintErrors(c, item.errorLines, item.textSize, p.error);
    }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override { return plainKey(key) && arrowKey(key.key); }
    Result keyDown(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        if (key.repeat) return {};
        int direction = key.key == Key::right || key.key == Key::down ? 1 : key.key == Key::left || key.key == Key::up ? -1 : 0;
        if (!direction) return {};
        auto& field = UiState::Internals::radios(ui).at(ui.focused());
        const auto target = focusOption(*ui.describe(ui.focused()));
        int n = static_cast<int>(field.options.size()), index = direction > 0 ? -1 : n;
        for (int i = 0; i < n; ++i) if (field.options[i].value == target) { index = i; break; }
        for (int i = index + direction; i >= 0 && i < n; i += direction)
            if (field.options[i].enabled) return UiState::Internals::selectOption(ui, ui.focused(), field.options[i].value);
        // At the boundary an unselected focus option can still be selected (including a sole enabled option).
        return target ? UiState::Internals::selectOption(ui, ui.focused(), *target) : Result{};
    }
    void pointerDown(UiState& ui, const Component& c, std::optional<std::size_t> option) const override { ui.armPointer(c.id, option); }
    Result pointerUp(UiState& ui, const PointerRelease& release) const override {
        auto& field = UiState::Internals::radios(ui).at(release.id);
        if (!release.pressedOption || release.pressedOption != release.option || *release.option >= field.options.size()) return {};
        return UiState::Internals::selectOption(ui, release.id, field.options[*release.option].value);
    }
};
class List final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::lists(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::candidate}); }
    bool hasRowCursor() const override { return true; }
    // A list row's own anchor for a keyboard-opened menu.
    std::optional<Rect> keyboardAnchor(UiState& state, const Placement& target, const std::optional<OptionValue>& item, int rowInset) const override {
        if (!item) return std::nullopt;
        auto semantic = state.describe(target.id);
        if (semantic) for (std::size_t i = 0; i < semantic->items.size() && i < target.options.size(); ++i)
            if (semantic->items[i].value == *item) {
                const auto& row = target.options[i];
                return Rect{row.x + rowInset, row.y, 0, row.h};
            }
        return std::nullopt;
    }
    // A menu is opened for a row; from the keyboard that is the current one.
    std::optional<OptionValue> menuCursor(const UiState& ui, const Component& c) const override { return UiState::Internals::lists(ui).at(c.id).cursor; }
    Result checkMenuItem(const UiState& ui, const Component& c, const OptionValue& item) const override {
        const auto& items = UiState::Internals::lists(ui).at(c.id).items;
        auto row = std::find_if(items.begin(), items.end(), [&](const auto& r) { return r.value == item; });
        if (row == items.end()) return {Status::notFound, u"沒有這個項目"};
        if (!row->enabled) return {Status::disabled, u"項目已停用"};
        return {};
    }
    void menuRowChosen(UiState& ui, const std::string& id, const OptionValue& item) const override {
        (void)std::get<ListHandle>(ui.layoutHandle(id)).select(item);
        auto& lists = UiState::Internals::lists(ui);
        if (auto list = lists.find(id); list != lists.end()) list->second.cursor = item;
    }
    std::optional<Query<OptionValue>> menuRow(const UiState& ui, const Component& c, std::size_t option) const override {
        const auto& items = UiState::Internals::lists(ui).at(c.id).items;
        if (option >= items.size()) return std::nullopt;
        if (!items[option].enabled) return Query<OptionValue>{Status::disabled, u"項目已停用", std::nullopt};
        return Query<OptionValue>{Status::ok, {}, items[option].value};
    }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        auto& lists = UiState::Internals::lists(ui);
        auto it = lists.find(c.id);
        if (it == lists.end()) return;
        const auto& f = it->second; auto value = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        s.candidate = f.cursor;
        for (const auto& item : f.items) {
            bool selected = value && *value == item.value;
            s.items.push_back({item.value, item.label, item.enabled, selected});
            if (selected) s.value = item.label;
        }
    }
    void describeAdmissions(const UiState& ui, const Component& c, const ComponentSnapshot&, std::vector<Admission>& out) const override {
        auto& lists = UiState::Internals::lists(ui);
        auto it = lists.find(c.id);
        if (it == lists.end()) return;
        const auto& f = it->second;
        if (!c.readOnly && f.source.read && f.immediate &&
            std::any_of(f.items.begin(), f.items.end(), [](const auto& item) { return item.enabled; })) out.push_back(Admission::select);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { ComponentType::verbs(c, out); return; }
        if (c.admits(Admission::select)) out.push_back(Verb::select);
        if (c.admits(Admission::focus)) out.push_back(Verb::focus);
        if (c.ui.hasMenu(c.control) && !c.ui.rendererFailed()) out.push_back(Verb::contextMenu);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (!c.ref.item) return std::nullopt;
        auto handle = c.ui.layoutHandle(c.ref.id);
        auto& list = std::get<ListHandle>(handle);
        if (c.verb == Verb::focus) return list.focus(*c.ref.item);
        if (c.verb != Verb::select) return std::nullopt;
        // A semantic selection does not move the entry cursor.
        UiState::OperationScope operation(&c.ui);
        auto& lists = UiState::Internals::lists(c.ui);
        auto previous = lists.at(c.ref.id).cursor;
        auto result = list.select(*c.ref.item);
        if (auto it = lists.find(c.ref.id); it != lists.end()) it->second.cursor = previous;
        return result;
    }
    std::string_view name() const override { return "list"; }
    Role role() const override { return Role::list; }
    std::optional<Role> itemRole() const override { return Role::listItem; }
    bool requiresTypedCreation() const override { return true; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<ListHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        // A row is tall enough for its ring, and its text starts a ring's thickness from the edge, focused or not.
        const int ring = focusRingWidth(c.ui, c.appearance, c.scale), textX = std::max(scaled(c.scale, c.presentation().gap), ring);
        int line = c.measure.lineHeight(p.textSize), row = std::max(line + scaled(c.scale, c.presentation().rowPadding), line + 2 * ring);
        p.labelLines = block(c.measure, s.name + (s.enabled ? u"" : c.presentation().disabledSuffix), p.textSize, c.width);
        int y = heightOf(p.labelLines);
        if (y) y += scaled(c.scale, c.presentation().gapSmall);
        if (s.items.empty()) {
            p.lines.push_back({{textX, y, c.width, line}, c.presentation().noItems});
            y += row;
        }
        for (const auto& item : s.items) {
            p.options.push_back({0, y, c.width, row});
            auto text = (item.selected ? c.presentation().selectedMarker : c.presentation().unselectedMarker) + item.label;
            if (!item.enabled) text += c.presentation().disabledSuffix;
            p.lines.push_back({{textX, y + (row - line) / 2, roomAfter(c.width, scaled(c.scale, c.presentation().bodySpacing), textX, scaled(c.scale, c.presentation().gap)), line}, std::move(text)});
            y += row;
        }
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette(); const auto& p = c.placement;
        paintLines(c, p.labelLines, p.textSize, c.semantics.enabled ? palette.text : palette.muted);
        if (c.semantics.items.empty()) { paintLines(c, p.lines, p.textSize, palette.muted); return; }
        for (std::size_t i = 0; i < p.options.size(); ++i) {
            const auto& item = c.semantics.items[i]; const auto& row = p.options[i];
            // Avoid shaping and submitting text for rows entirely outside the viewport.
            const auto visible = intersect(row, p.clip);
            if (visible.w <= 0 || visible.h <= 0) continue;
            // The ring frames the candidate row; the selected row keeps its selection color inside it.
            Rect shown = row; int shownRadius = 0;
            if (c.focusVisible && c.semantics.candidate == item.value) {
                const auto shape = c.focusRing(row, scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
                shown = shape.inner; shownRadius = shape.innerRadius;
            }
            if (item.selected) marked(c, PaintRole::selection, [&] { if (shownRadius > 0) c.rounded(shown, shownRadius, palette.selection); else c.fill(shown, palette.selection); });
            if (c.hovered(item.value)) c.hover(shown, shownRadius);
            const auto& line = p.lines[i];
            c.text(line.text, p.textSize, line.bounds.x, line.bounds.y, item.enabled && c.semantics.enabled ? palette.text : palette.muted);
        }
    }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override { return plainKey(key) && (key.key == Key::space || arrowKey(key.key)); }
    Result keyDown(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        auto& f = UiState::Internals::lists(ui).at(ui.focused());
        auto handle = ui.layoutHandle(ui.focused());
        auto& list = std::get<ListHandle>(handle);
        if (key.key == Key::space) return f.cursor ? list.select(*f.cursor) : Result{};
        if (!arrowKey(key.key)) return {};
        int n = static_cast<int>(f.items.size());
        int current = -1;
        for (int i = 0; i < n; ++i) if (f.cursor && f.items[i].value == *f.cursor) { current = i; break; }
        int step = key.key == Key::up || key.key == Key::left || key.key == Key::end ? -1 : 1;
        int start = key.key == Key::home ? 0 : key.key == Key::end ? n - 1 : current < 0 ? (step > 0 ? 0 : n - 1) : current + step;
        for (int i = start; i >= 0 && i < n; i += step) if (f.items[i].enabled) return list.select(f.items[i].value);
        return {};
    }
    void pointerDown(UiState& ui, const Component& c, std::optional<std::size_t> option) const override {
        const auto& items = UiState::Internals::lists(ui).at(c.id).items;
        ui.armPointer(c.id, option, option && *option < items.size() ? std::optional<OptionValue>(items[*option].value) : std::nullopt);
    }
    Result pointerUp(UiState& ui, const PointerRelease& release) const override {
        const auto& items = UiState::Internals::lists(ui).at(release.id).items;
        if (!release.pressedValue || !release.option || *release.option >= items.size() || items[*release.option].value != *release.pressedValue) return {};
        return std::get<ListHandle>(ui.layoutHandle(release.id)).select(*release.pressedValue);
    }
};
class Dropdown final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::dropdowns(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::candidate, S::error, S::conflict}); }
    bool opensPopup() const override { return true; }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        auto& dropdowns = UiState::Internals::dropdowns(ui);
        auto it = dropdowns.find(c.id);
        if (it == dropdowns.end()) return;
        const auto& f = it->second;
        s.sourceConflict = diverged(f.connected, f.origin, f.sourceValue, f.value);
        s.expanded = ui.popupOpen(c.id); s.candidate = f.candidate;
        for (const auto& option : f.options) {
            bool selected = f.value && *f.value == option.value;
            s.items.push_back({option.value, option.label, option.enabled, selected});
            if (selected) s.value = option.label;
        }
    }
    void describeAdmissions(const UiState& ui, const Component& c, const ComponentSnapshot&, std::vector<Admission>& out) const override {
        auto& dropdowns = UiState::Internals::dropdowns(ui);
        auto it = dropdowns.find(c.id);
        if (it != dropdowns.end() && it->second.writable()) out.push_back(Admission::select);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (c.item) {
            if (c.admits(Admission::select)) out.push_back(Verb::select);
            if (c.admits(Admission::focus) && c.semantics.expanded) out.push_back(Verb::focus);
            return;
        }
        ComponentType::verbs(c, out);
        if (c.admits(Admission::select)) { out.push_back(Verb::open); out.push_back(Verb::cancel); }
    }
    bool confirmPending(const UiState& ui, const Component& c) const override { return hasConfirmedWrite(UiState::Internals::dropdowns(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        auto handle = c.ui.layoutHandle(c.ref.id);
        auto& dropdown = std::get<DropdownHandle>(handle);
        if (c.ref.item && c.verb == Verb::select) return dropdown.select(*c.ref.item);
        if (c.ref.item && c.verb == Verb::focus) return dropdown.focus(*c.ref.item);
        if (!c.ref.item && c.verb == Verb::open) return dropdown.open();
        if (!c.ref.item && c.verb == Verb::cancel) return dropdown.cancel();
        return std::nullopt;
    }
    std::string_view name() const override { return "dropdown"; }
    Role role() const override { return Role::dropdown; }
    std::optional<Role> itemRole() const override { return Role::dropdownOption; }
    bool requiresTypedCreation() const override { return true; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<DropdownHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement; auto s = *c.ui.describe(c.control.id);
        // The box (and its ring) frames the name, the value row and any error text: all of it keeps a ring's thickness
        // from the edge, focused or not.
        const int ring = focusRingWidth(c.ui, c.appearance, c.scale);
        int line = c.measure.lineHeight(p.textSize), row = std::max(c.appearance.buttonHeight(line, c.scale, c.ui.presentation().density), line + 2 * ring);
        p.lines.push_back({{ring, ring, std::max(0, c.width - 2 * ring), line}, s.name + (s.enabled ? u"" : c.presentation().disabledSuffix)});
        const int valueX = std::max(scaled(c.scale, c.presentation().gap), ring);
        p.lines.push_back({{valueX, ring + line + (row - line) / 2, roomAfter(c.width, scaled(c.scale, c.presentation().bodySpacing), valueX, scaled(c.scale, c.presentation().gap)), line}, s.value.empty() ? c.presentation().noSelection : s.value});
        int y = ring + line + row;
        y = conflictLines(c, y, s.sourceConflict, ring);
        y = errorLines(c, y, s, ring);
        if (y > ring + line + row) y += ring;  // text under the value row also keeps clear of the bottom edge
        p.bounds = {0, 0, c.width, y};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        auto bounds = c.placement.bounds;
        if (c.placement.dropdownPanel) {
            c.clip(bounds);
            c.fill(bounds, palette.surface);
            if (c.semantics.items.empty()) {
                paintLines(c, c.placement.lines, c.placement.textSize, palette.muted);
                return;
            }
            for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
                const auto& o = c.semantics.items[c.placement.optionStart + i];
                const auto& r = c.placement.options[i];
                if (c.semantics.candidate && *c.semantics.candidate == o.value) marked(c, PaintRole::selection, [&] { c.fill(r, palette.selection); });
                if (c.hovered(o.value)) c.hover(r);
                const auto& line = c.placement.lines[i];
                c.text(o.enabled ? line.text : line.text + c.presentation().disabledSuffix, c.placement.textSize, line.bounds.x, line.bounds.y, o.enabled ? palette.text : palette.muted);
            }
            return;
        }
        const int radius = scaled(c.scale, c.appearance.radius());
        const auto shape = c.focusVisible ? c.focusRing(bounds, radius, palette.surface, c.semantics.invalid)
            : c.frame(bounds, radius, scaled(c.scale, c.presentation().inset), c.semantics.invalid ? palette.error : c.semantics.enabled ? palette.outline : palette.border, palette.surface,
                      c.semantics.invalid ? PaintRole::errorIndicator : PaintRole::content);
        if (c.hovered()) c.hover(shape.inner, shape.innerRadius);
        paintLines(c, c.placement.lines, c.placement.textSize, c.semantics.enabled ? palette.text : palette.muted);
        if (c.placement.lines.size() > 1) {
            const auto& line = c.placement.lines[1].bounds;
            // The marker's room is never less than a ring plus a glyph (at most one em wide), so it stays inside the ring.
            const int markerRoom = std::max(scaled(c.scale, c.presentation().dropdownMarkerSpace), focusRingWidth(c.ui, c.appearance, c.scale) + c.placement.textSize);
            c.text(c.presentation().dropdownMarker, c.placement.textSize, bounds.x + std::max(0, bounds.w - markerRoom), line.y,
                   c.semantics.enabled ? palette.text : palette.muted);
        }
        paintLines(c, c.placement.descriptionLines, c.placement.textSize, palette.muted);
        paintErrors(c, c.placement.errorLines, c.placement.textSize, palette.error);
    }
    bool handlesKey(const UiState& ui, const Component& c, KeyPress key) const override {
        return plainKey(key) && (key.key == Key::enter || key.key == Key::space || (ui.popupOpen(c.id) && (arrowKey(key.key) || key.key == Key::escape)));
    }
    Result keyDown(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        const Id& focus = ui.focused();
        auto& f = UiState::Internals::dropdowns(ui).at(focus);
        if (key.key == Key::escape) { ui.closePopup(); return {}; }
        if (key.key == Key::enter || key.key == Key::space) {
            if (key.repeat) return {};
            if (!ui.popupOpen(focus)) return std::get<DropdownHandle>(ui.layoutHandle(focus)).open();
            auto candidate = f.candidate; ui.closePopup(); return candidate ? UiState::Internals::selectDropdown(ui, focus, *candidate) : Result{};
        }
        if (!ui.popupOpen(focus)) return {};
        int direction = key.key == Key::down || key.key == Key::right ? 1 : key.key == Key::up || key.key == Key::left ? -1 : 0;
        if (key.key == Key::home) direction = 1;
        if (key.key == Key::end) direction = -1;
        if (!direction) return {};
        int n = static_cast<int>(f.options.size()), index = direction > 0 ? -1 : n;
        if (key.key != Key::home && key.key != Key::end) for (int i = 0; i < n; ++i) if (f.candidate && f.options[i].value == *f.candidate) { index = i; break; }
        f.reveal = true;
        for (int i = index + direction; i >= 0 && i < n; i += direction) if (f.options[i].enabled) { f.candidate = f.options[i].value; break; }
        ui.invalidate(); return {};
    }
    void pointerDown(UiState& ui, const Component& c, std::optional<std::size_t> option) const override {
        const auto& f = UiState::Internals::dropdowns(ui).at(c.id);
        ui.armPointer(c.id, option, ui.popupOpen(c.id) && option && *option < f.options.size() ? std::optional<OptionValue>(f.options[*option].value) : std::nullopt);
    }
    Result pointerUp(UiState& ui, const PointerRelease& release) const override {
        const auto& f = UiState::Internals::dropdowns(ui).at(release.id);
        if (release.pressedValue) {
            if (!ui.popupOpen(release.id) || !release.option || *release.option >= f.options.size() || f.options[*release.option].value != *release.pressedValue) return {};
            ui.closePopup(); return UiState::Internals::selectDropdown(ui, release.id, *release.pressedValue);
        }
        if (release.pressedOption || release.option) return {};
        return std::get<DropdownHandle>(ui.layoutHandle(release.id)).open();
    }
};
class Slider final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::numerics(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::error, S::conflict}); }
    std::optional<std::u16string> displayText(const UiState& ui, const Component& c) const override {
        auto& numerics = UiState::Internals::numerics(ui);
        if (auto it = numerics.find(c.id); it != numerics.end()) return numberText(it->second.value);
        return std::nullopt;
    }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        auto& numerics = UiState::Internals::numerics(ui);
        auto it = numerics.find(c.id);
        if (it == numerics.end()) return;
        s.sourceConflict = diverged(it->second.connected, it->second.origin, it->second.sourceValue, it->second.value);
        s.range = it->second.range; s.numericValue = it->second.value;
    }
    void describeAdmissions(const UiState& ui, const Component& c, const ComponentSnapshot&, std::vector<Admission>& out) const override {
        auto& numerics = UiState::Internals::numerics(ui);
        auto it = numerics.find(c.id);
        if (it != numerics.end() && !c.readOnly && it->second.writable()) out.push_back(Admission::edit);
    }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        ComponentType::verbs(c, out);
        if (!c.item && c.admits(Admission::edit)) out.push_back(Verb::setValue);
    }
    bool confirmPending(const UiState& ui, const Component& c) const override { return hasConfirmedWrite(UiState::Internals::numerics(ui), c); }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.verb == Verb::setValue) {
            if (auto value = std::get_if<int>(&c.argument)) return std::get<SliderHandle>(c.ui.layoutHandle(c.ref.id)).setValue(*value);
            return c.invalidArgument();
        }
        return std::nullopt;
    }
    std::string_view name() const override { return "slider"; }
    Role role() const override { return Role::slider; }
    bool requiresTypedCreation() const override { return true; }
    bool beginValueDrag(UiState& ui, const Placement& p, int x, float scale) const override { ui.sliderPointerDown(p.id, x, p.sliderTrack, scale); return true; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<SliderHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantics = *c.ui.describe(c.control.id);
        // The ring frames the title and the track, so both keep a ring's thickness from its edge; the track also leaves room
        // for the enlarged thumb at its ends. All of it is reserved whether or not the slider is focused.
        const int ring = focusRingWidth(c.ui, c.appearance, c.scale);
        const int thumb = scaled(c.scale, c.appearance.componentPresentation().bodySpacing);
        int line = c.measure.lineHeight(p.textSize), trackHeight = scaled(c.scale, c.presentation().sliderHeight);
        const int pad = std::max(scaled(c.scale, c.presentation().sliderPad), ring + (thumb + 1) / 2);
        auto title = semantics.name + u": " + semantics.value + (semantics.enabled ? u"" : c.presentation().disabledSuffix);
        if (semantics.sourceConflict) title += c.presentation().conflictSuffix;
        p.lines = block(c.measure, title, p.textSize, std::max(1, c.width - 2 * ring));
        for (auto& l : p.lines) { l.bounds.x += ring; l.bounds.y += ring; }
        int y = ring + heightOf(p.lines) + scaled(c.scale, c.presentation().gapSmall);
        p.sliderTrack = {pad, y, std::max(1, c.width - 2 * pad), trackHeight};
        y += trackHeight + ring;
        y = errorLines(c, y, semantics);
        p.bounds = {0, 0, c.width, std::max(y, line)};
    }
    void paint(ComponentPaint& c) const override {
        const auto& item = c.placement; auto palette = c.palette();
        if (c.focusVisible) {  // the ring frames the title and the track; error text stays outside it
            const int ring = focusRingWidth(c.ui, c.appearance, c.scale);
            c.focusRing({item.bounds.x, item.bounds.y, item.bounds.w, item.sliderTrack.y + item.sliderTrack.h + ring - item.bounds.y},
                         scaled(c.scale, c.appearance.radius()), c.backdrop(), c.semantics.invalid);
        }
        paintLines(c, item.lines, item.textSize, c.semantics.enabled ? palette.text : palette.muted);
        paintErrors(c, item.errorLines, item.textSize, palette.error);
        auto track = item.sliderTrack; int thickness = scaled(c.scale, c.presentation().focus);
        c.fill({track.x, track.y + track.h / 2 - thickness / 2, track.w, thickness}, c.semantics.enabled ? palette.outline : palette.border);
        if (c.semantics.range && c.semantics.numericValue) {
            const auto range = *c.semantics.range;
            double ratio = (static_cast<double>(*c.semantics.numericValue) - range.minimum) /
                           (static_cast<double>(range.maximum) - range.minimum);
            int x = track.x + static_cast<int>(std::lround(track.w * ratio));
            // While the pointer holds the thumb it sits under the pointer (kept on the current track);
            // the value is still the snapped one. Releasing or interrupting the drag slides it back to the
            // tick, and a value change from the user's own input slides it to the new tick (#75).
            if (const auto animated = c.ui.sliderAnimatedRatio(item.id)) x = track.x + static_cast<int>(std::lround(track.w * *animated));
            else if (const auto held = c.ui.sliderHeldX(item.id)) x = std::clamp(*held, track.x, track.x + track.w);
            int size = scaled(c.scale, c.focusVisible ? c.appearance.componentPresentation().bodySpacing : c.appearance.componentPresentation().rowPadding);
            Rect thumb{x - size / 2, track.y + (track.h - size) / 2, size, size};
            c.rounded(thumb, size / 2, c.semantics.enabled ? palette.accent : palette.muted);
            if (c.hovered()) c.hover(thumb, size / 2);  // anywhere on the slider marks the thumb
        }
    }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override { return plainKey(key) && arrowKey(key.key); }
    Result keyDown(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        const Id id = ui.focused();
        auto& f = UiState::Internals::numerics(ui).at(id);
        const long long lo = f.range.minimum, hi = f.range.maximum, step = f.range.step, value = f.value;
        long long target = value;
        switch (key.key) {
        case Key::home: target = lo; break;
        case Key::end: target = hi; break;
        case Key::right: case Key::up:
            if (value < hi) target = std::min(hi, lo + ((value - lo) / step + 1) * step);
            break;
        case Key::left: case Key::down:
            if (value > lo) target = lo + ((value - lo - 1) / step) * step;
            break;
        default: return {};
        }
        const double from = UiState::Internals::sliderDisplayRatio(ui, id);
        auto result = ui.sliderSetValue(id, static_cast<int>(target));
        UiState::Internals::sliderStartedMoving(ui, id, from, static_cast<int>(value));
        return result;
    }
    void pointerDown(UiState& ui, const Component& c, std::optional<std::size_t> option) const override { ui.armPointer(c.id, option); }
    Result pointerUp(UiState&, const PointerRelease&) const override { return {}; }
};
class Progress final : public ComponentType {
public:
    void retire(UiState& ui, const Id& id) const override { UiState::Internals::retire(ui, UiState::Internals::progresses(ui), id); }
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal}); }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override {
        auto& progresses = UiState::Internals::progresses(ui);
        auto it = progresses.find(c.id);
        if (it == progresses.end()) return;
        s.readOnly = true;
        if (it->second.source.read) {
            int displayed = std::clamp(it->second.source.read(), 0, 100);
            s.numericValue = displayed;
            s.value = numberText(displayed) + u"%";
        }
    }
    std::string_view name() const override { return "progress"; }
    Role role() const override { return Role::progress; }
    bool requiresTypedCreation() const override { return true; }
    bool needsName() const override { return false; }
    bool handlesKey(const UiState&, const Component&, KeyPress) const override { return false; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<ProgressHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto& p = c.placement;
        auto semantic = *c.ui.describe(c.control.id);
        auto title = semantic.name + (semantic.value.empty() ? u"" : u": " + semantic.value);
        p.lines = block(c.measure, title, p.textSize, c.width);
        int y = heightOf(p.lines) + scaled(c.scale, c.presentation().gapSmall);
        p.progressTrack = {0, y, std::max(0, c.width), scaled(c.scale, c.presentation().rowPadding)};
        p.progressFill = p.progressTrack;
        p.progressFill.w = static_cast<int>(static_cast<long long>(p.progressTrack.w) * semantic.numericValue.value_or(0) / 100);
        p.bounds = {0, 0, c.width, y + p.progressTrack.h};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        paintLines(c, c.placement.lines, c.placement.textSize, palette.text);
        // The track is the only cue of the bar's extent when no value text is shown; with text it stays decorative.
        c.fill(c.placement.progressTrack, c.semantics.value.empty() ? palette.outline : palette.border);
        if (c.placement.progressFill.w > 0) c.fill(c.placement.progressFill, palette.accent);
    }
    bool canFocus() const override { return false; }
    bool acceptsPointer() const override { return false; }
};
class Tabs final : public ComponentType {
public:
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected, S::candidate, S::error}); }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override { describePages(ui, c, s); }
    void describeAdmissions(const UiState&, const Component&, const ComponentSnapshot& s, std::vector<Admission>& out) const override { if (!s.executing) out.push_back(Admission::activate); }
    void describeNode(const UiState&, const Component&, const ComponentSnapshot&, const NodeRef&, NodeSnapshot& n) const override { n.selectionRequired = true; }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { ComponentType::verbs(c, out); return; }
        if (c.admits(Admission::focus) || c.admits(Admission::select)) out.push_back(Verb::select);
        if (c.admits(Admission::focus)) out.push_back(Verb::focus);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.ref.item && c.verb == Verb::select) return std::get<TabsHandle>(c.ui.layoutHandle(c.ref.id)).activate(*c.ref.item);
        if (c.ref.item && c.verb == Verb::focus) return std::get<TabsHandle>(c.ui.layoutHandle(c.ref.id)).focus(*c.ref.item);
        return std::nullopt;
    }
    std::string_view name() const override { return "tabs"; }
    Role role() const override { return Role::tabs; }
    std::optional<Role> itemRole() const override { return Role::tabItem; }
    bool requiresTypedCreation() const override { return true; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<TabsHandle>(c); }
    void arrange(ComponentLayout& c) const override {
        auto semantics = *c.ui.describe(c.control.id);
        // A tab is tall enough for its ring, and its label starts a ring's thickness from the edge, focused or not.
        const int ring = focusRingWidth(c.ui, c.appearance, c.scale), textX = std::max(scaled(c.scale, c.presentation().gapSmall), ring);
        const int lineHeight = c.measure.lineHeight(c.placement.textSize);
        int height = std::max(c.appearance.buttonHeight(lineHeight, c.scale, c.ui.presentation().density), lineHeight + 2 * ring);
        c.placement.labelLines = block(c.measure, semantics.name, c.placement.textSize, c.width);
        int top = heightOf(c.placement.labelLines);
        if (top) top += scaled(c.scale, c.presentation().gapSmall);
        int count = static_cast<int>(semantics.items.size());
        for (int i = 0; i < count; ++i) {
            int left = c.width * i / count, right = c.width * (i + 1) / count;
            c.placement.options.push_back({left, top, right - left, height});
            const auto& option = semantics.items[static_cast<std::size_t>(i)];
            std::u16string prefix = option.invalid ? c.presentation().invalidTabMarker : u"";
            if (!option.enabled) prefix += c.presentation().disabledTabMarker;
            auto label = option.label;
            const int room = roomAfter(right - left, scaled(c.scale, c.presentation().gap), textX, scaled(c.scale, c.presentation().gapSmall));
            while (!label.empty() && c.measure.measure(prefix + label, c.placement.textSize).width > room) {
                if (label.back() >= 0xdc00 && label.back() <= 0xdfff && label.size() > 1) label.pop_back();
                label.pop_back();
            }
            label = prefix + label;
            c.placement.lines.push_back({{left + textX, top + (height - c.measure.lineHeight(c.placement.textSize)) / 2,
                                         room, height}, std::move(label)});
        }
        c.placement.bounds = {0, 0, c.width, top + height};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        paintLines(c, c.placement.labelLines, c.placement.textSize, palette.text);
        for (std::size_t i = 0; i < c.placement.options.size(); ++i) {
            const auto& option = c.semantics.items[i]; auto rect = c.placement.options[i];
            // The ring frames the candidate tab; the tab keeps its fill, and the selected tab its underline, inside the ring.
            const Color tabFill = option.selected ? palette.selection : palette.surface;
            ComponentPaint::RingShape shape{rect, 0, 0};
            const bool framed = c.focusVisible && c.semantics.candidate && *c.semantics.candidate == option.value;
            if (framed) shape = c.focusRing(rect, scaled(c.scale, c.appearance.radius()), tabFill, c.semantics.invalid);
            marked(c, option.selected ? PaintRole::selection : PaintRole::content, [&] { if (framed) c.rounded(shape.inner, shape.innerRadius, tabFill); else c.fill(rect, tabFill); });
            if (c.hovered(option.value)) c.hover(shape.inner, shape.innerRadius);
            if (option.selected) {
                const int thickness = scaled(c.scale, c.presentation().focus);
                marked(c, PaintRole::selection, [&] { c.fill({shape.inner.x, shape.inner.y + shape.inner.h - thickness, shape.inner.w, thickness}, palette.accent); });
            }
            const auto& line = c.placement.lines[i];
            marked(c, option.invalid ? PaintRole::errorText : PaintRole::content, [&] { c.text(line.text, c.placement.textSize, line.bounds.x, line.bounds.y, option.enabled ? palette.text : palette.muted); });
        }
    }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override {
        return plainKey(key) && (key.key == Key::enter || key.key == Key::space || key.key == Key::left || key.key == Key::right);
    }
    Result keyDown(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        auto& t = UiState::Internals::tabs(ui).at(ui.focused());
        if (key.key == Key::left || key.key == Key::right) {
            int dir = key.key == Key::left ? -1 : 1;
            int i = static_cast<int>(t.candidate);
            for (std::size_t n = 0; n < t.pages.size(); ++n) {
                i = (i + dir + static_cast<int>(t.pages.size())) % static_cast<int>(t.pages.size());
                if (t.pages[static_cast<std::size_t>(i)].enabled) { t.candidate = static_cast<std::size_t>(i); break; }
            }
            ui.invalidate();
            return {};
        }
        return commandKeyDown(ui, key);
    }
    void pointerDown(UiState& ui, const Component& c, std::optional<std::size_t> option) const override { ui.armPointer(c.id, option); }
    Result pointerUp(UiState& ui, const PointerRelease& release) const override { return pageRelease(ui, release); }
};
class NavigationList final : public ComponentType {
public:
    bool supportsState(ComponentState state) const override { return supports(state, {S::normal, S::disabled, S::selected}); }
    void describe(const UiState& ui, const Component& c, ComponentSnapshot& s) const override { describePages(ui, c, s); }
    void describeNode(const UiState&, const Component&, const ComponentSnapshot&, const NodeRef&, NodeSnapshot& n) const override { n.selectionRequired = true; }
    void verbs(const VerbContext& c, std::vector<Verb>& out) const override {
        if (!c.item) { ComponentType::verbs(c, out); return; }
        if (c.admits(Admission::focus) || c.admits(Admission::select)) out.push_back(Verb::select);
        if (c.admits(Admission::focus)) out.push_back(Verb::focus);
        if (c.ui.hasMenu(c.control) && !c.ui.rendererFailed()) out.push_back(Verb::contextMenu);
    }
    std::optional<Result> perform(const PerformContext& c) const override {
        if (c.ref.item && c.verb == Verb::select) return std::get<NavigationListHandle>(c.ui.layoutHandle(c.ref.id)).select(*c.ref.item);
        if (c.ref.item && c.verb == Verb::focus) return std::get<NavigationListHandle>(c.ui.layoutHandle(c.ref.id)).focus(*c.ref.item);
        return std::nullopt;
    }
    std::string_view name() const override { return "navigation_list"; }
    Role role() const override { return Role::list; }
    std::optional<Role> itemRole() const override { return Role::listItem; }
    ComponentHandle handle(UiState& ui, const Component& c) const override { return ui.typedHandle<NavigationListHandle>(c); }
    int naturalWidth(const ComponentLayout& c) const override {
        auto semantic = c.ui.describe(c.control.id);
        int width = 0;
        for (const auto& option : semantic->items) width = std::max(width, static_cast<int>(c.measure.measure(option.label, c.placement.textSize).width));
        return std::max(scaled(c.scale, 160), width + scaled(c.scale, 48));
    }
    void arrange(ComponentLayout& c) const override {
        auto semantic = *c.ui.describe(c.control.id);
        int line = c.measure.lineHeight(c.placement.textSize);
        bool material = c.appearance.name() == "Material-inspired";
        int padding = material ? 32 : 16;
        if (c.ui.presentation().density == Density::compact) padding /= 2;
        int height = std::max({scaled(c.scale, 24), line + scaled(c.scale, padding), line + 2 * focusRingWidth(c.ui, c.appearance, c.scale)});
        for (std::size_t i = 0; i < semantic.items.size(); ++i) {
            int y = static_cast<int>(i) * height;
            c.placement.options.push_back({0, y, c.width, height});
            auto label = semantic.items[i].label;
            if (!semantic.items[i].enabled) label += u"\uff08\u505c\u7528\uff09";
            c.placement.lines.push_back({{scaled(c.scale, 16), y + (height-line)/2, c.width-scaled(c.scale, 32), line}, std::move(label)});
        }
        c.placement.bounds = {0, 0, c.width, height * static_cast<int>(semantic.items.size())};
    }
    void paint(ComponentPaint& c) const override {
        auto palette = c.palette();
        bool material = c.appearance.name() == "Material-inspired";
        c.fill(c.placement.bounds, material ? palette.surface : palette.panel);
        if (!material) c.fill({c.placement.bounds.x+c.placement.bounds.w-scaled(c.scale,1), c.placement.bounds.y, scaled(c.scale,1), c.placement.bounds.h}, palette.border);
        for (std::size_t i=0; i<c.placement.options.size(); ++i) {
            const auto& option = c.semantics.items[i]; auto rect = c.placement.options[i];
            const int radius = material ? rect.h / 2 : scaled(c.scale, 6);
            ComponentPaint::RingShape shape{rect, radius, radius};
            // Programmatic focus can frame a page without selecting it.
            if (c.focusVisible && c.semantics.candidate == option.value)
                shape = c.focusRing(rect, radius, material ? palette.surface : palette.panel, c.semantics.invalid);
            if (option.selected) {
                marked(c, PaintRole::selection, [&] {
                    c.rounded(shape.inner, shape.innerRadius, palette.selection);
                    if (!material) c.fill({shape.inner.x, shape.inner.y, scaled(c.scale, 3), shape.inner.h}, palette.accent);
                });
            }
            if (c.hovered(option.value)) c.hover(shape.inner, shape.innerRadius);
            const auto& line = c.placement.lines[i];
            c.text(line.text, c.placement.textSize, line.bounds.x, line.bounds.y, option.enabled && c.semantics.enabled ? palette.text : palette.muted);
        }
    }
    bool handlesKey(const UiState&, const Component&, KeyPress key) const override {
        return plainKey(key) && (key.key == Key::up || key.key == Key::down || key.key == Key::home || key.key == Key::end);
    }
    Result keyDown(UiState& ui, KeyPress key) const override {
        UiState::OperationScope operation(&ui);
        if (auto r = ui.available(ui.focused()); !r) return r;
        auto& t = UiState::Internals::tabs(ui).at(ui.focused());
        int index = static_cast<int>(t.candidate), last = static_cast<int>(t.pages.size()) - 1;
        if (key.key == Key::home) index = -1;
        else if (key.key == Key::end) index = last + 1;
        else if (key.key != Key::up && key.key != Key::down) return {};
        int direction = key.key == Key::up || key.key == Key::end ? -1 : 1;
        for (index += direction; index >= 0 && index <= last; index += direction)
            if (t.pages[static_cast<std::size_t>(index)].enabled) return ui.activateTab(ui.focused(), t.pages[static_cast<std::size_t>(index)].value);
        return {};
    }
    bool ignoresPress(const UiState& ui, const Component& c, std::optional<std::size_t> option) const override {
        if (!option) return false;
        const auto& pages = UiState::Internals::tabs(ui).at(c.id).pages;
        return *option >= pages.size() || !pages[*option].enabled;
    }
    void pointerDown(UiState& ui, const Component& c, std::optional<std::size_t> option) const override { ui.armPointer(c.id, option); }
    Result pointerUp(UiState& ui, const PointerRelease& release) const override { return pageRelease(ui, release); }
};
NavigationList navigationList;
Tabs tabs;
Progress progress;
Slider slider;
List list;
Dropdown dropdown;
RadioGroup radioGroup;
Checkbox checkbox;
Label label;
Text text;
Button button;
Command command;
const std::map<ComponentKind, const ComponentType*>& types() {
    static const std::map<ComponentKind, const ComponentType*> registry{
        {ComponentKind::label, &label}, {ComponentKind::textField, &text}, {ComponentKind::button, &button},
        {ComponentKind::checkbox, &checkbox}, {ComponentKind::radioGroup, &radioGroup}, {ComponentKind::slider, &slider}, {ComponentKind::dropdown, &dropdown}, {ComponentKind::list, &list}, {ComponentKind::tabs, &tabs}, {ComponentKind::progress, &progress}, {ComponentKind::navigationList, &navigationList}};
    return registry;
}
// Test registrations belong to the thread owning their UI. Built-in lookups remain immutable
// and can be used concurrently by independent UI environments.
std::map<ComponentKind, const ComponentType*>& testTypes() {
    thread_local std::map<ComponentKind, const ComponentType*> registry;
    return registry;
}
}
int focusRingWidth(const UiState& ui, const Appearance& appearance, float scale) {
    const auto& settings = ui.presentation();
    return scaled(scale, settings.focusRingWidth.value_or(appearance.componentPresentation(settings.density).focus));
}
int ComponentPaint::ringRadius(int own) const {
    const auto& radius = ui.presentation().focusRingRadius;
    return radius ? static_cast<int>(std::lround(*radius * scale)) : own;
}
Color ComponentPaint::backdrop() const { return placement.dialog ? palette().surface : palette().background; }
ComponentPaint::RingShape ComponentPaint::ringShape(Rect outer, int radius, int width) {
    RingShape shape;
    shape.outerRadius = std::clamp(radius, 0, std::max(0, std::min(outer.w, outer.h) / 2));  // a radius that is too large is a capsule
    shape.inner = {outer.x + width, outer.y + width, std::max(0, outer.w - 2 * width), std::max(0, outer.h - 2 * width)};
    shape.innerRadius = std::max(0, shape.outerRadius - width);
    return shape;
}
ComponentPaint::RingShape ComponentPaint::frame(Rect outer, int radius, int width, Color edge, Color fill, PaintRole frameRole) {
    const auto shape = ringShape(outer, radius, width);
    marked(*this, frameRole, [&] { rounded(outer, shape.outerRadius, edge); });
    rounded(shape.inner, shape.innerRadius, fill);
    return shape;
}
ComponentPaint::RingShape ComponentPaint::focusRing(Rect outer, int ownRadius, Color fill, bool invalid) {
    const auto shape = ringShape(outer, ringRadius(ownRadius), focusRingWidth(ui, appearance, scale));
    const auto marks = invalid ? PaintRole::focusIndicator | PaintRole::errorIndicator : PaintRole::focusIndicator;
    marked(*this, marks, [&] {
        rounded(outer, shape.outerRadius, invalid ? palette().error : palette().focusColor());
        rounded(shape.inner, shape.innerRadius, fill);
    });
    return shape;
}
Result commandKeyDown(UiState& ui, KeyPress key) {
    UiState::OperationScope operation(&ui);
    if (key.repeat) return {};
    if (key.key == Key::enter) return ui.invoke(ui.focused());
    if (key.key == Key::space) ui.armKey(ui.focused());
    return {};
}
void commandPointerDown(UiState& ui) {
    UiState::OperationScope operation(&ui);
    ui.armPointer(ui.focused());
}
bool VerbContext::admits(Admission admission) const {
    return std::find(admissions.begin(), admissions.end(), admission) != admissions.end();
}
Result ComponentType::pointerUp(UiState& ui, const PointerRelease& release) const { return ui.invoke(release.id); }
void ComponentType::verbs(const VerbContext& c, std::vector<Verb>& out) const {
    if (c.item) return;
    if (c.ui.hasMenu(c.control) && !c.ui.rendererFailed()) out.push_back(Verb::contextMenu);
    if (c.admits(Admission::focus)) out.push_back(Verb::focus);
}
const ComponentType* findComponentType(ComponentKind kind) {
    auto test = testTypes().find(kind);
    if (test != testTypes().end()) return test->second;
    auto it = types().find(kind);
    return it == types().end() ? nullptr : it->second;
}
const ComponentType& componentType(ComponentKind kind) {
    if (const auto* type = findComponentType(kind)) return *type;
    throw std::invalid_argument("Unknown component type");
}
ScopedComponentType::ScopedComponentType(ComponentKind kind, const ComponentType& type) : kind_(kind) {
    if (types().contains(kind) || !testTypes().emplace(kind, &type).second)
        throw std::invalid_argument("Component type already registered");
}
ScopedComponentType::~ScopedComponentType() { testTypes().erase(kind_); }
}
