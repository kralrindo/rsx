#pragma once

#include <pch.h>

constexpr size_t textBufferMaxIndentation = 32ull;

class CTextBuffer
{
public:
	CTextBuffer(char* const buf, const size_t bufSize) : buffer(buf), writer(buffer), saved(nullptr), size(bufSize), indentation(), numIndents(0u)
	{
		assertm(buffer && size, "no valid buffer provided");
	}

	~CTextBuffer() {}

	// get the remaining capacity of our buffer
	inline const size_t Capacity() const { return size - (writer - buffer); }
	inline const bool VerifyCapcity(const size_t dataSize) const
	{
		const size_t capacity = Capacity();

		assertm(capacity >= dataSize, "ran out of data");
		if (capacity < dataSize)
			return false;

		return true;
	}

	// writes data to the internal buffer (use for data before text)
	const void* const WriteBufferData(const void* data, const size_t dataSize)
	{
		// prevent duplicate data, basically this pointer is to this memory block
		if (data >= buffer && data < writer)
			return data;

		if (!VerifyCapcity(dataSize))
			return nullptr;

		const void* out = writer;

		memcpy_s(writer, Capacity(), data, dataSize);
		writer += dataSize;

		return out;
	}

	// write a string to the internal buffer
	const char* const WriteBufferString(const char* const str, const size_t MAX_STRING)
	{
		const size_t stringLength = strnlen_s(str, MAX_STRING);
		const void* const out = WriteBufferData(str, stringLength + 1);
		return reinterpret_cast<const char* const>(out);
	}

	inline void WriteString(const char* const str, const size_t length)
	{
		strncpy_s(Writer(), Capacity(), str, length);
		AdvanceWriter(length);
	}

	inline void WriteString(const char* const str)
	{
		const size_t length = strnlen_s(str, Capacity());
		WriteString(str, length);
	}

	void WriteFormatted(const char* const fmt, ...)
	{
		va_list args;
		va_start(args, fmt);

		const int length = vsnprintf(Writer(), Capacity(), fmt, args);

		if (length < 0)
		{
			assertm(false, "length too large");
			return;
		}

		AdvanceWriter(static_cast<size_t>(length));
	}

	inline void WriteCharacter(const char character)
	{
		*writer = character;
		AdvanceWriter(1);
	}

	inline void WriteIndentation()
	{
		if (numIndents == 0u)
			return;

		WriteString(indentation, numIndents);
	}

	inline void AdvanceWriter(const size_t count)
	{
		VerifyCapcity(count);
		writer += count;
	}
	inline char* const Writer() const { return writer; }

	inline void SetTextStart() { saved = writer; }
	inline const char* const Text() const { return saved; }
	inline const size_t TextLength() const { return saved ? writer - saved : 0ull; }
	inline void WriterToText() // null terminate string and move writer back to start
	{
		WriteCharacter('\0');
		writer = saved;
	}

	const uint32_t GetIndentation() const { return numIndents; }
	void SetIndenation(const uint32_t level)
	{
		assertm(level < textBufferMaxIndentation, "exceeded max indentation");

		if (level == numIndents)
		{
			return;
		}

		if (level > numIndents)
		{
			memset(indentation, '\t', level);
		}

		numIndents = level;
		indentation[numIndents] = '\0';
	}
	
	inline void IncreaseIndenation()
	{
		const uint32_t newIndentation = numIndents + 1u;
		if (newIndentation == textBufferMaxIndentation)
		{
			assertm(false, "exceeded max indentation");
			return;
		}

		SetIndenation(newIndentation);
	}
	inline void DecreaseIndenation()
	{
		if (numIndents == 0u)
		{
			assertm(false, "cannot decrease any further");
			return;
		}

		const uint32_t newIndentation = numIndents - 1u;
		SetIndenation(newIndentation);
	}

private:
	char* const buffer;
	char* writer;
	char* saved;

	const size_t size;

	char indentation[textBufferMaxIndentation];
	uint32_t numIndents;
};

// reference to CTextBuffer, write string with precomputed length
#define TEXTBUFFER_WRITE_STRING_CT(buf, str)			\
{														\
	constexpr const char* string = str;					\
	constexpr size_t length = strlen_ct(string);		\
	buf.WriteString(string, length);					\
}
	

// pointer to CTextBuffer, write string with precomputed length
#define PTEXTBUFFER_WRITE_STRING_CT(buf, str)			\
{														\
	constexpr const char* string = str;					\
	constexpr size_t length = strlen_ct(string);		\
	buf->WriteString(string, length);					\
}