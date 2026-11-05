// Applies a language (lexer, keywords, styles, properties) to an editor
// (port of upstream ScintillaEditView::defineDocType / setLexer / makeStyle / setStyle).
#pragma once

#include "core/editor/IEditor.h"
#include "core/lang/LangType.h"

namespace npp::core {

class LanguageRegistry;
class StyleRegistry;
struct Style;
struct UserLanguage;

// stylers.xml "Global override" flags (GuiConfig globalOverride*)
struct GlobalOverride {
    bool fg = false, bg = false, font = false, fontSize = false, bold = false, italic = false, underline = false;
    bool any() const noexcept { return fg || bg || font || fontSize || bold || italic || underline; }
};

struct LanguageApplyOptions {
    bool sqlBackslashEscape = true;  // GuiConfig::backslashIsEscapeForSql
    bool showCcUniEol = true;        // GuiConfig::ccUniEolShow
    GlobalOverride override;
    // Replacement for fonts missing on this platform (e.g. "Courier New" on Linux); empty = keep.
    std::string (*fontSubstitute)(const std::string& fontName) = nullptr;
};

// Sets one style; inherits from `defaultStyle` according to the global override flags.
void applyStyle(const IEditor& editor, const Style& style, const Style* defaultStyle,
                const LanguageApplyOptions& options);

// Global styles (STYLE_DEFAULT + predefined styles 33..39) then STYLE_CLEARALL semantics.
void applyGlobalStyles(const IEditor& editor, const StyleRegistry& styles, const LanguageApplyOptions& options);

// Full language setup for `lang` (L_USER: use applyUserLanguage).
void applyLanguage(const IEditor& editor, LangType lang, const LanguageRegistry& langs, const StyleRegistry& styles,
                   const LanguageApplyOptions& options = {});

// UDL setup (upstream setUserLexer). `instanceKey` feeds userDefine.udlName/currentBufferID.
void applyUserLanguage(const IEditor& editor, const UserLanguage& udl, const StyleRegistry& styles,
                       const LanguageApplyOptions& options = {}, uintptr_t instanceKey = 0);

} // namespace npp::core
