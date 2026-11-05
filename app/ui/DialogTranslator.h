// Applies a nativeLang <Dialog> block to a Qt dialog by control id (upstream changeDlgLang):
// child widgets whose objectName is the upstream control id get the translated text.
#pragma once

#include <QString>

#include <string_view>

class QWidget;

namespace npp::core {
class NativeLanguage;
struct DialogStrings;
}

namespace npp::ui {

// Sets the id of a control (objectName = upstream IDC_* value).
void setControlId(QWidget* w, int id);

// Translates `root`'s children with the <tag> block; sets the window title from `titleAttribute`
// when it is present and not empty. Returns false if the language has no such block.
bool translateDialog(QWidget* root, const core::NativeLanguage* nl, std::string_view tag,
                     std::string_view titleAttribute = "title");

// Win32 rc / nativeLang text -> Qt text (labels without buddy lose the '&' marks).
QString controlText(const std::string& text, bool keepMnemonic);

} // namespace npp::ui
