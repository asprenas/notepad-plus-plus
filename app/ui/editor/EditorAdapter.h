// IEditor implementation on top of the Scintilla Qt widget (UI side of the core/UI boundary).
#pragma once

#include "core/editor/IEditor.h"

#include <ScintillaEditBase.h>

namespace npp::ui {

class EditorAdapter final : public core::IEditor {
public:
    explicit EditorAdapter(ScintillaEditBase* editor) : m_editor(editor) {}
    intptr_t send(unsigned int message, uintptr_t wParam = 0, intptr_t lParam = 0) const override
    {
        return m_editor->send(message, wParam, lParam);
    }
    ScintillaEditBase* widget() const noexcept { return m_editor; }

private:
    ScintillaEditBase* m_editor;
};

} // namespace npp::ui
