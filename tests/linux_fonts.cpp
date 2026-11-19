#include "isoduo/fonts.hpp"
#include "check.hpp"
#include "text.hpp"
#include <algorithm>

using namespace isoduo;

namespace {
void resolveFixture() {
    const auto& catalog = systemFontCatalog();
    test::check(catalog.contains(u"Ubuntu"), "The regular-only local family is selectable");
    test::check(!catalog.contains(u"Google Symbols"), "A subset without letters and digits is excluded from the picker");
    for (const auto& family : catalog.families())
        test::check(catalog.contains(family.key) && !family.displayName.empty(), "Every picker key has a display name");

    PresentationSettings settings;
    settings.fontFamily = u"Ubuntu";
    const auto known = catalog.resolve(settings);
    test::check(known.requested == u"Ubuntu" && known.requestedAvailable && known.primary == u"Ubuntu",
        "Resolving the local family retains the requested key");
    test::check(std::ranges::find(known.fallback, u"Sree Krushnadevaraya") != known.fallback.end(),
        "The installed Telugu family is in the ordered fallback");

    settings.fontFamily = u"Font absent from this fixture";
    const auto missing = catalog.resolve(settings);
    test::check(missing.requested == settings.fontFamily && !missing.requestedAvailable && missing.primary == u"Ubuntu",
        "The missing request is retained and resolves to the configured default");
}

void shapeFallback() {
    detail::Text text;
    const auto mixed = text.shape(u"A\u0c2c", 24);
    test::check(mixed.missing == 0 && mixed.glyphs.size() == 2 &&
        mixed.glyphs[0].faceId != mixed.glyphs[1].faceId, "Telugu falls back to the installed Telugu family");
}

void facesAndSynthesis() {
    detail::Text text;
    const auto& catalog = systemFontCatalog();
    for (auto family : {u"Roboto Flex", u"Ubuntu"}) {
        PresentationSettings settings;
        settings.fontFamily = family;
        text.setFontResolution(catalog.resolve(settings), {});
        const auto regular = text.shape(u"A", 24).glyphs.at(0);
        for (unsigned look : {1u, 2u, 3u}) {
            const auto styled = text.shape(u"A", 24, {bool(look & 1), bool(look & 2)}).glyphs.at(0);
            if (settings.fontFamily == u"Roboto Flex") {
                test::check(styled.id && styled.slot == look && styled.synth == 0 && styled.faceId != regular.faceId,
                    "Named variable instances supply real bold, italic and bold italic");
            } else {
                test::check(styled.id && styled.synth == look && styled.faceId == regular.faceId,
                    "A regular-only font stays in the family and synthesizes the absent style");
            }
        }
    }
}

void missingCharacter() {
    int reports = 0;
    detail::Text text(detail::systemFontFile, std::function<void(const RenderDiagnostic&)>([&](const auto& event) {
        if (event.code == "text.glyph_missing" && event.nativeCode == 0x10ffff) ++reports;
    }));
    text.setFontResolution(systemFontCatalog().resolve({}), {});
    const auto missing = text.shape(u"\U0010ffff", 24);
    test::check(missing.missing == 1 && missing.width > 0, "An absent code point still occupies space");
    const Color color{172, 57, 93};
    detail::Canvas canvas(64, 64, {255, 255, 255});
    text.draw(canvas, missing, 0, 24, color);
    const auto offset = ((24 - 19) * canvas.width) * 4;
    test::check(canvas.pixels[offset] == color.r && canvas.pixels[offset + 1] == color.g &&
        canvas.pixels[offset + 2] == color.b, "The replacement box uses the requested text color");
    text.shape(u"\U0010ffff", 32, {.bold = true});
    test::check(reports == 1, "The missing code point is reported once across sizes and styles");
}
}

int main() {
    const test::Case cases[] = {
        {"System catalog resolves local, fallback and unavailable fonts", resolveFixture},
        {"The resolved fallback reaches shaped text", shapeFallback},
        {"Real style faces and synthetic styles stay in the chosen family", facesAndSynthesis},
        {"An absent code point draws a colored box and reports once", missingCharacter},
    };
    return test::run("Linux system fonts", cases);
}
