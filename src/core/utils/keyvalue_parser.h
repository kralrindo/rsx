#pragma once

namespace kv_parser
{
	constexpr int maxNumChildren = 64;
	constexpr size_t maxStringLength = MAX_PATH;

	enum TokenState_t : uint16_t
	{
		TOKEN_KEY,
		TOKEN_VALUE,
		TOKEN_ARRAY,
	};

	enum TokenType_t : uint16_t
	{
		TOKEN_NONE,
		TOKEN_STRING,
		TOKEN_INT,
		TOKEN_FLOAT,
	};

	struct TokenData_t
	{
		~TokenData_t()
		{
			if (dataType != TOKEN_STRING)
				return;

			FreeAllocArray(data.data_string);
		}

		union
		{
			void* data_none;
			char* data_string;
			int data_int;
			float data_float;
		} data;

		TokenType_t dataType;
	};

	class Token_t
	{
	public:
		Token_t() = default;
		Token_t(const char** const input, const TokenState_t tokenState);
		~Token_t()
		{
			FreeAllocArray(token);
			FreeAllocArray(children);
		}
		
		TokenData_t* token;
		int16_t numTokens;
		TokenState_t state;

		int numChildren;
		Token_t* children;

		Token_t& operator=(const Token_t&&) = delete;
		Token_t& operator=(Token_t&& tokie) noexcept
		{
			if (this != &tokie)
			{
				token = tokie.token;
				numTokens = tokie.numTokens;
				state = tokie.state;

				numChildren = tokie.numChildren;
				children = tokie.children;

				tokie.token = nullptr;
				tokie.children = nullptr;
			}

			return *this;
		}
		Token_t& operator=(const Token_t&) = delete;
		Token_t& operator=(Token_t& tokie) noexcept
		{
			if (this != &tokie)
			{
				token = tokie.token;
				numTokens = tokie.numTokens;
				state = tokie.state;

				numChildren = tokie.numChildren;
				children = tokie.children;

				tokie.token = nullptr;
				tokie.children = nullptr;
			}

			return *this;
		}

		void SerializeToken(CTextBuffer* const text) const;
		void Serialize(CTextBuffer* const text) const;

		void ReplaceToken(const void* const value, const TokenType_t tokenType, const int idx = 0);

		inline const bool CheckKey(const char* const key, const size_t length) const
		{
			assertm(state == TOKEN_KEY || state == TOKEN_ARRAY, "token was not key");
			const int success = strncmp(GetStringToken(), key, length);

			return success == 0 ? true : false;
		}

		const int16_t TokenCountFromToken(char* tokie);

		inline const int16_t GetTokenCount() const { return numTokens; }
		inline const char* const GetStringToken(const int idx = 0) const { return token[idx].dataType == TOKEN_STRING ? token[idx].data.data_string : nullptr; }
		inline const int GetIntToken(const int idx = 0) const { return token[idx].dataType == TOKEN_INT ? token[idx].data.data_int : 0; }
		inline const float GetFloatToken(const int idx = 0) const { return token[idx].dataType == TOKEN_FLOAT ? token[idx].data.data_float : 0.0f; }

		inline const Token_t* const GetValue() const
		{
			assertm(state == TOKEN_KEY && numChildren == 1, "key should be string (key name)");
			return children;
		}

		inline const char* const GetStringValue(const int idx = 0) const { return GetValue()->GetStringToken(idx); }
		inline const int GetIntValue(const int idx = 0) const { return GetValue()->GetIntToken(idx); }
		inline const float GetFloatValue(const int idx = 0) const { return GetValue()->GetFloatToken(idx); }

		inline const bool HasChild(const char* const name, const Token_t** output = nullptr) const
		{
			const Token_t* const pChildToken = GetChild(name);
			if (pChildToken == nullptr)
			{
				return false;
			}

			if (output)
			{
				*output = pChildToken;
			}

			return true;
		}
		const inline int GetNumChildren() const { return numChildren; }
		const Token_t* const GetChild(const int index) const { return children + index; }
		const Token_t* const GetChild(const char* const name) const
		{
			assertm(state == TOKEN_ARRAY, "not valid array");
			
			const size_t maxLength = strnlen_s(name, maxStringLength);

			int tokenId = -1;
			for (int i = 0; i < numChildren; i++)
			{
				const Token_t* const pChildKey = children + i;
				assertm(pChildKey->numTokens == 1, "key was array");
				assertm(pChildKey->token[0].dataType == TOKEN_STRING, "children should be string (key name)"); // todo: surely numbers can be keys though?

				// not the value we are looking for
				if (strncmp(pChildKey->GetStringToken(), name, maxLength))
				{
					continue;
				}

				tokenId = i;
			}

			if (tokenId == -1)
			{
				return nullptr;
			}

			return children + tokenId;
		}

	private:
		inline const bool IsQuoted(const char* tokie) const { return tokie[0] == '\"' ? true : false; }
		const bool IsWhitespace(const char character) const;
		const bool IsArray(const char character) const;
		inline const bool IsEOF(const char character) const { return character == '\0' ? true : false; };
		inline const bool IsComma(const char character) const { return character == ',' ? true : false; };

		void HandleValueToken(char* tokie);

		void SeekThroughQuotes(const char** input) const;
		void SeekThroughWhitespace(const char** input) const;
		char* GetQuotedToken(const char** input) const;
		char* GetWhitespaceToken(const char** input) const;
		char* GetToken(const char** input) const;

		void ShrinkChildren();
	};
}