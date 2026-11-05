// Auto-indentation on typing (phase 3): port of upstream Notepad_plus::maintainIndentation,
// isConditionExprLine, findMachedBracePos (v8.9.8.1) and ScintillaEditView::setLineIndent on an
// IEditor. Generated once by a mechanical conversion.
#include "AutoIndent.h"

#include "IEditor.h"

#include <SciLexer.h>
#include <Scintilla.h>

#include <cstring>

namespace npp::core {

namespace {

intptr_t lineLength(const IEditor& e, intptr_t line)
{
	return e.send(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line)) - e.send(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line));
}

intptr_t lineIndent(const IEditor& e, intptr_t line) { return e.send(SCI_GETLINEINDENTATION, static_cast<uintptr_t>(line)); }

// ScintillaEditView::setLineIndent: also moves the selection with the indentation
void setLineIndent(const IEditor& e, intptr_t line, intptr_t indent)
{
	if (e.send(SCI_GETSELECTIONS) != 1) {
		e.send(SCI_SETLINEINDENTATION, static_cast<uintptr_t>(line), indent);
		return;
	}
	intptr_t cpMin = e.send(SCI_GETSELECTIONSTART), cpMax = e.send(SCI_GETSELECTIONEND);
	const intptr_t posBefore = e.send(SCI_GETLINEINDENTPOSITION, static_cast<uintptr_t>(line));
	e.send(SCI_SETLINEINDENTATION, static_cast<uintptr_t>(line), indent);
	const intptr_t posAfter = e.send(SCI_GETLINEINDENTPOSITION, static_cast<uintptr_t>(line));
	const intptr_t diff = posAfter - posBefore;
	if (posAfter > posBefore) {
		if (cpMin >= posBefore) cpMin += diff;
		if (cpMax >= posBefore) cpMax += diff;
	} else if (posAfter < posBefore) {
		if (cpMin >= posAfter) cpMin = cpMin >= posBefore ? cpMin + diff : posAfter;
		if (cpMax >= posAfter) cpMax = cpMax >= posBefore ? cpMax + diff : posAfter;
	}
	e.send(SCI_SETSEL, static_cast<uintptr_t>(cpMin), cpMax);
}

bool isConditionExprLine(const IEditor& e, intptr_t lineNumber)
{
	if (lineNumber < 0 || lineNumber > e.send(SCI_GETLINECOUNT))
		return false;

	auto startPos = e.send(SCI_POSITIONFROMLINE, lineNumber);
	auto endPos = e.send(SCI_GETLINEENDPOSITION, lineNumber);
	e.send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);
	e.send(SCI_SETTARGETRANGE, startPos, endPos);

	const char ifElseForWhileExpr[] = "((else[ \t]+)?if|for|while)[ \t]*[(].*[)][ \t]*|else[ \t]*";

	auto posFound = e.send(SCI_SEARCHINTARGET, strlen(ifElseForWhileExpr), reinterpret_cast<intptr_t>(ifElseForWhileExpr));
	if (posFound >= 0)
	{
		auto end = e.send(SCI_GETTARGETEND);
		if (end == endPos)
			return true;
	}

	return false;
}

intptr_t findMachedBracePos(const IEditor& e, size_t startPos, size_t endPos, char targetSymbol, char matchedSymbol)
{
	if (startPos == endPos)
		return -1;

	if (startPos > endPos) // backward
	{
		int balance = 0;
		for (intptr_t i = startPos; i >= static_cast<intptr_t>(endPos); --i)
		{
			char aChar = static_cast<char>(e.send(SCI_GETCHARAT, i));
			if (aChar == targetSymbol)
			{
				if (balance == 0)
					return i;
				--balance;
			}
			else if (aChar == matchedSymbol)
			{
				++balance;
			}
		}
	}
	else // forward
	{
	}
	return -1;
}

} // namespace

void maintainIndentation(const IEditor& e, LangType type, int maintainIndent, int ch)
{
	if (maintainIndent == kAutoIndentNone)
		return;

	intptr_t eolMode = e.send(SCI_GETEOLMODE);
	intptr_t curLine = e.send(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(e.send(SCI_GETCURRENTPOS)));
	intptr_t prevLine = curLine - 1;
	intptr_t indentAmountPrevLine = 0;
	intptr_t tabWidth = e.send(SCI_GETTABWIDTH);

	// Do not alter indentation if we were at the beginning of the line and we pressed Enter
	if ((((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
		(eolMode == SC_EOL_CR && ch == '\r')) && prevLine >= 0 && lineLength(e, prevLine) == 0)
		return;

	// (external lexers: phase 7)

	if (maintainIndent == kAutoIndentBasic) // Basic indentation mode only
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && lineLength(e, prevLine) == 0)
				prevLine--;

			if (prevLine >= 0)
			{
				indentAmountPrevLine = lineIndent(e, prevLine);
			}

			if (indentAmountPrevLine > 0)
			{
				setLineIndent(e, curLine, indentAmountPrevLine);
			}
		}

		return;
	}

	// else nppGui._maintainIndent == autoIndent_advance

	if (type == L_C || type == L_CPP || type == L_JAVA || type == L_CS || type == L_OBJC ||
		type == L_PHP || type == L_JS_EMBEDDED || type == L_JAVASCRIPT || type == L_JSP || type == L_CSS || type == L_PERL || 
		type == L_RUST || type == L_POWERSHELL || type == L_JSON || type == L_JSON5 || type == L_TYPESCRIPT || type == L_GOLANG || type == L_SWIFT)
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && lineLength(e, prevLine) == 0)
				prevLine--;

			// Get previous line's Indent
			if (prevLine >= 0)
			{
				indentAmountPrevLine = lineIndent(e, prevLine);
			}

			// get previous char from current line
			intptr_t prevPos = e.send(SCI_GETCURRENTPOS) - (eolMode == SC_EOL_CRLF ? 3 : 2);
			unsigned char prevChar = (unsigned char)e.send(SCI_GETCHARAT, prevPos);
			auto curPos = e.send(SCI_GETCURRENTPOS);
			unsigned char nextChar = (unsigned char)e.send(SCI_GETCHARAT, curPos);

			if (prevChar == '{')
			{
				if (nextChar == '}')
				{
					const char *eolChars;
					if (eolMode == SC_EOL_CRLF)
						eolChars = "\r\n";
					else if (eolMode == SC_EOL_LF)
						eolChars = "\n";
					else
						eolChars = "\r";

					e.send(SCI_INSERTTEXT, e.send(SCI_GETCURRENTPOS), reinterpret_cast<intptr_t>(eolChars));
					setLineIndent(e, curLine + 1, indentAmountPrevLine);
				}
				setLineIndent(e, curLine, indentAmountPrevLine + tabWidth);
			}
			else if (nextChar == '{')
			{
				setLineIndent(e, curLine, indentAmountPrevLine);
			}
			// These languages do no support single line control structures without braces.
			else if (type == L_PERL || type == L_RUST || type == L_POWERSHELL || type == L_JSON || type == L_JSON5)
			{
				setLineIndent(e, curLine, indentAmountPrevLine);
			}
			else if (isConditionExprLine(e, prevLine))
			{
				setLineIndent(e, curLine, indentAmountPrevLine + tabWidth);
			}
			else
			{
				if (indentAmountPrevLine > 0)
				{
					if (prevLine > 0 && isConditionExprLine(e, prevLine - 1))
						setLineIndent(e, curLine, indentAmountPrevLine - tabWidth);
					else
						setLineIndent(e, curLine, indentAmountPrevLine);
				}
			}
		}
		else if (ch == '{')
		{
			// if no character in front of {, aligned with prev line's indentation
			auto startPos = e.send(SCI_POSITIONFROMLINE, curLine);
			intptr_t endPos = e.send(SCI_GETCURRENTPOS);

			for (intptr_t i = endPos - 2; i > 0 && i >= startPos; --i)
			{
				unsigned char aChar = (unsigned char)e.send(SCI_GETCHARAT, i);
				if (aChar != ' ' && aChar != '\t')
					return;
			}

			// Search the non-empty previous line
			while (prevLine >= 0 && lineLength(e, prevLine) == 0)
				prevLine--;

			// Get previous line's Indent
			if (prevLine >= 0)
			{
				indentAmountPrevLine = lineIndent(e, prevLine);

				auto startPos2 = e.send(SCI_POSITIONFROMLINE, prevLine);
				auto endPos2 = e.send(SCI_GETLINEENDPOSITION, prevLine);
				e.send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);
				e.send(SCI_SETTARGETRANGE, startPos2, endPos2);

				const char braceExpr[] = "[ \t]*\\{.*";

				intptr_t posFound = e.send(SCI_SEARCHINTARGET, strlen(braceExpr), reinterpret_cast<intptr_t>(braceExpr));
				if (posFound >= 0)
				{
					auto end = e.send(SCI_GETTARGETEND);
					if (end == endPos2)
						indentAmountPrevLine += tabWidth;
				}
			}

			setLineIndent(e, curLine, indentAmountPrevLine);

		}
		else if (ch == '}')
		{
			// Look backward for the pair {
			intptr_t startPos = e.send(SCI_GETCURRENTPOS);
			if (startPos != 0)
				startPos -= 1;
			intptr_t posFound = findMachedBracePos(e, startPos - 1, 0, '{', '}');

			// if no { found, do nothing
			if (posFound == -1)
				return;

			// if { is in the same line, do nothing
			intptr_t matchedPairLine = e.send(SCI_LINEFROMPOSITION, posFound);
			if (matchedPairLine == curLine)
				return;

			// { is in another line, get its indentation
			indentAmountPrevLine = lineIndent(e, matchedPairLine);

			// aligned } indent with {
			setLineIndent(e, curLine, indentAmountPrevLine);

			/*
			// indent lines from { to }
			for (int i = matchedPairLine + 1; i < curLine; ++i)
				setLineIndent(e, i, indentAmountPrevLine + tabWidth);
			*/
		}
	}
	else if (type == L_PYTHON)
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && lineLength(e, prevLine) == 0)
				prevLine--;

			// Get previous line's Indent
			if (prevLine >= 0)
			{
				indentAmountPrevLine = lineIndent(e, prevLine);
			}

			e.send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_POSIX);

			auto startPos = e.send(SCI_POSITIONFROMLINE, prevLine);
			auto endPos = e.send(SCI_GETLINEENDPOSITION, prevLine);
			e.send(SCI_SETTARGETRANGE, startPos, endPos);

			// colon optionally followed by only whitespace and/or start-of-comment, but NOT on a line that is already a comment
			const char colonExpr[] = ":[ \t]*(#|$)";

			auto posColon = e.send(SCI_SEARCHINTARGET, strlen(colonExpr), reinterpret_cast<intptr_t>(colonExpr));

			// when colon found, additionally check that it is not in a comment, inside a string, etc.
			if ((posColon >= 0) && (e.send(SCI_GETSTYLEINDEXAT, posColon) == SCE_P_OPERATOR))
			{
				setLineIndent(e, curLine, indentAmountPrevLine + tabWidth);
			}
			else if (indentAmountPrevLine > 0)
			{
				setLineIndent(e, curLine, indentAmountPrevLine);
			}
		}
	}
	else // Basic indentation mode
	{
		if (((eolMode == SC_EOL_CRLF || eolMode == SC_EOL_LF) && ch == '\n') ||
			(eolMode == SC_EOL_CR && ch == '\r'))
		{
			// Search the non-empty previous line
			while (prevLine >= 0 && lineLength(e, prevLine) == 0)
				prevLine--;

			if (prevLine >= 0)
			{
				indentAmountPrevLine = lineIndent(e, prevLine);
			}

			if (indentAmountPrevLine > 0)
			{
				setLineIndent(e, curLine, indentAmountPrevLine);
			}
		}
	}
}

} // namespace npp::core
