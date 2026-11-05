#include "core/editor/Representations.h"

#include "core/lang/StyleRegistry.h"

#include <Scintilla.h>

#include <array>
#include <cstring>

namespace npp::core {

namespace {

using CharRow = std::array<const char*, 3>; // UTF-8 char, abbreviation, codepoint

constexpr CharRow kCcUniEolChars[] = {
#define NPP_CCUNIEOL(c, abbr, cp) {c, abbr, cp},
#define NPP_NPC(c, abbr, cp)
#include "core/editor/NonPrintingChars.inc"
#undef NPP_CCUNIEOL
#undef NPP_NPC
};

constexpr CharRow kNonPrintingChars[] = {
#define NPP_CCUNIEOL(c, abbr, cp)
#define NPP_NPC(c, abbr, cp) {c, abbr, cp},
#include "core/editor/NonPrintingChars.inc"
#undef NPP_CCUNIEOL
#undef NPP_NPC
};

static_assert(std::size(kCcUniEolChars) == 64 && std::size(kNonPrintingChars) == 49);

constexpr char kZwsp[] = "\xE2\x80\x8B";

intptr_t ptr(const char* s) { return reinterpret_cast<intptr_t>(s); }
uintptr_t uptr(const char* s) { return reinterpret_cast<uintptr_t>(s); }

intptr_t alphaColour(int rgb) // 0xRRGGBB -> Scintilla 0xAABBGGRR with alpha (DirectWrite needs it)
{
    return static_cast<intptr_t>(static_cast<unsigned>(Style::scintillaColour(rgb)) | 0xFF000000u);
}

} // namespace

void RepresentationOptions::colorsFrom(const StyleRegistry& styles)
{
    if (const Style* s = styles.globalStyleByName("EOL custom color"); s && s->fg >= 0)
        eolColor = s->fg;
    if (const Style* s = styles.globalStyleByName("Non-printing characters custom color"); s && s->fg >= 0)
        npcColor = s->fg;
}

const char* nonPrintingRepresentation(const char* utf8Char, int mode)
{
    if (mode < 1 || mode > 2)
        return "";
    for (const CharRow& r : kNonPrintingChars)
        if (std::strcmp(r[0], utf8Char) == 0)
            return r[static_cast<size_t>(mode)];
    for (const CharRow& r : kCcUniEolChars)
        if (std::strcmp(r[0], utf8Char) == 0)
            return r[static_cast<size_t>(mode)];
    return "";
}

void applyRepresentations(const IEditor& e, const RepresentationOptions& o)
{
    e.send(SCI_CLEARALLREPRESENTATIONS);

    // C0 / C1 / Unicode EOL
    if (o.ccUniEolShow) {
        const size_t mode = static_cast<size_t>(o.npcIncludeCcUniEol && o.npcMode == 2 ? 2 : 1);
        for (const CharRow& r : kCcUniEolChars)
            e.send(SCI_SETREPRESENTATION, uptr(r[0]), ptr(r[mode]));
    } else {
        for (const CharRow& r : kCcUniEolChars) {
            e.send(SCI_SETREPRESENTATION, uptr(r[0]), ptr(kZwsp));
            e.send(SCI_SETREPRESENTATIONAPPEARANCE, uptr(r[0]), SC_REPRESENTATION_PLAIN);
        }
    }

    // Non-printing characters
    if (o.npcShow) {
        const size_t mode = static_cast<size_t>(o.npcMode == 2 ? 2 : 1);
        for (const CharRow& r : kNonPrintingChars)
            e.send(SCI_SETREPRESENTATION, uptr(r[0]), ptr(r[mode]));
    }
    if (o.npcCustomColor) {
        const intptr_t colour = alphaColour(o.npcColor);
        const intptr_t appearance = SC_REPRESENTATION_BLOB | SC_REPRESENTATION_COLOUR;
        if (o.npcShow)
            for (const CharRow& r : kNonPrintingChars) {
                e.send(SCI_SETREPRESENTATIONCOLOUR, uptr(r[0]), colour);
                e.send(SCI_SETREPRESENTATIONAPPEARANCE, uptr(r[0]), appearance);
            }
        if (o.ccUniEolShow && o.npcIncludeCcUniEol)
            for (const CharRow& r : kCcUniEolChars) {
                e.send(SCI_SETREPRESENTATIONCOLOUR, uptr(r[0]), colour);
                e.send(SCI_SETREPRESENTATIONAPPEARANCE, uptr(r[0]), appearance);
            }
    }

    // CR / LF appearance
    intptr_t appearance = SC_REPRESENTATION_BLOB;
    switch (o.eolMode) {
    case 0: appearance = SC_REPRESENTATION_PLAIN; break;
    case 2: appearance = SC_REPRESENTATION_PLAIN | SC_REPRESENTATION_COLOUR; break;
    case 3: appearance = SC_REPRESENTATION_BLOB | SC_REPRESENTATION_COLOUR; break;
    default: break;
    }
    for (const char* c : {"\r", "\n"}) {
        e.send(SCI_SETREPRESENTATIONCOLOUR, uptr(c), alphaColour(o.eolColor));
        e.send(SCI_SETREPRESENTATIONAPPEARANCE, uptr(c), appearance);
    }
}

} // namespace npp::core
