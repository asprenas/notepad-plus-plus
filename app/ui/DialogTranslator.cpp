#include "DialogTranslator.h"

#include "core/i18n/NativeLanguage.h"

#include <QAbstractButton>
#include <QGroupBox>
#include <QLabel>
#include <QWidget>

namespace npp::ui {

void setControlId(QWidget* w, int id)
{
    w->setObjectName(QString::number(id));
}

QString controlText(const std::string& text, bool keepMnemonic)
{
    QString s = QString::fromStdString(text);
    if (keepMnemonic)
        return s;
    s.replace(QLatin1String("&&"), QStringLiteral("\x01"));
    s.remove(QLatin1Char('&'));
    s.replace(QLatin1Char('\x01'), QLatin1Char('&'));
    return s;
}

bool translateDialog(QWidget* root, const core::NativeLanguage* nl, std::string_view tag, std::string_view titleAttribute)
{
    if (!nl || !nl->isLoaded())
        return false;
    const auto d = nl->dialog(tag, titleAttribute);
    if (!d)
        return false;
    if (!d->title.empty())
        root->setWindowTitle(controlText(d->title, false));
    for (QWidget* w : root->findChildren<QWidget*>()) {
        bool ok = false;
        const int id = w->objectName().toInt(&ok);
        if (!ok)
            continue;
        const auto it = d->items.find(id);
        if (it == d->items.end())
            continue;
        if (auto* b = qobject_cast<QAbstractButton*>(w))
            b->setText(controlText(it->second, true));
        else if (auto* g = qobject_cast<QGroupBox*>(w))
            g->setTitle(controlText(it->second, false));
        else if (auto* l = qobject_cast<QLabel*>(w))
            l->setText(controlText(it->second, l->buddy() != nullptr));
    }
    root->setLayoutDirection(nl->isRTL() ? Qt::RightToLeft : Qt::LeftToRight);
    return true;
}

} // namespace npp::ui
