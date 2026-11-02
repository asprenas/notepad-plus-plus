#include "IEditor.h"

#include <Scintilla.h>

namespace npp::core {

intptr_t IEditor::setProperty(const std::string& key, const std::string& value) const
{
    return send(SCI_SETPROPERTY, reinterpret_cast<uintptr_t>(key.c_str()), reinterpret_cast<intptr_t>(value.c_str()));
}

std::string IEditor::text() const
{
    const intptr_t len = send(SCI_GETLENGTH);
    std::string s(static_cast<size_t>(len) + 1, '\0');
    send(SCI_GETTEXT, static_cast<uintptr_t>(len + 1), reinterpret_cast<intptr_t>(s.data()));
    s.resize(static_cast<size_t>(len));
    return s;
}

std::string IEditor::textRange(intptr_t start, intptr_t end) const
{
    if (end <= start)
        return {};
    std::string s(static_cast<size_t>(end - start) + 1, '\0');
    Sci_TextRangeFull tr{{start, end}, s.data()};
    send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<intptr_t>(&tr));
    s.resize(static_cast<size_t>(end - start));
    return s;
}

} // namespace npp::core
