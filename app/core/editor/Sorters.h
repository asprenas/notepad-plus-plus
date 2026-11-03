// Line sorters: port of upstream PowerEditor/src/MISC/Common/Sorters.h (v8.9.8.1) on UTF-16
// strings (std::u16string instead of std::wstring, same algorithms). Generated once by a
// mechanical conversion; differences: case-insensitive compare and number parsing are
// portable helpers (upstream: _wcsicmp, _wcstod_l with the en-US locale).
#pragma once

#include <algorithm>
#include <cassert>
#include <ctime>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace npp::core {

// _wcsicmp equivalent (simple case folding per UTF-16 unit)
int compareIgnoreCase(const std::u16string& a, const std::u16string& b);
// Upstream stringTakeWhileAdmissable / stringReplace (Common.cpp)
std::u16string stringTakeWhileAdmissable(const std::u16string& input, const std::u16string& admissable);
std::u16string stringReplace(std::u16string subject, const std::u16string& search, const std::u16string& replace);
// std::stod in the "C" locale; throws std::invalid_argument / std::out_of_range like upstream
double parseDouble(const std::u16string& input);

// Base interface for line sorting.
class ISorter
{
private:
	bool _isDescending = true;
	size_t _fromColumn = 0;
	size_t _toColumn = 0;

protected:
	bool isDescending() const {
		return _isDescending;
	}

	std::u16string getSortKey(const std::u16string& input) {
		if (isSortingSpecificColumns())
		{
			if (input.length() < _fromColumn)
			{
				// prevent an std::out_of_range exception
				return u"";
			}
			else if (_fromColumn == _toColumn)
			{
				// get characters from the indicated column to the end of the line
				return input.substr(_fromColumn);
			}
			else
			{
				// get characters between the indicated columns, inclusive
				return input.substr(_fromColumn, _toColumn - _fromColumn);
			}
		}
		else
		{
			return input;
		}
	}

	bool isSortingSpecificColumns() {
		return _toColumn != 0;
	}

public:
	ISorter(bool isDescending, size_t fromColumn, size_t toColumn) : _isDescending(isDescending), _fromColumn(fromColumn), _toColumn(toColumn) {
		assert(_fromColumn <= _toColumn);
	}
	virtual ~ISorter() {}
	virtual void sort(std::vector<std::u16string>& lines) = 0;
};

// Implementation of lexicographic sorting of lines.
class LexicographicSorter : public ISorter
{
public:
	LexicographicSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn) {}
	
	void sort(std::vector<std::u16string>& lines) override {
		// Note that both branches here are equivalent in the sense that they always give the same answer.
		// However, if we are *not* sorting specific columns, then we get a 40% speed improvement by not calling
		// getSortKey() so many times.
		if (isSortingSpecificColumns())
		{
			std::stable_sort(lines.begin(), lines.end(), [this](const std::u16string& a, const std::u16string& b)
			{
				if (isDescending())
				{
					return getSortKey(a).compare(getSortKey(b)) > 0;
					
				}
				else
				{
					return getSortKey(a).compare(getSortKey(b)) < 0;
				}
			});
		}
		else
		{
			std::sort(lines.begin(), lines.end(), [this](const std::u16string& a, const std::u16string& b)
			{
				if (isDescending())
				{
					return a.compare(b) > 0;
				}
				else
				{
					return a.compare(b) < 0;
				}
			});
		}
	}
};

// Implementation of lexicographic sorting of lines, ignoring character casing
class LexicographicCaseInsensitiveSorter : public ISorter
{
public:
	LexicographicCaseInsensitiveSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn) {}

	void sort(std::vector<std::u16string>& lines) override {
		// Note that both branches here are equivalent in the sense that they always give the same answer.
		// However, if we are *not* sorting specific columns, then we get a 40% speed improvement by not calling
		// getSortKey() so many times.
		if (isSortingSpecificColumns())
		{
			std::stable_sort(lines.begin(), lines.end(), [this](const std::u16string& a, const std::u16string& b)
				{
					if (isDescending())
					{
						return compareIgnoreCase(getSortKey(a), getSortKey(b)) > 0;
					}
					else
					{
						return compareIgnoreCase(getSortKey(a), getSortKey(b)) < 0;
					}
				});
		}
		else
		{
			std::sort(lines.begin(), lines.end(), [this](const std::u16string& a, const std::u16string& b)
				{
					if (isDescending())
					{
						return compareIgnoreCase(a, b) > 0;
					}
					else
					{
						return compareIgnoreCase(a, b) < 0;
					}
				});
		}
	}
};

class IntegerSorter : public ISorter
{
public:
	IntegerSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn) {}

	void sort(std::vector<std::u16string>& lines) override {
		if (isSortingSpecificColumns())
		{
			std::stable_sort(lines.begin(), lines.end(), [this](std::u16string aIn, std::u16string bIn)
			{
				std::u16string a = getSortKey(aIn);
				std::u16string b = getSortKey(bIn);

				long long compareResult = 0;
				size_t aNumIndex = 0;
				size_t bNumIndex = 0;
				while (compareResult == 0)
				{
					if (aNumIndex >= a.length() || bNumIndex >= b.length())
					{
						compareResult = a.compare(std::min<size_t>(aNumIndex, a.length()), std::u16string::npos, b, std::min<size_t>(bNumIndex, b.length()), std::u16string::npos);
						break;
					}

					bool aChunkIsNum = a[aNumIndex] >= u'0' && a[aNumIndex] <= u'9';
					bool bChunkIsNum = b[bNumIndex] >= u'0' && b[bNumIndex] <= u'9';

					int aNumSign = 1;
					// Could be start of negative number
					if (!aChunkIsNum && (aNumIndex + 1) < a.length())
					{
						aChunkIsNum = (a[aNumIndex] == u'-' && (a[aNumIndex + 1] >= u'0' && a[aNumIndex + 1] <= u'9'));
						aNumSign = -1;
					}

					int bNumSign = 1;
					if (!bChunkIsNum && (bNumIndex + 1) < b.length())
					{
						bChunkIsNum = (b[bNumIndex] == u'-' && (b[bNumIndex + 1] >= u'0' && b[bNumIndex + 1] <= u'9'));
						bNumSign = -1;
					}

					// One is number and one is string
					if (aChunkIsNum != bChunkIsNum)
					{
						compareResult = a[aNumIndex] - b[bNumIndex];

						// compareResult isn't necessarily 0
						// consider this case: "0-0", "0-"
						// "-0" is considered a number, but "-" isn't
						// but we are comparing two "-', which is the same
						aNumIndex++;
						bNumIndex++;
					}
					// Both are numbers
					else if (aChunkIsNum)
					{
						// if the sign is differemt, just return the compareResult
						if (aNumSign != bNumSign)
						{
							if (aNumSign == 1)
							{
								compareResult = 1;
							}
							else
							{
								compareResult = -1;
							}
							// No need to update anything; compareResult != 0; will break out while loop
						}
						else
						{
							if (aNumSign == -1)
							{
								aNumIndex++;
								bNumIndex++;
							}

							size_t aNumEnd = a.find_first_not_of(u"1234567890", aNumIndex);
							if (aNumEnd == std::u16string::npos)
							{
								aNumEnd = a.length();
							}

							size_t bNumEnd = b.find_first_not_of(u"1234567890", bNumIndex);
							if (bNumEnd == std::u16string::npos)
							{
								bNumEnd = b.length();
							}

							int aZeroNum = 0;
							while (aNumIndex < a.length() && a[aNumIndex] == '0')
							{
								aZeroNum++;
								aNumIndex++;
							}

							int bZeroNum = 0;
							while (bNumIndex < b.length() && b[bNumIndex] == '0')
							{
								bZeroNum++;
								bNumIndex++;
							}

							size_t aNumLength = aNumEnd - aNumIndex;
							size_t bNumLength = bNumEnd - bNumIndex;

							// aNum is longer than bNum, must be larger (smaller if negative)
							if (aNumLength > bNumLength)
							{
								compareResult = 1 * aNumSign;
								// No need to update anything; compareResult != 0; will break out while loop
							}
							// bNum is longer than aNum, must be larger (smaller if negative)
							else if (aNumLength < bNumLength)
							{
								compareResult = -1 * aNumSign;
								// No need to update anything; compareResult != 0; will break out while loop
							}
							else
							{
								// the lengths of the numbers are equal
								// compare the two number. However, we cannot use std::stoll
								// because the number strings can be every large, well over the maximum long long value
								// thus, we compare each digit one by one
								while (compareResult == 0
									&& aNumIndex < a.length()
									&& (a[aNumIndex] >= u'0' && a[aNumIndex] <= u'9')
									&& bNumIndex < b.length()
									&& (b[bNumIndex] >= u'0' && b[bNumIndex] <= u'9'))
								{
									compareResult = (a[aNumIndex] - b[bNumIndex]) * aNumSign;
									aNumIndex++;
									bNumIndex++;
								}

								if (compareResult == 0)
								{
									compareResult = bZeroNum - aZeroNum;
								}
							}
						}
					}
					// Both are strings
					else
					{
						if (a[aNumIndex] == u'-')
						{
							aNumIndex++;
						}

						if (b[bNumIndex] == u'-')
						{
							bNumIndex++;
						}

						size_t aChunkEnd = a.find_first_of(u"1234567890-", aNumIndex);
						size_t bChunkEnd = b.find_first_of(u"1234567890-", bNumIndex);
						compareResult = a.compare(aNumIndex, aChunkEnd - aNumIndex, b, bNumIndex, bChunkEnd - bNumIndex);
						aNumIndex = aChunkEnd;
						bNumIndex = bChunkEnd;
					}
				}

				if (isDescending())
				{
					return compareResult > 0;
				}
				else
				{
					return compareResult < 0;
				}
			});
		}
		else
		{
			std::sort(lines.begin(), lines.end(), [this](std::u16string aIn, std::u16string bIn)
			{
				std::u16string a = aIn;
				std::u16string b = bIn;

				long long compareResult = 0;
				size_t aNumIndex = 0;
				size_t bNumIndex = 0;
				while (compareResult == 0)
				{
					if (aNumIndex >= a.length() || bNumIndex >= b.length())
					{
						compareResult = a.compare(std::min<size_t>(aNumIndex, a.length()), std::u16string::npos, b, std::min<size_t>(bNumIndex, b.length()), std::u16string::npos);
						break;
					}

					bool aChunkIsNum = a[aNumIndex] >= u'0' && a[aNumIndex] <= u'9';
					bool bChunkIsNum = b[bNumIndex] >= u'0' && b[bNumIndex] <= u'9';

					int aNumSign = 1;
					// Could be start of negative number
					if (!aChunkIsNum && (aNumIndex + 1) < a.length())
					{
						aChunkIsNum = (a[aNumIndex] == u'-' && (a[aNumIndex + 1] >= u'0' && a[aNumIndex + 1] <= u'9'));
						aNumSign = -1;
					}

					int bNumSign = 1;
					if (!bChunkIsNum && (bNumIndex + 1) < b.length())
					{
						bChunkIsNum = (b[bNumIndex] == u'-' && (b[bNumIndex + 1] >= u'0' && b[bNumIndex + 1] <= u'9'));
						bNumSign = -1;
					}

					// One is number and one is string
					if (aChunkIsNum != bChunkIsNum)
					{
						compareResult = a[aNumIndex] - b[bNumIndex];

						// compareResult isn't necessarily 0
						// consider this case: "0-0", "0-"
						// "-0" is considered a number, but "-" isn't
						// but we are comparing two "-', which is the same
						// increment the indexes for this case and continue the loop
						aNumIndex++;
						bNumIndex++;
					}
					// Both are numbers
					else if (aChunkIsNum)
					{
						// if the sign is differemt, just return the compareResult
						if (aNumSign != bNumSign)
						{
							if (aNumSign == 1)
							{
								compareResult = 1;
							}
							else
							{
								compareResult = -1;
							}
							// No need to update anything; compareResult != 0; will break out while loop
						}
						else
						{
							if (aNumSign == -1)
							{
								aNumIndex++;
								bNumIndex++;
							}

							size_t aNumEnd = a.find_first_not_of(u"1234567890", aNumIndex);
							if (aNumEnd == std::u16string::npos)
							{
								aNumEnd = a.length();
							}

							size_t bNumEnd = b.find_first_not_of(u"1234567890", bNumIndex);
							if (bNumEnd == std::u16string::npos)
							{
								bNumEnd = b.length();
							}

							int aZeroNum = 0;
							while (aNumIndex < a.length() && a[aNumIndex] == '0')
							{
								aZeroNum++;
								aNumIndex++;
							}

							int bZeroNum = 0;
							while (bNumIndex < b.length() && b[bNumIndex] == '0')
							{
								bZeroNum++;
								bNumIndex++;
							}

							size_t aNumLength = aNumEnd - aNumIndex;
							size_t bNumLength = bNumEnd - bNumIndex;

							// aNum is longer than bNum, must be larger (smaller if negative)
							if (aNumLength > bNumLength)
							{
								compareResult = 1 * aNumSign;
								// No need to update anything; compareResult != 0; will break out while loop
							}
							// bNum is longer than aNum, must be larger (smaller if negative)
							else if (aNumLength < bNumLength)
							{
								compareResult = -1 * aNumSign;
								// No need to update anything; compareResult != 0; will break out while loop
							}
							else
							{
								// the lengths of the numbers are equal
								// compare the two number. However, we cannot use std::stoll
								// because the number strings can be every large, well over the maximum long long value
								// thus, we compare each digit one by one
								while (compareResult == 0
									&& aNumIndex < a.length()
									&& (a[aNumIndex] >= u'0' && a[aNumIndex] <= u'9')
									&& bNumIndex < b.length()
									&& (b[bNumIndex] >= u'0' && b[bNumIndex] <= u'9'))
								{
									compareResult = (a[aNumIndex] - b[bNumIndex]) * aNumSign;
									aNumIndex++;
									bNumIndex++;
								}

								if (compareResult == 0)
								{
									compareResult = bZeroNum - aZeroNum;
								}
							}
						}
					}
					// Both are strings
					else
					{
						if (a[aNumIndex] == u'-')
						{
							aNumIndex++;
						}

						if (b[bNumIndex] == u'-')
						{
							bNumIndex++;
						}

						size_t aChunkEnd = a.find_first_of(u"1234567890-", aNumIndex);
						size_t bChunkEnd = b.find_first_of(u"1234567890-", bNumIndex);
						compareResult = a.compare(aNumIndex, aChunkEnd - aNumIndex, b, bNumIndex, bChunkEnd - bNumIndex);
						aNumIndex = aChunkEnd;
						bNumIndex = bChunkEnd;
					}
				}

				if (isDescending())
				{
					return compareResult > 0;
				}
				else
				{
					return compareResult < 0;
				}
			});
		}
	}
};

// Convert each line to a number and then sort.
// The conversion must be implemented in classes which inherit from this, see prepareStringForConversion and convertStringToNumber.
template<typename T_Num>
class NumericSorter : public ISorter
{
public:
	NumericSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn)
	{
	}

	~NumericSorter() override
	{
	}
	
	void sort(std::vector<std::u16string>& lines) override {
		// Note that empty lines are filtered out and added back manually to the output at the end.
		std::vector<std::pair<size_t, T_Num>> nonEmptyInputAsNumbers;
		std::vector<std::u16string> empties;
		nonEmptyInputAsNumbers.reserve(lines.size());
		for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
		{
			const std::u16string originalLine = lines[lineIndex];
			const std::u16string preparedLine = prepareStringForConversion(originalLine);
			if (considerStringEmpty(preparedLine))
			{
				empties.push_back(originalLine);
			}
			else
			{
				try {
					nonEmptyInputAsNumbers.push_back(std::make_pair(lineIndex, convertStringToNumber(preparedLine)));
				}
				catch (...)
				{
					throw lineIndex;
				}
			}
		}

		assert(nonEmptyInputAsNumbers.size() + empties.size() == lines.size());
		const bool descending = isDescending();
		std::stable_sort(nonEmptyInputAsNumbers.begin(), nonEmptyInputAsNumbers.end(), [descending](std::pair<size_t, T_Num> a, std::pair<size_t, T_Num> b)
			{
				if (descending)
				{
					return a.second > b.second;
				}
				else
				{
					return a.second < b.second;
				}
			});

		std::vector<std::u16string> output;
		output.reserve(lines.size());
		if (!isDescending())
		{
			output.insert(output.end(), empties.begin(), empties.end());
		}

		for (auto it = nonEmptyInputAsNumbers.begin(); it != nonEmptyInputAsNumbers.end(); ++it)
		{
			output.push_back(lines[it->first]);
		}

		if (isDescending())
		{
			output.insert(output.end(), empties.begin(), empties.end());
		}

		assert(output.size() == lines.size());
		lines = output;
	}

protected:
	bool considerStringEmpty(const std::u16string& input) {
		// String has something else than just whitespace.
		return input.find_first_not_of(u" \t\r\n") == std::string::npos;
	}

	// Prepare the string for conversion to number.
	virtual std::u16string prepareStringForConversion(const std::u16string& input) = 0;

	// Should convert the input string to a number of the correct type.
	// If unable to convert, throw either std::invalid_argument or std::out_of_range.
	virtual T_Num convertStringToNumber(const std::u16string& input) = 0;

};

// Converts lines to double before sorting (assumes decimal comma).
class DecimalCommaSorter : public NumericSorter<double>
{
public:
	DecimalCommaSorter(bool isDescending, size_t fromColumn, size_t toColumn) : NumericSorter<double>(isDescending, fromColumn, toColumn) {}

protected:
	std::u16string prepareStringForConversion(const std::u16string& input) override {
		std::u16string admissablePart = stringTakeWhileAdmissable(getSortKey(input), u" \t\r\n0123456789,-");
		return stringReplace(admissablePart, u",", u".");
	}

	double convertStringToNumber(const std::u16string& input) override {
		return parseDouble(input);
	}
};

// Converts lines to double before sorting (assumes decimal dot).
class DecimalDotSorter : public NumericSorter<double>
{
public:
	DecimalDotSorter(bool isDescending, size_t fromColumn, size_t toColumn) : NumericSorter<double>(isDescending, fromColumn, toColumn) {}

protected:
	std::u16string prepareStringForConversion(const std::u16string& input) override {
		return stringTakeWhileAdmissable(getSortKey(input), u" \t\r\n0123456789.-");
	}

	double convertStringToNumber(const std::u16string& input) override {
		return parseDouble(input);
	}
};

class ReverseSorter : public ISorter
{
public:
	ReverseSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn) {}

	void sort(std::vector<std::u16string>& lines) override {
		std::reverse(lines.begin(), lines.end());
	}
};

class RandomSorter : public ISorter
{
public:
	unsigned seed;

	RandomSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn) {
		seed = static_cast<unsigned>(std::time(nullptr));
	}

	void sort(std::vector<std::u16string>& lines) override {
		std::shuffle(lines.begin(), lines.end(), std::default_random_engine(seed));
	}
};

// Implementation of length wise sorting of lines.
class LineLengthSorter : public ISorter
{
public:
	LineLengthSorter(bool isDescending, size_t fromColumn, size_t toColumn) : ISorter(isDescending, fromColumn, toColumn) {}

	void sort(std::vector<std::u16string>& lines) override {
		// When sorting specific columns, the effect would be to sort the lines whose length is between
		// fromColumn and toColumn. Any lines shorter than fromColumn would be considered length 0.
		// Any lines longer than toColumn would be considered same length (toColumn - fromColumn) and hence their
		// relative order would be preserved.
		if (isSortingSpecificColumns())
		{
			std::stable_sort(lines.begin(), lines.end(), [this](std::u16string a, std::u16string b)
				{
					if (isDescending())
					{
						return getSortKey(a).length() > getSortKey(b).length();

					}
					else
					{
						return getSortKey(a).length() < getSortKey(b).length();
					}
				});
		}
		// Normal sorting by line length
		else
		{
			std::sort(lines.begin(), lines.end(), [this](std::u16string a, std::u16string b)
				{
					if (isDescending())
					{
						return a.length() > b.length();
					}
					else
					{
						return a.length() < b.length();
					}
				});
		}
	}
};

} // namespace npp::core
