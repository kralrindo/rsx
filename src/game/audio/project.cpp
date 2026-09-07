#include <pch.h>
#include "miles.h"
#include "project.h"

const bool CMilesAudioProject::ParseFile(const std::filesystem::path& path)
{
	Log("MRPJ: Trying to load file: %s\n", path.string().c_str());

	filePath = path;

	if (!FileSystem::ReadFileData(path.string(), &m_fileBuf))
		return false;

	MilesProjectHeader_Short_s* hdrShort = reinterpret_cast<MilesProjectHeader_Short_s*>(m_fileBuf.get());

	if (hdrShort->magic != 'CPRJ')
		return false;

	this->fileVersion = hdrShort->version;

	if (fileVersion < 0)
		return false;

	if (!this->ParseFromHeader())
	{
		Log("MPRJ: Tried to parse unimplemented file version %i.\n", hdrShort->version);
		return false;
	}
	else return true;
}

const bool CMilesAudioProject::ParseFromHeader()
{
	switch (fileVersion)
	{
	case 0x2E: // s30
	{
		this->Construct(GetPtr<MilesProjectHeader_v46_s>(0));

		return true;
	}
	}

	return false;
}