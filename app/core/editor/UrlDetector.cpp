// URL detection (phase 3, P3-08): port of upstream Notepad_plus.cpp isUrl / scanToUrlStart /
// scanToUrlEnd / removeUnwantedTrailingChar* (v8.9.8.1) on UTF-16 text. WinINet's
// InternetCrackUrl validation is replaced by QUrl (checked with PowerEditor/Test/UrlDetection).
#include "UrlDetector.h"

#include <QString>
#include <QUrl>

namespace npp::core {

namespace {

std::u16string g_extraSchemes;

bool equalsNoCase(const char16_t* a, const char16_t* b, int n)
{
	for (int i = 0; i < n; ++i) {
		char16_t x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = static_cast<char16_t>(x + 32);
		if (y >= 'A' && y <= 'Z') y = static_cast<char16_t>(y + 32);
		if (x != y)
			return false;
	}
	return true;
}

bool crackUrl(const char16_t* text, int len)
{
	// InternetCrackUrl accepts any "scheme:..." text; for http / https / ftp it needs a host
	const QString str = QString::fromUtf16(text, len);
	const QString scheme = str.left(str.indexOf(QLatin1Char(':'))).toLower();
	if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("ftp")) {
		const QUrl u(str, QUrl::TolerantMode);
		return u.isValid() && !u.host().isEmpty();
	}
	return !scheme.isEmpty();
}

bool isUrlSchemeStartChar(char16_t const c)
{
	return ((c >= 'A') && (c <= 'Z'))
		|| ((c >= 'a') && (c <= 'z'));
}

bool isUrlSchemeDelimiter(char16_t const c) // characters allowed immedeately before scheme
{
	return   ! (((c >= '0') && (c <= '9'))
			 || ((c >= 'A') && (c <= 'Z'))
			 || ((c >= 'a') && (c <= 'z'))
			 ||  (c == '_'));
}

// Checks URL character if it is a part of text or a special symbol
// If the character is a part of regular text:
// - True is returned
// - Regular text is a fundamental alphanumeric which is regularly a part of the common text of the URL, and not a part of queries, http requests etc
// If the character is a whitespace, unicode equivalent of whitespace, or a special character used in URL queries (for example http queries, requests)
// - False is returned
bool isUrlTextChar(char16_t const c)
{
	if (c <= ' ') return false;

	// Checks for unicode whitespace equivalents (ex: whitespace in different languages, mathematical or other contexts)
	switch (c)
	{
		case u'\u0020':		// space
		case u'\u00A0':		// non breaking space
		case u'\u2002':		// en space
		case u'\u2003':		// em space
		case u'\u3000':		// ideographic space (east asian languages)
		case u'\u2004':		// three-per-em
		case u'\u2005':		// four-per-em
		case u'\u2006':		// six-per-em
		case u'\u2007':		// figure space
		case u'\u2008':		// punctuation space
		case u'\u2009':		// thin space
		case u'\u200A':		// hair space
		case u'\u200B':		// zero width space
		case u'\u202F':		// narrow non breaking space
		case u'\u205F':		// medium mathematical space
		case u'\uFEFF':		// zero width non breaking space (byte order mark)
			return false;		
	}
	
	// Checks for special characters
	switch (c)
	{
		case '"':
		case '#':
		case '<':
		case '>':
		case '{':
		case '}':
		case '?':
		case '\u007F':
			return false;
	}
	
	return true;
}

bool isUrlQueryDelimiter(char16_t const c)
{
	switch(c)
	{
		case '&':
		case '+':
		case '=':
		case ';':
			return true;
	}
	return false;
}

bool isUrlSchemeSupported(const char16_t *url, int remainingLength)
{
	const std::u16string mySchemes = u"ftp:// http:// https:// mailto: file:// " + g_extraSchemes + u" ";
	const char16_t *p = mySchemes.c_str();
	while (*p)
	{
		int i = 0;
		while (p [i] && (p [i] != ' ')) i++;
		if (i == 0) return false;
		if (i <= remainingLength && equalsNoCase(url, p, i)) return true;
		p += i;
		while (*p == ' ') p++;
	}
	return false;
}

// scanToUrlStart searches for a possible URL in <text>.
// If a possible URL is found, then:
// - True is returned.
// - The number of characters between <text[start]> and the beginning of the URL candidate is stored in <distance>.
// - The length of the URL scheme is stored in <schemeLength>.
// If no URL is found, then:
// - False is returned.
// - The number of characters between <text[start]> and the end of text is stored in <distance>.
bool scanToUrlStart(const char16_t *text, int textLen, int start, int* distance, int* schemeLength)
{
	int p = start;
	int p0 = 0;
	enum {sUnknown, sScheme} s = sUnknown;
	while (p < textLen)
	{
		switch (s)
		{
			case sUnknown:
				if (isUrlSchemeStartChar(text [p]) && ((p == 0) || isUrlSchemeDelimiter(text [p - 1])))
				{
					p0 = p;
					s = sScheme;
				}
				break;

			case sScheme:
				if (text [p] == ':' && isUrlSchemeSupported(text + p0, textLen - p0))
				{
					*distance = p0 - start;
					*schemeLength = p - p0 + 1;
					return true;
				}
				if (!isUrlSchemeStartChar(text [p]))
					s = sUnknown;
				break;
		}
		p++;
	}
	*schemeLength = 0;
	*distance = p - start;
	return false;
}

// scanToUrlEnd searches the end of an URL, coarsly parsing its main parts HostAndPath, Query and Fragment.
//
// In the query part, a simple pattern is enforced, to avoid that everything goes through as a query.
// The pattern is kept simple, since there seem to be many different forms of queries used in the world.
// The objective here is not to detect whether or not a query is malformed. The objective is, to let through
// most of the real world's queries, and to sort out what is certainly not a query.
//
// The approach is:
// - A query begins with '?', followed by any number of values,
//   which are separated by a single delimiter character '&', '+', '=' or ';'.
// - Each value may be enclosed in single or double quotes.
//
// The query pattern going through looks like this:
// - ?abc;def;fgh="i j k"&'l m n'+opq
//
void scanToUrlEnd(const char16_t *text, int textLen, int start, int* distance)
{
	int p = start;
	char16_t q = 0;
	enum {sHostAndPath, sQuery, sQueryAfterDelimiter, sQueryQuotes, sQueryAfterQuotes, sFragment} s = sHostAndPath;
	while (p < textLen)
	{
		switch (s)
		{
			case sHostAndPath: 
				if (text [p] == '?')
					s = sQuery;
				else if (text [p] == '#')
					s = sFragment;
				else if (!isUrlTextChar (text [p]))
				{
					*distance = p - start;
					return;
				}
				break;

			case sQuery:
				if (text [p] == '#')
					s = sFragment;
				else if (isUrlQueryDelimiter (text [p]))
					s = sQueryAfterDelimiter;
				else if (!isUrlTextChar(text [p]))
				{
					*distance = p - start;
					return;
				}
				break;

			case sQueryAfterDelimiter:
				if ((text [p] == '\'') || (text [p] == '"') || (text [p] == '`'))
				{
					q = text [p];
					s = sQueryQuotes;
				}
				else if (text [p] == '(')
				{
					q = ')';
					s = sQueryQuotes;
				}
				else if (text [p] == '[')
				{
					q = ']';
					s = sQueryQuotes;
				}
				else if (text [p] == '{')
				{
					q = '}';
					s = sQueryQuotes;
				}
				else if (isUrlTextChar(text [p]))
					s = sQuery;
				else
				{
					*distance = p - start;
					return;
				}
				break;

			case sQueryQuotes:
				if (text [p] < ' ')
				{
					*distance = p - start;
					return;
				}
				if (text [p] == q)
					s = sQueryAfterQuotes;
				break;
	
			case sQueryAfterQuotes:
				if (isUrlQueryDelimiter (text [p]))
					s = sQueryAfterDelimiter;
				else
				{
					*distance = p - start;
					return;
				}
				break;

			case sFragment:
				if (text [p] != '?' && !isUrlTextChar(text [p]))
				{
					*distance = p - start;
					return;
				}
				break;
		}
		p++;
	}
	*distance = p - start;
}

// removeUnwantedTrailingCharFromEnclosedUrl removes a single unwanted trailing character from a URL if the URL is enclosed by a pair of characters.
void removeUnwantedTrailingCharFromEnclosedUrl(int start, char16_t const * text, int * length)
{
	// Check if URL is enclosed in apostrophes.
	if (start > 0 && text [start - 1] == '\'' && text [start + *length - 1] == '\'')
		*length -= 1;

	// Check if URL is enclosed in grave accents.
	if (start > 0 && text [start - 1] == '`' && text [start + *length - 1] == '`')
		*length -= 1;
}

// removeUnwantedTrailingCharFromUrl removes a single unwanted trailing character from an URL.
// It has to be called repeatedly, until it returns false, meaning that all unwanted characters are gone.
bool removeUnwantedTrailingCharFromUrl (char16_t const *text, int* length)
{
	int l = *length - 1;
	if (l <= 0) return false;
	{ // remove unwanted single characters
		const char16_t *singleChars = u".,:;?!#";
		for (int i = 0; singleChars [i]; i++)
			if (text [l] == singleChars [i])
			{
				*length = l;
				return true;
			}
	}
	{ // remove unwanted closing parenthesis
		const char16_t *closingParenthesis = u")]";
		const char16_t *openingParenthesis = u"([";
		for (int i = 0; closingParenthesis [i]; i++)
			if (text [l] == closingParenthesis [i])
			{
				int count = 0;
				for (int j = l - 1; j >= 0; j--)
				{
					if (text [j] == closingParenthesis [i])
						count++;
					if (text[j] == openingParenthesis[i])
					{
						if (count > 0)
							count--;
						else
							return false;
					}
				}
				if (count != 0)
					return false;
				*length = l;
				return true;
			}
	}
	return false;
}

// isUrl checks, whether there is a valid URL at <text [start]>.
// If yes:
// - True is returned.
// - The length of the URL is stored in <segmentLen>.
// If no:
// - False is returned.
// - The number of characters between <text[start]> and the next URL is stored in <segementLen>.
// - If no URL is found at all, then the number of characters between <text[start]> and the end of text is stored in <segmentLen>.
} // namespace

void setExtraUrlSchemes(const std::u16string& schemes) { g_extraSchemes = schemes; }

bool isUrl(const char16_t * text, int textLen, int start, int* segmentLen)
{
	int dist = 0, schemeLen = 0;
	if (scanToUrlStart(text, textLen, start, & dist, & schemeLen))
	{
		if (dist)
		{
			*segmentLen = dist;
			return false;
		}
		int len = 0;
		scanToUrlEnd (text, textLen, start + schemeLen, & len);
		if (len)
		{
			len += schemeLen;
			const bool r = crackUrl(&text[start], len); // upstream: WinINet InternetCrackUrl
			if (r)
			{
				removeUnwantedTrailingCharFromEnclosedUrl(start, & text [0], & len);

				while (removeUnwantedTrailingCharFromUrl (& text [start], & len));
				*segmentLen = len;
				return true;
			}
			else // to avoid potentially catastrophic backtracking, skip the entire text that looked like a URL
			{
				*segmentLen = len;
				return false;
			}
		}
		len = 1;
		int lMax = textLen - start;
		while (isUrlSchemeStartChar(text[start+len]) && (len < lMax)) len++;
		*segmentLen = len;
		return false;
	}
	*segmentLen = dist;
	return false;
}

} // namespace npp::core
