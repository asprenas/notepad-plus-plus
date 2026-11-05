// XML / HTML matched tags highlighting (phase 3, P3-08): port of upstream
// PowerEditor/src/ScintillaComponent/xmlMatchedTagsHighlighter.cpp (v8.9.8.1) on an IEditor.
// Generated once by a mechanical conversion (ScintillaEditView::execute -> IEditor::send).
#include "XmlTagMatcher.h"

#include "IEditor.h"

#include <SciLexer.h>
#include <Scintilla.h>

#include <algorithm>
#include <string>
#include <vector>

namespace npp::core {

namespace {
constexpr int kTagMatch = 27, kTagAttr = 26; // SCE_UNIVERSAL_TAGMATCH / TAGATTR
}

void XmlTagMatcher::clearIndicator(int indicator) const
{
	send(SCI_SETINDICATORCURRENT, indicator);
	send(SCI_INDICATORCLEARRANGE, 0, send(SCI_GETLENGTH));
}

std::vector< std::pair<intptr_t, intptr_t> > XmlTagMatcher::getAttributesPos(intptr_t start, intptr_t end)
{
	std::vector< std::pair<intptr_t, intptr_t> > attributes;

	intptr_t bufLen = end - start + 1;
	char *buf = new char[bufLen+1];
	const std::string text = m_e.textRange(start, end);
	std::copy(text.begin(), text.end(), buf);
	buf[text.size()] = 0;

	enum {\
		attr_invalid,\
		attr_key,\
		attr_pre_assign,\
		attr_assign,\
		attr_string,\
		attr_single_quot_string,\
		attr_value,\
		attr_valid\
	} state = attr_invalid;

	int startPos = -1;
	int oneMoreChar = 1;
	int i = 0;
	for (; i < bufLen ; ++i)
	{
		switch (buf[i])
		{
			case ' ':
			case '\t':
			case '\n':
			case '\r':
			{
				if (state == attr_key)
					state = attr_pre_assign;
				else if (state == attr_value)
				{
					state = attr_valid;
					oneMoreChar = 0;
				}
			}
			break;

			case '=':
			{
				if (state == attr_key || state == attr_pre_assign)
					state = attr_assign;
				else if (state == attr_assign || state == attr_value)
					state = attr_invalid;
			}
			break;

			case '"':
			{
				if (state == attr_string)
				{
					state = attr_valid;
					oneMoreChar = 1;
				}
				else if (state == attr_key || state == attr_pre_assign || state == attr_value)
					state = attr_invalid;
				else if (state == attr_assign)
					state = attr_string;
			}
			break;
			
			case '\'':
			{
				if (state == attr_single_quot_string)
				{
					state = attr_valid;
					oneMoreChar = 1;
				}
				else if (state == attr_key || state == attr_pre_assign || state == attr_value)
					state = attr_invalid;
				else if (state == attr_assign)
					state = attr_single_quot_string;
			}
			break;

			default:
			{
				if (state == attr_invalid)
				{
					state = attr_key;
					startPos = i;
				}
				else if (state == attr_pre_assign)
					state = attr_invalid;
				else if (state == attr_assign)
					state = attr_value;
			}
		}

		if (state == attr_valid)
		{
			attributes.push_back(std::pair<intptr_t, intptr_t>(start+startPos, start+i+oneMoreChar));
			state = attr_invalid;
		}
	}

	if (state == attr_value)
		attributes.push_back(std::pair<intptr_t, intptr_t>(start+startPos, start+i-1));

	delete [] buf;
	return attributes;
}



bool XmlTagMatcher::getXmlMatchedTagsPos(XmlMatchedTagsPos &xmlTags)
{
	bool tagFound = false;
	intptr_t caret = send(SCI_GETCURRENTPOS);
	intptr_t searchStartPoint = caret;
	intptr_t styleAt;
	FindResult openFound;
	
	// Search back for the previous open angle bracket.
	// Keep looking whilst the angle bracket found is inside an XML attribute
	do
	{
		openFound = findText("<", searchStartPoint, 0, 0);
		styleAt = send(SCI_GETSTYLEAT, openFound.start);
		searchStartPoint = openFound.start - 1;
	} while (openFound.success && (styleAt == SCE_H_DOUBLESTRING || styleAt == SCE_H_SINGLESTRING || styleAt == SCE_H_COMMENT ) && searchStartPoint > 0);

	if (openFound.success && styleAt != SCE_H_CDATA)
	{
		// Found the "<" before the caret, now check there isn't a > between that position and the caret.
		FindResult closeFound;
		searchStartPoint = openFound.start;
		do
		{
			closeFound = findText(">", searchStartPoint, caret, 0);
			styleAt = send(SCI_GETSTYLEAT, closeFound.start);
			searchStartPoint = closeFound.end;
		} while (closeFound.success && (styleAt == SCE_H_DOUBLESTRING || styleAt == SCE_H_SINGLESTRING || styleAt == SCE_H_COMMENT) && searchStartPoint <= caret);

		if (!closeFound.success)
		{
			// We're in a tag (either a start tag or an end tag)
			intptr_t nextChar = send(SCI_GETCHARAT, openFound.start + 1);


			/////////////////////////////////////////////////////////////////////////
			// CURSOR IN CLOSE TAG   
			/////////////////////////////////////////////////////////////////////////
			if ('/' == nextChar)
			{
				xmlTags.tagCloseStart = openFound.start;
				intptr_t docLength = send(SCI_GETLENGTH);
				FindResult endCloseTag = findText(">", caret, docLength, 0);
				if (endCloseTag.success)
				{
					xmlTags.tagCloseEnd = endCloseTag.end;
				}
				// Now find the tagName
				intptr_t position = openFound.start + 2;

				// UTF-8 or ASCII tag name
				std::string tagName;
				nextChar = send(SCI_GETCHARAT, position);
				// Checking for " or ' is actually wrong here, but it means it works better with invalid XML
				while (position < docLength && !isWhitespace(nextChar) && nextChar != '/' && nextChar != '>' && nextChar != '\"' && nextChar != '\'')
				{
					tagName.push_back(static_cast<char>(nextChar));
					++position;
					nextChar = send(SCI_GETCHARAT, position);
				}
				
				// Now we know where the end of the tag is, and we know what the tag is called
				if (tagName.size() != 0)
				{
					/* Now we need to find the open tag.  The logic here is that we search for "<TAGNAME",
					 * then check the next character - if it's one of '>', ' ', '\"' then we know we've found 
					 * a relevant tag. 
					 * We then need to check if either
					 *    a) this tag is a self-closed tag - e.g. <TAGNAME attrib="value" />
					 * or b) this tag has another closing tag after it and before our closing tag
					 *       e.g.  <TAGNAME attrib="value">some text</TAGNAME></TAGNA|ME>
					 *             (cursor represented by |)
					 * If it's either of the above, then we continue searching, but only up to the
					 * the point of the last find. (So in the (b) example above, we'd only search backwards 
					 * from the first "<TAGNAME...", as we know there's a close tag for the opened tag.

					 * NOTE::  NEED TO CHECK THE ROTTEN CASE: ***********************************************************
					 * <TAGNAME attrib="value"><TAGNAME>something</TAGNAME></TAGNAME></TAGNA|ME>
					 * Maybe count all closing tags between start point and start of our end tag.???
					 */
					intptr_t currentEndPoint = xmlTags.tagCloseStart;
					intptr_t openTagsRemaining = 1;
					FindResult nextOpenTag;
					do 
					{
						nextOpenTag = findOpenTag(tagName, currentEndPoint, 0);
						if (nextOpenTag.success) 
						{
							--openTagsRemaining;
							// Open tag found
							// Now we need to check how many close tags there are between the open tag we just found,
							// and our close tag
							// eg. (Cursor == | )
							// <TAGNAME attrib="value"><TAGNAME>something</TAGNAME></TAGNAME></TAGNA|ME>
							//                         ^^^^^^^^ we've found this guy
							//                                           ^^^^^^^^^^ ^^^^^^^^ Now we need to cound these fellas
							FindResult inbetweenCloseTag;
							intptr_t currentStartPosition = nextOpenTag.end;
							intptr_t closeTagsFound = 0;
							bool forwardSearch = (currentStartPosition < currentEndPoint);

							do
							{
								inbetweenCloseTag = findCloseTag(tagName, currentStartPosition, currentEndPoint);
								
								if (inbetweenCloseTag.success)
								{
									++closeTagsFound;
									if (forwardSearch)
									{
										currentStartPosition = inbetweenCloseTag.end;
									}
									else
									{
										currentStartPosition = inbetweenCloseTag.start - 1;
									}
								}

							} while (inbetweenCloseTag.success);
					
							// If we didn't find any close tags between the open and our close,
							// and there's no open tags remaining to find
							// then the open we found was the right one, and we can return it
							if (closeTagsFound == 0 && openTagsRemaining == 0)
							{
								xmlTags.tagOpenStart = nextOpenTag.start;
								xmlTags.tagOpenEnd = nextOpenTag.end + 1;
								xmlTags.tagNameEnd = nextOpenTag.start + static_cast<int32_t>(tagName.size()) + 1;  /* + 1 to account for '<' */
								tagFound = true;
							}
							else
							{
							
								// Need to find the same number of opening tags, without closing tags etc.
								openTagsRemaining += closeTagsFound;
								currentEndPoint = nextOpenTag.start;
							}
						}
					} while (!tagFound && openTagsRemaining > 0 && nextOpenTag.success);
				}
			}
			else
			{
			/////////////////////////////////////////////////////////////////////////
			// CURSOR IN OPEN TAG   
			/////////////////////////////////////////////////////////////////////////
				intptr_t position = openFound.start + 1;
				intptr_t docLength = send(SCI_GETLENGTH);
				
				xmlTags.tagOpenStart = openFound.start;

				std::string tagName;
				nextChar = send(SCI_GETCHARAT, position);
				// Checking for " or ' is actually wrong here, but it means it works better with invalid XML
				while (position < docLength && !isWhitespace(nextChar) && nextChar != '/' && nextChar != '>' && nextChar != '\"' && nextChar != '\'' )
				{
					tagName.push_back(static_cast<char>(nextChar));
					++position;
					nextChar = send(SCI_GETCHARAT, position);
				}
				
				// Now we know where the end of the tag is, and we know what the tag is called
				if (tagName.size() != 0)
				{
					// First we need to check if this is a self-closing tag.
					// If it is, then we can just return this tag to highlight itself.
					xmlTags.tagNameEnd = openFound.start + static_cast<int32_t>(tagName.size()) + 1;
					intptr_t closeAnglePosition = findCloseAngle(position, docLength);
					if (-1 != closeAnglePosition)
					{
						xmlTags.tagOpenEnd = closeAnglePosition + 1;
						// If it's a self closing tag
						if (send(SCI_GETCHARAT, closeAnglePosition - 1) == '/')
						{
							// Set it as found, and mark that there's no close tag
							xmlTags.tagCloseEnd = -1;
							xmlTags.tagCloseStart = -1;
							tagFound = true;
						}
						else
						{
							// It's a normal open tag



							/* Now we need to find the close tag.  The logic here is that we search for "</TAGNAME",
							 * then check the next character - if it's '>' or whitespace followed by '>' then we've 
							 * found a relevant tag. 
							 * We then need to check if 
							 * our tag has another opening tag after it and before the closing tag we've found
							 *       e.g.  <TA|GNAME><TAGNAME attrib="value">some text</TAGNAME></TAGNAME>
							 *             (cursor represented by |)
							 */
							intptr_t currentStartPosition = xmlTags.tagOpenEnd;
							intptr_t closeTagsRemaining = 1;
							FindResult nextCloseTag;
							do 
							{
								nextCloseTag = findCloseTag(tagName, currentStartPosition, docLength);
								if (nextCloseTag.success) 
								{
									--closeTagsRemaining;
									// Open tag found
									// Now we need to check how many close tags there are between the open tag we just found,
									// and our close tag
									// eg. (Cursor == | )
									// <TAGNAM|E attrib="value"><TAGNAME>something</TAGNAME></TAGNAME></TAGNAME>
									//                                            ^^^^^^^^ we've found this guy
									//                         ^^^^^^^^^ Now we need to find this fella
									FindResult inbetweenOpenTag;
									intptr_t currentEndPosition = nextCloseTag.start;
									intptr_t openTagsFound = 0;

									do
									{
										inbetweenOpenTag = findOpenTag(tagName, currentStartPosition, currentEndPosition);
								
										if (inbetweenOpenTag.success)
										{
											++openTagsFound;
											currentStartPosition = inbetweenOpenTag.end;
										}

									} while (inbetweenOpenTag.success);
					
									// If we didn't find any open tags between our open and the close,
									// and there's no close tags remaining to find
									// then the close we found was the right one, and we can return it
									if (openTagsFound == 0 && closeTagsRemaining == 0)
									{
										xmlTags.tagCloseStart = nextCloseTag.start;
										xmlTags.tagCloseEnd = nextCloseTag.end + 1;
										tagFound = true;
									}
									else
									{
							
										// Need to find the same number of closing tags, without opening tags etc.
										closeTagsRemaining += openTagsFound;
										currentStartPosition = nextCloseTag.end;
									}
								}
							} while (!tagFound && closeTagsRemaining > 0 && nextCloseTag.success);
						} // end if (selfclosingtag)... else {
					} // end if (-1 != closeAngle)  {

				} // end if tagName.size() != 0
			} // end open tag test
		}
	}
	return tagFound;
}

XmlTagMatcher::FindResult XmlTagMatcher::findOpenTag(const std::string& tagName, intptr_t start, intptr_t end)
{
	std::string search("<");
	search.append(tagName);
	FindResult openTagFound;
	openTagFound.success = false;
	FindResult result;
	intptr_t nextChar = 0; 
	intptr_t styleAt;
	intptr_t searchStart = start;
	intptr_t searchEnd = end;
	bool forwardSearch = (start < end);
	do
	{
		
		result = findText(search.c_str(), searchStart, searchEnd, 0);
		if (result.success)
		{
			nextChar = send(SCI_GETCHARAT, result.end);
			styleAt = send(SCI_GETSTYLEAT, result.start);
			if (styleAt != SCE_H_CDATA && styleAt != SCE_H_DOUBLESTRING && styleAt != SCE_H_SINGLESTRING && styleAt != SCE_H_COMMENT)
			{
				// We've got an open tag for this tag name (i.e. nextChar was space or '>')
				// Now we need to find the end of the start tag.
		
				// Common case, the tag is an empty tag with no whitespace. e.g. <TAGNAME>
				if (nextChar == '>')
				{
					openTagFound.end = result.end;
					openTagFound.success = true;
				}
				else if (isWhitespace(nextChar))
				{
					intptr_t closeAnglePosition = findCloseAngle(result.end, forwardSearch ? end : start);
					if (-1 != closeAnglePosition && '/' != send(SCI_GETCHARAT, closeAnglePosition - 1))
					{
						openTagFound.end = closeAnglePosition;
						openTagFound.success = true;
					}
				}
			}

		}

		if (forwardSearch)
		{
			searchStart = result.end + 1;
		}
		else
		{
			searchStart = result.start - 1;
		}
		
		// Loop while we've found a <TAGNAME, but it's either in a CDATA section,
		// or it's got more none whitespace characters after it. e.g. <TAGNAME2
	} while (result.success && !openTagFound.success);
	
	openTagFound.start = result.start;


	return openTagFound;

}


intptr_t XmlTagMatcher::findCloseAngle(intptr_t startPosition, intptr_t endPosition)
{
	// We'll search for the next '>', and check it's not in an attribute using the style
	FindResult closeAngle;
	
	bool isValidClose; 
	intptr_t returnPosition = -1;
	
	// Only search forwards
	if (startPosition > endPosition)
	{
		intptr_t temp = endPosition;
		endPosition = startPosition;
		startPosition = temp;
	}

	do
	{
		isValidClose = false;

		closeAngle = findText(">", startPosition, endPosition);
		if (closeAngle.success)
		{
			auto style = send(SCI_GETSTYLEAT, closeAngle.start);
			// As long as we're not in an attribute (  <TAGNAME attrib="val>ue"> is VALID XML. )
			if (style != SCE_H_DOUBLESTRING && style != SCE_H_SINGLESTRING && style != SCE_H_COMMENT)
			{
				returnPosition = closeAngle.start;
				isValidClose = true;
			}
			else
			{
				startPosition = closeAngle.end;
			}
		}
				
	} while (closeAngle.success && isValidClose == false);

	return returnPosition;
}


XmlTagMatcher::FindResult XmlTagMatcher::findCloseTag(const std::string& tagName, intptr_t start, intptr_t end)
{
	std::string search("</");
	search.append(tagName);
	FindResult closeTagFound;
	closeTagFound.success = false;
	FindResult result;
	intptr_t nextChar; 
	intptr_t searchStart = start;
	intptr_t searchEnd = end;
	bool forwardSearch = (start < end);
	bool validCloseTag;
	do
	{
		validCloseTag = false;
		result = findText(search.c_str(), searchStart, searchEnd, 0);
		if (result.success)
		{
			nextChar = send(SCI_GETCHARAT, result.end);
			auto styleAt = send(SCI_GETSTYLEAT, result.start);
		
			// Setup the parameters for the next search, if there is one.
			if (forwardSearch)
			{
				searchStart = result.end + 1;
			}
			else
			{
				searchStart = result.start - 1;
			}
		
			if (styleAt != SCE_H_CDATA && styleAt != SCE_H_SINGLESTRING && styleAt != SCE_H_DOUBLESTRING && styleAt != SCE_H_COMMENT) // If what we found was in CDATA section, it's not a valid tag.
			{
				// Common case - '>' follows the tag name directly
				if (nextChar == '>')
				{
					validCloseTag = true;
					closeTagFound.start = result.start;
					closeTagFound.end = result.end;
					closeTagFound.success = true;
				}
				else if (isWhitespace(nextChar))  // Otherwise, if it's whitespace, then allow whitespace until a '>' - any other character is invalid.
				{
					intptr_t whitespacePoint = result.end;
					do
					{
						++whitespacePoint;
						nextChar = send(SCI_GETCHARAT, whitespacePoint);
				
					} while (isWhitespace(nextChar));
			
					if (nextChar == '>')
					{
						validCloseTag = true;
						closeTagFound.start = result.start;
						closeTagFound.end = whitespacePoint;
						closeTagFound.success = true;
					}
				}
			}
		}

	} while (result.success && !validCloseTag);

	return closeTagFound;

}

XmlTagMatcher::FindResult XmlTagMatcher::findText(const char *text, intptr_t start, intptr_t end, int flags)
{
	FindResult returnValue;

	Sci_TextToFindFull search{};
	search.lpstrText = const_cast<char *>(text); // Grrrrrr
	search.chrg.cpMin = static_cast<Sci_Position>(start);
	search.chrg.cpMax = static_cast<Sci_Position>(end);

	LangType lang = m_lang;
	if (lang == L_XML || (lang == L_HTML && m_xhtml))
		flags = flags | SCFIND_MATCHCASE;

	intptr_t result = send(SCI_FINDTEXTFULL, flags, reinterpret_cast<intptr_t>(&search));
	if (-1 == result)
	{
		returnValue.success = false;
	}
	else
	{
		returnValue.success = true;
		returnValue.start = search.chrgText.cpMin;
		returnValue.end = search.chrgText.cpMax;
	}
	return returnValue;
}

void XmlTagMatcher::tagMatch(bool doHiliteAttr) 
{
	// Clean up all marks of previous action
	clearIndicator(kTagMatch);
	clearIndicator(kTagAttr);

	// Detect the current lang type. It works only with html and xml
	LangType lang = m_lang;

	if (lang != L_XML && lang != L_HTML && lang != L_PHP && lang != L_ASP && lang != L_JSP)
		return;

	// If we're inside a code block (i.e not markup), don't try to match tags.
	if (lang == L_PHP || lang == L_ASP || lang == L_JSP)
	{
		std::string codeBeginTag = lang == L_PHP ? "<?" : "<%";
		std::string codeEndTag = lang == L_PHP ? "?>" : "%>";

		const intptr_t caret = 1 + send(SCI_GETCURRENTPOS); // +1 to deal with the case when the caret is between the angle and the question mark in "<?" (or between '<' and '%').
		const FindResult startFound = findText(codeBeginTag.c_str(), caret, 0, 0); // This searches backwards from "caret".
		const FindResult endFound= findText(codeEndTag.c_str(), caret, 0, 0); // This searches backwards from "caret".

		if (startFound.success)
		{
			if (! endFound.success)
				return;
			else if (endFound.success && endFound.start <= startFound.end)
				return;
		}
	}

	// Get the original targets and search options to restore after tag matching operation
	auto originalStartPos = send(SCI_GETTARGETSTART);
	auto originalEndPos = send(SCI_GETTARGETEND);
	auto originalSearchFlags = send(SCI_GETSEARCHFLAGS);

	XmlMatchedTagsPos xmlTags;

    // Detect if it's a xml/html tag. If yes, Colorize it
	if (getXmlMatchedTagsPos(xmlTags))
	{
		send(SCI_SETINDICATORCURRENT, kTagMatch);
		int openTagTailLen = 2;

		// Colourising the close tag firstly
		if ((xmlTags.tagCloseStart != -1) && (xmlTags.tagCloseEnd != -1))
		{
			send(SCI_INDICATORFILLRANGE,  xmlTags.tagCloseStart, xmlTags.tagCloseEnd - xmlTags.tagCloseStart);
			// tag close is present, so it's not single tag
			openTagTailLen = 1;
		}

		// Colourising the open tag
		send(SCI_INDICATORFILLRANGE,  xmlTags.tagOpenStart, xmlTags.tagNameEnd - xmlTags.tagOpenStart);
		send(SCI_INDICATORFILLRANGE,  xmlTags.tagOpenEnd - openTagTailLen, openTagTailLen);

        
        // Colouising its attributs
        if (doHiliteAttr)
		{
			std::vector< std::pair<intptr_t, intptr_t> > attributes = getAttributesPos(xmlTags.tagNameEnd, xmlTags.tagOpenEnd - openTagTailLen);
			send(SCI_SETINDICATORCURRENT,  kTagAttr);
			for (size_t i = 0, len = attributes.size(); i < len ; ++i)
			{
				send(SCI_INDICATORFILLRANGE,  attributes[i].first, attributes[i].second - attributes[i].first);
			}
        }

        // Colouising indent guide line position
		if ((send(SCI_GETINDENTATIONGUIDES) != SC_IV_NONE))
		{
			intptr_t columnAtCaret  = send(SCI_GETCOLUMN, xmlTags.tagOpenStart);
			intptr_t columnOpposite = send(SCI_GETCOLUMN, xmlTags.tagCloseStart);

			intptr_t lineAtCaret  = send(SCI_LINEFROMPOSITION, xmlTags.tagOpenStart);
			intptr_t lineOpposite = send(SCI_LINEFROMPOSITION, xmlTags.tagCloseStart);

			if (xmlTags.tagCloseStart != -1 && lineAtCaret != lineOpposite)
			{
				send(SCI_BRACEHIGHLIGHT, xmlTags.tagOpenStart, xmlTags.tagCloseEnd-1);
				send(SCI_SETHIGHLIGHTGUIDE, (columnAtCaret < columnOpposite)?columnAtCaret:columnOpposite);
			}
		}
	}

	// restore the original targets and search options to avoid the conflit with search/replace function
	send(SCI_SETTARGETRANGE, originalStartPos, originalEndPos);
	send(SCI_SETSEARCHFLAGS, originalSearchFlags);
}

} // namespace npp::core
