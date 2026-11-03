// Character representations: EOL appearance, non-printing characters (View > Show Symbol >
// Show Non-Printing Characters) and C0/C1/Unicode EOL (port of upstream ScintillaEditView
// setCRLF / setNpcAndCcUniEOL / showNpc / showCcUniEol).
#pragma once

#include "core/editor/IEditor.h"

namespace npp::core {

class StyleRegistry;

struct RepresentationOptions {
    int  eolMode = 1;              // GuiConfig::eolMode: 0 plain, 1 rounded rect, 2 plain+colour, 3 rounded rect+colour
    int  eolColor = 0xC0C0C0;      // 0xRRGGBB ("EOL custom color")
    bool npcShow = false;
    int  npcMode = 1;              // 1 abbreviation, 2 codepoint
    bool npcCustomColor = false;
    bool npcIncludeCcUniEol = false;
    bool ccUniEolShow = true;
    int  npcColor = 0xC0C0C0;      // 0xRRGGBB ("Non-printing characters custom color")

    // Fills the colours from the "EOL custom color" / "Non-printing characters custom color" widget styles
    void colorsFrom(const StyleRegistry& styles);
};

// Resets all representations then applies the options (same end state as upstream's toggles).
void applyRepresentations(const IEditor& editor, const RepresentationOptions& options);

// Representation text of the non-printing character `utf8Char` in `mode` (1 or 2); empty if none.
const char* nonPrintingRepresentation(const char* utf8Char, int mode);

} // namespace npp::core
