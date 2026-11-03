#include "ColumnOps.h"

#include "IEditor.h"

#include <Scintilla.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace npp::core {

namespace {

struct ColumnModeInfo {
    intptr_t selLpos = 0, selRpos = 0, order = -1;
    bool isDirectionL2R = true;
    intptr_t nbVirtualAnchorSpc = 0, nbVirtualCaretSpc = 0;
    bool isValid() const { return order >= 0 && selLpos >= 0 && selRpos >= 0 && selLpos <= selRpos; }
};

std::vector<ColumnModeInfo> columnModeSelectInfo(const IEditor& e)
{
    std::vector<ColumnModeInfo> out;
    const intptr_t nbSel = e.send(SCI_GETSELECTIONS);
    if (nbSel <= 1)
        return out;
    for (intptr_t i = 0; i < nbSel; ++i) {
        const auto ui = static_cast<uintptr_t>(i);
        const intptr_t a = e.send(SCI_GETSELECTIONNANCHOR, ui), c = e.send(SCI_GETSELECTIONNCARET, ui);
        const intptr_t va = e.send(SCI_GETSELECTIONNANCHORVIRTUALSPACE, ui), vc = e.send(SCI_GETSELECTIONNCARETVIRTUALSPACE, ui);
        if (a == c && e.send(SCI_SELECTIONISRECTANGLE))
            out.push_back({a, c, i, va < vc, va, vc});
        else if (a > c)
            out.push_back({c, a, i, false, va, vc});
        else
            out.push_back({a, c, i, true, va, vc});
    }
    return out;
}

void setMultiSelections(const IEditor& e, const std::vector<ColumnModeInfo>& cmi)
{
    for (size_t i = 0; i < cmi.size(); ++i) {
        if (cmi[i].isValid()) {
            const intptr_t s = cmi[i].isDirectionL2R ? cmi[i].selLpos : cmi[i].selRpos;
            const intptr_t en = cmi[i].isDirectionL2R ? cmi[i].selRpos : cmi[i].selLpos;
            e.send(SCI_SETSELECTIONNSTART, i, s);
            e.send(SCI_SETSELECTIONNEND, i, en);
        }
        if (cmi[i].nbVirtualAnchorSpc)
            e.send(SCI_SETSELECTIONNANCHORVIRTUALSPACE, i, cmi[i].nbVirtualAnchorSpc);
        if (cmi[i].nbVirtualCaretSpc)
            e.send(SCI_SETSELECTIONNCARETVIRTUALSPACE, i, cmi[i].nbVirtualCaretSpc);
    }
}

// columnReplace(cmi, str) / columnReplace(cmi, numbers...): one text per selection
template <class TextOf>
void columnReplace(const IEditor& e, std::vector<ColumnModeInfo>& cmi, TextOf textOf, bool numbers)
{
    intptr_t totalDiff = 0;
    for (size_t i = 0; i < cmi.size(); ++i) {
        if (!cmi[i].isValid())
            continue;
        const std::string str = textOf(i);
        const intptr_t diff = static_cast<intptr_t>(str.size()) - (cmi[i].selRpos - cmi[i].selLpos);
        cmi[i].selLpos += totalDiff;
        cmi[i].selRpos += totalDiff;
        // (the text version tests the anchor virtual space, the numbers version the smaller one)
        const intptr_t va = numbers ? std::min(cmi[i].nbVirtualAnchorSpc, cmi[i].nbVirtualCaretSpc) : cmi[i].nbVirtualAnchorSpc;
        const intptr_t vc = numbers ? std::max(cmi[i].nbVirtualAnchorSpc, cmi[i].nbVirtualCaretSpc) : cmi[i].nbVirtualCaretSpc;
        const bool hasVirtualSpc = va > 0;
        if (hasVirtualSpc) { // virtual space: insert real spaces
            for (intptr_t j = 0, k = cmi[i].selLpos; j < vc; ++j, ++k)
                e.send(SCI_INSERTTEXT, static_cast<uintptr_t>(k), reinterpret_cast<intptr_t>(" "));
            cmi[i].selLpos += va;
            cmi[i].selRpos += vc;
        }
        e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(cmi[i].selLpos), cmi[i].selRpos);
        e.send(SCI_REPLACETARGET, static_cast<uintptr_t>(-1), reinterpret_cast<intptr_t>(str.c_str()));
        if (hasVirtualSpc) {
            totalDiff += va + static_cast<intptr_t>(str.size());
            cmi[i].nbVirtualAnchorSpc = cmi[i].nbVirtualCaretSpc = 0;
        } else {
            totalDiff += diff;
        }
        cmi[i].selRpos += diff;
    }
}

// Without a column selection: insert at the caret column of the lines caret line .. last line
template <class TextOf>
void insertAtCaretColumn(const IEditor& e, TextOf textOf)
{
    const auto cursorPos = e.send(SCI_GETCURRENTPOS);
    const auto cursorCol = e.send(SCI_GETCOLUMN, static_cast<uintptr_t>(cursorPos));
    const auto cursorLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(cursorPos));
    const auto endLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(e.send(SCI_GETLENGTH)));
    for (intptr_t i = cursorLine; i <= endLine; ++i) {
        const auto lineBegin = e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(i));
        const auto lineEnd = e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(i));
        const auto lineEndCol = e.send(SCI_GETCOLUMN, static_cast<uintptr_t>(lineEnd));
        std::string s2r = e.textRange(lineBegin, lineEnd);
        const std::string str = textOf(static_cast<size_t>(i - cursorLine));
        if (lineEndCol < cursorCol) {
            s2r.append(static_cast<size_t>(cursorCol - lineEndCol), ' ');
            s2r.append(str);
        } else {
            intptr_t rel = e.send(SCI_FINDCOLUMN, static_cast<uintptr_t>(i), cursorCol) - lineBegin;
            rel = std::min<intptr_t>(rel, static_cast<intptr_t>(s2r.size()));
            s2r.insert(static_cast<size_t>(rel), str);
        }
        e.send(SCI_SETTARGETRANGE, static_cast<uintptr_t>(lineBegin), lineEnd);
        e.send(SCI_REPLACETARGET, s2r.size(), reinterpret_cast<intptr_t>(s2r.data()));
    }
}

size_t baseOf(NumBase b) { return b == NumBase::Dec ? 10 : (b == NumBase::Hex || b == NumBase::HexUpper) ? 16 : b == NumBase::Oct ? 8 : 2; }

std::vector<size_t> numberSequence(size_t count, size_t initial, size_t increase, size_t repeat)
{
    std::vector<size_t> numbers;
    size_t cur = initial;
    while (numbers.size() < count) {
        for (size_t i = 0; i < repeat && numbers.size() < count; ++i)
            numbers.push_back(cur);
        cur += increase;
    }
    return numbers;
}

} // namespace

size_t numberOfDigits(size_t n, size_t base)
{
    size_t d = 0;
    do {
        ++d;
        n /= base;
    } while (n != 0);
    return d;
}

std::string formatColumnNumber(size_t number, NumBase b, size_t nbDigits, Leading lead)
{
    std::string numberStr;
    if (b == NumBase::Bin) {
        size_t n = number;
        do {
            numberStr.insert(numberStr.begin(), n % 2 ? '1' : '0');
            n /= 2;
        } while (n != 0);
    } else if (b == NumBase::Oct) {
        std::ostringstream s;
        s << std::oct << number;
        numberStr = s.str();
    } else if (b == NumBase::Hex || b == NumBase::HexUpper) {
        std::ostringstream s;
        if (b == NumBase::HexUpper)
            s << std::uppercase;
        s << std::hex << number;
        numberStr = s.str();
    } else {
        numberStr = std::to_string(number);
    }
    if (nbDigits <= numberStr.size())
        return numberStr;
    const std::string pad(nbDigits - numberStr.size(), lead == Leading::Zeros ? '0' : ' ');
    return lead == Leading::None ? numberStr + pad : pad + numberStr; // no leading: spaces after the number
}

void columnInsertText(const IEditor& e, const std::string& text)
{
    e.send(SCI_BEGINUNDOACTION);
    if (e.send(SCI_SELECTIONISRECTANGLE) || e.send(SCI_GETSELECTIONS) > 1) {
        auto cmi = columnModeSelectInfo(e);
        std::sort(cmi.begin(), cmi.end(), [](const auto& l, const auto& r) { return l.selLpos < r.selLpos; });
        columnReplace(e, cmi, [&text](size_t) { return text; }, false);
        std::sort(cmi.begin(), cmi.end(), [](const auto& l, const auto& r) { return l.order < r.order; });
        setMultiSelections(e, cmi);
    } else {
        insertAtCaretColumn(e, [&text](size_t) { return text; });
    }
    e.send(SCI_ENDUNDOACTION);
}

void columnInsertNumbers(const IEditor& e, size_t initial, size_t increase, size_t repeat, NumBase base, Leading lead)
{
    repeat = std::max<size_t>(1, repeat); // 0 would loop forever
    const size_t b = baseOf(base);
    e.send(SCI_BEGINUNDOACTION);
    if (e.send(SCI_SELECTIONISRECTANGLE) || e.send(SCI_GETSELECTIONS) > 1) {
        auto cmi = columnModeSelectInfo(e);
        if (!cmi.empty()) {
            std::sort(cmi.begin(), cmi.end(), [](const auto& l, const auto& r) { return l.selLpos < r.selLpos; });
            const auto numbers = numberSequence(cmi.size(), initial, increase, repeat);
            const size_t width = std::max(numberOfDigits(initial, b), numberOfDigits(numbers.back(), b));
            columnReplace(e, cmi, [&](size_t i) { return formatColumnNumber(numbers[i], base, width, lead); }, true);
            std::sort(cmi.begin(), cmi.end(), [](const auto& l, const auto& r) { return l.order < r.order; });
            setMultiSelections(e, cmi);
        }
    } else {
        const auto cursorLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(e.send(SCI_GETCURRENTPOS)));
        const auto endLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(e.send(SCI_GETLENGTH)));
        const auto numbers = numberSequence(static_cast<size_t>(endLine - cursorLine + 1), initial, increase, repeat);
        const size_t width = std::max(numberOfDigits(initial, b), numberOfDigits(numbers.back(), b));
        insertAtCaretColumn(e, [&](size_t i) { return formatColumnNumber(numbers[i], base, width, lead); });
    }
    e.send(SCI_ENDUNDOACTION);
}

bool findCharInRange(const IEditor& e, unsigned char begin, unsigned char end, intptr_t startPos, bool down, bool wrap)
{
    const intptr_t total = e.send(SCI_GETLENGTH);
    if (startPos == -1)
        startPos = down ? 0 : total - 1;
    if (startPos > total)
        return false;
    const std::string content = e.textRange(0, total);
    auto inRange = [&](intptr_t i) {
        const auto c = static_cast<unsigned char>(content[static_cast<size_t>(i)]);
        return c >= begin && c <= end;
    };
    intptr_t found = -1;
    for (intptr_t i = startPos - (down ? 0 : 1); down ? i < total : i >= 0; down ? ++i : --i)
        if (inRange(i)) {
            found = i;
            break;
        }
    if (found < 0 && wrap)
        for (intptr_t i = down ? 0 : total - 1; down ? i < total : i >= 0; down ? ++i : --i)
            if (inRange(i)) {
                found = i;
                break;
            }
    if (found < 0)
        return false;
    e.send(SCI_ENSUREVISIBLE, static_cast<uintptr_t>(e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(found))));
    e.send(SCI_GOTOPOS, static_cast<uintptr_t>(found));
    e.send(SCI_SETSEL, static_cast<uintptr_t>(down ? found : found + 1), down ? found + 1 : found);
    return true;
}

} // namespace npp::core
