#pragma once

struct MilesProjectHeader_Short_s
{
	int magic;
	int version;
};

struct MilesProjectHeader_v46_s
{
	int magic;
	int version;
	uint32_t fileSize;
	uint32_t hash;

	OffsetPtr_t controllers;
	OffsetPtr_t strings;
	OffsetPtr_t graphData;
	OffsetPtr_t buses;

	// the rest of these may be inaccurate as they are based on the titanfall 2 version of this struct
    OffsetPtr_t busNames;
    OffsetPtr_t busIndices;
    OffsetPtr_t busChildren;
    OffsetPtr_t filters;
    OffsetPtr_t appliedDucking;
    OffsetPtr_t duckNames;
    OffsetPtr_t ducks;
    OffsetPtr_t bankNames;
    OffsetPtr_t unk_68;
    OffsetPtr_t voiceLimitNames;

    OffsetPtr_t unk_80;
    OffsetPtr_t unk_88;

    OffsetPtr_t functions; // seems to be at the correct offset
    OffsetPtr_t languages;

    OffsetPtr_t unk_ptrs[8];

    uint32_t unkCount_E0;
    uint32_t unkCount_E4;

    uint32_t languageCount;
    uint32_t voiceLimitCount;
    uint32_t duckCount;
    uint32_t controllerCount;
    uint32_t busCount;

    // there's more but idk what they are
};

static_assert(offsetof(MilesProjectHeader_v46_s, unkCount_E0) == 0xE0);

class CMilesAudioProject
{
public:
	CMilesAudioProject() {};
	~CMilesAudioProject() = default;

	const bool ParseFile(const std::filesystem::path& path);
	const bool ParseFromHeader();

	int GetVersion() const { return fileVersion; };

	const std::vector<std::string>& GetLanguageNames() const { return languageNames; }

	const char* GetString(uint64_t offset) const
	{
		return reinterpret_cast<const char*>(stringTable) + offset;
	}

	template <typename T>
	const T* GetPtr(uint64_t offset) const
	{
		return reinterpret_cast<const T*>(m_fileBuf.get() + offset);
	}

	template <typename T>
	T* GetPtr(uint64_t offset)
	{
		return reinterpret_cast<T*>(m_fileBuf.get() + offset);
	}

	template <typename T>
	const T* GetPtr(const OffsetPtr_t& ptr) const
	{
		return reinterpret_cast<const T*>(m_fileBuf.get() + ptr.offset);
	}

	template <typename T>
	T* GetPtr(const OffsetPtr_t& ptr)
	{
		return reinterpret_cast<T*>(m_fileBuf.get() + ptr.offset);
	}

	// getters

	const std::filesystem::path& GetFilePath() const { return filePath; };

	const void* GetGraphData() const
	{
		return graphData;
	}

private:

	std::filesystem::path filePath;
	std::shared_ptr<char[]> m_fileBuf;

	std::vector<std::string> languageNames;

	uint32_t buildTag;

	void* graphData;

	const char* stringTable;

	int fileVersion;


	/*void Construct(const MilesBankHeader_v13_t* const header)
	{

	}

	void Construct(const MilesBankHeader_v28_t* const header)
	{

	}

	void Construct(const MilesBankHeader_v45_t* const header)
	{

	}*/

	void Construct(const MilesProjectHeader_v46_s* const header)
	{
		this->stringTable = GetPtr<const char>(header->strings);
		this->graphData = GetPtr<void>(header->graphData);

		for (uint32_t i = 0; i < header->languageCount; ++i)
		{
			int languageOffset = GetPtr<int>(header->languages)[i];
			this->languageNames.push_back(GetString(languageOffset));
		}
	}
};