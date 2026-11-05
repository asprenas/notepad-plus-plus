// XML / HTML matched tags highlighting: port of upstream xmlMatchedTagsHighlighter.h
#pragma once

#include "IEditor.h"
#include "core/lang/LangType.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace npp::core {

class XmlTagMatcher {
public:
	XmlTagMatcher(const IEditor& e, LangType lang, bool xhtml) : m_e(e), m_lang(lang), m_xhtml(xhtml) {}
	void tagMatch(bool doHiliteAttr);
	
private:
	const IEditor& m_e;
	LangType m_lang;
	bool m_xhtml;
	intptr_t send(unsigned m, uintptr_t w = 0, intptr_t l = 0) const { return m_e.send(m, w, l); }
	void clearIndicator(int indicator) const;
	
	struct XmlMatchedTagsPos {
		intptr_t tagOpenStart = 0;
		intptr_t tagNameEnd = 0;
		intptr_t tagOpenEnd = 0;

		intptr_t tagCloseStart = 0;
		intptr_t tagCloseEnd = 0;
	};

	struct FindResult {
		intptr_t start = 0;
		intptr_t end = 0;
		bool success = false;
	};
	
	bool getXmlMatchedTagsPos(XmlMatchedTagsPos & tagsPos);

	// Allowed whitespace characters in XML
	static bool isWhitespace(intptr_t ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

	FindResult findText(const char *text, intptr_t start, intptr_t end, int flags = 0);
	FindResult findOpenTag(const std::string& tagName, intptr_t start, intptr_t end);
	FindResult findCloseTag(const std::string& tagName, intptr_t start, intptr_t end);
	intptr_t findCloseAngle(intptr_t startPosition, intptr_t endPosition);
	
	std::vector< std::pair<intptr_t, intptr_t> > getAttributesPos(intptr_t start, intptr_t end);
	
};

} // namespace npp::core
