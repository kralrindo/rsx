#pragma once

#pragma pack(push, 1)
struct LocalizationEntry_t
{
    uint64_t hash;
    int stringStartIndex; // wchar index at which the value for this entry starts
    int unkC;

};
#pragma pack(pop)

struct LocalizationHeader_t
{
    char* fileName;
    int numStrings;
    int f;
    size_t numEntries;
    LocalizationEntry_t* entries;
    wchar_t* strings;
    char unk28[16];
};

static_assert(sizeof(LocalizationHeader_t) == 56);

struct LocalizationAsset
{
    LocalizationAsset(LocalizationHeader_t* hdr) : fileName(hdr->fileName), numStrings(hdr->numStrings), numEntries(hdr->numEntries), entries(hdr->entries), strings(hdr->strings) {};

    char* fileName;
    int numStrings;
    size_t numEntries;
    LocalizationEntry_t* entries;
    wchar_t* strings;

    std::unordered_map<uint64_t, std::string> entryMap;

    std::string getName() { return fileName; };
};