#include <pch.h>
#include <game/rtech/utils/studio/studio_generic.h>
#include <game/rtech/assets/animseq_data.h>

void LoadAnimSeqDataAsset(CAssetContainer* const container, CAsset* const asset)
{
	UNUSED(container);

	CPakAsset* pakAsset = static_cast<CPakAsset*>(asset);

	AnimSeqDataAsset* animSeqDataAsset = nullptr;

	switch (pakAsset->version())
	{
	case 1:
	{
		AnimSeqDataAssetHeader_v1_t* hdr = reinterpret_cast<AnimSeqDataAssetHeader_v1_t*>(pakAsset->header());
		animSeqDataAsset = new AnimSeqDataAsset(hdr);
		break;
	}
	default:
	{
		assertm(false, "unaccounted asset version, will cause major issues!");
		return;
	}
	}

	pakAsset->setExtraData(animSeqDataAsset);
}

static const char* const s_PathPrefixASQD = s_AssetTypePaths.find(AssetType_t::ASQD)->second;
bool ExportAnimSeqDataAsset(CAsset* const asset, const int setting)
{
	UNUSED(setting);

	CPakAsset* pakAsset = static_cast<CPakAsset*>(asset);

	const AnimSeqDataAsset* const animSeqDataAsset = reinterpret_cast<AnimSeqDataAsset*>(pakAsset->extraData());
	assertm(animSeqDataAsset, "Extra data should be valid at this point.");

	if (animSeqDataAsset->dataSize == 0)
	{
		Log("animseq data %s had a size of 0, skipping...\n", pakAsset->GetAssetName().c_str());
		return true;
	}

	// Create exported path + asset path.
	std::filesystem::path exportPath = std::filesystem::current_path().append(EXPORT_DIRECTORY_NAME);
	const std::filesystem::path animPath(pakAsset->GetAssetName());
	const std::string animStem(animPath.stem().string());

	// truncate paths?
	if (g_ExportSettings.exportPathsFull)
		exportPath.append(animPath.parent_path().string());
	else
		exportPath.append(std::format("{}/{}", s_PathPrefixASQD, animStem));

	if (!CreateDirectories(exportPath))
	{
		assertm(false, "Failed to create asset directory.");
		return false;
	}

	exportPath.append(std::format("{}.asqd", animStem));

	StreamIO out(exportPath, eStreamIOMode::Write);
	out.write(animSeqDataAsset->data, animSeqDataAsset->dataSize);

	return true;
}

void InitAnimSeqDataAssetType()
{
	AssetTypeBinding_t type =
	{
		.type = 'dqsa',
		.headerAlignment = 8,
		.loadFunc = LoadAnimSeqDataAsset,
		.postLoadFunc = nullptr,
		.previewFunc = nullptr,
		.e = { ExportAnimSeqDataAsset, 0, nullptr, 0ull },
	};

	REGISTER_TYPE(type);
}

void ParseAnimSeqDataForSeqdesc(seqdesc_t* const seqdesc, const size_t boneCount)
{
	for (size_t i = 0; i < seqdesc->AnimCount(); i++)
	{
		animdesc_t* const animdesc = &seqdesc->anims.at(i);

		if (!animdesc->animDataAsset && animdesc->animData)
			continue;

		CPakAsset* const dataAsset = g_assetData.FindAssetByGUID<CPakAsset>(animdesc->animDataAsset);
		if (!dataAsset)
		{
			/*assertm(false, "animseq data was not loaded!");*/
			animdesc->animData = nullptr;
			continue;
		}

		AnimSeqDataAsset* const animSeqDataAsset = reinterpret_cast<AnimSeqDataAsset* const>(dataAsset->extraData());

		if (!animSeqDataAsset->data)
		{
			assertm(false, "animseq data ptr was invalid");
			continue;
		}

		animdesc->animData = animSeqDataAsset->data;

		int index = 0;

		if (animdesc->sectionframes)
		{
			for (int section = animdesc->SectionCount(true) - 1; section >= 0; section--)
			{
				const animsection_t* const pSection = &animdesc->sections.at(section);

				if (pSection->isExternal)
					continue;

				index = pSection->animindex;
			}
		}

		const uint8_t* const boneFlagArray = reinterpret_cast<const uint8_t* const>(animdesc->animData + index);
		const r5::mstudio_rle_anim_t* panim = reinterpret_cast<const r5::mstudio_rle_anim_t*>(&boneFlagArray[ANIM_BONEFLAG_SIZE(boneCount)]);

		for (size_t bone = 0; bone < boneCount; bone++)
		{
			panim = panim->pNext();
		}

		animSeqDataAsset->dataSize = reinterpret_cast<const char* const>(panim) - animdesc->animData;
	}
}