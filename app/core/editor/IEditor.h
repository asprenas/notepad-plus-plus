// Abstract Scintilla editor used by npp-core (docs/phase-1-core-extraction.md 4.2).
// Upstream ScintillaEditView::execute(SCI_xxx, w, l) calls map 1:1 to IEditor::send().
// The UI implements it on top of ScintillaEditBase (QtWidgets); the core never sees widgets.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace npp::core {

class IEditor {
public:
    virtual ~IEditor() = default;
    virtual intptr_t send(unsigned int message, uintptr_t wParam = 0, intptr_t lParam = 0) const = 0;

    // Convenience wrappers
    intptr_t sendStr(unsigned int message, uintptr_t wParam, const std::string& s) const
    {
        return send(message, wParam, reinterpret_cast<intptr_t>(s.c_str()));
    }
    intptr_t setProperty(const std::string& key, const std::string& value) const;
    std::string text() const;
    std::string textRange(intptr_t start, intptr_t end) const;
};

} // namespace npp::core
