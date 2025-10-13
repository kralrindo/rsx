#include <pch.h>
#include <game/rtech/utils/studio/studio_generic.h>

// generic studio data
// movement
animmovement_t::animmovement_t(const animmovement_t & movement) : baseptr(movement.baseptr), sectionframes(movement.sectionframes), sectioncount(movement.sectioncount)
{
	std::memcpy(this->scale, movement.scale, sizeof(float) * 4);

	if (sectioncount > 0)
	{
		sections = new int[sectioncount];

		std::memcpy(sections, movement.sections, sizeof(int) * sectioncount);

		return;
	}

	std::memcpy(this->offset, movement.offset, sizeof(short) * 4);
}

animmovement_t::animmovement_t(const r1::mstudioframemovement_t* const movement) : baseptr(movement), sectionframes(0), sectioncount(0)
{
	std::memcpy(this->scale, movement->scale, sizeof(float) * 4);
	std::memcpy(this->offset, movement->offset, sizeof(short) * 4);
}

animmovement_t::animmovement_t(const r5::mstudioframemovement_t* const movement, const int frameCount, const bool indexType) : baseptr(movement), sectionframes(movement->sectionframes), sectioncount(movement->SectionCount(frameCount))
{
	std::memcpy(this->scale, movement->scale, sizeof(float) * 4);

	sections = new int[sectioncount];

	for (int i = 0; i < sectioncount; i++)
	{
		const int index = indexType ? FIX_OFFSET(reinterpret_cast<uint16_t*>((char*)baseptr + sizeof(r5::mstudioframemovement_t))[i]) : reinterpret_cast<int*>((char*)baseptr + sizeof(r5::mstudioframemovement_t))[i];

		sections[i] = index;
	}
}

// animdesc
animdesc_t::animdesc_t(const animdesc_t& animdesc) : baseptr(animdesc.baseptr), name(animdesc.name), fps(animdesc.fps), flags(animdesc.flags), numframes(animdesc.numframes), animindex(animdesc.animindex),
	sectionindex(animdesc.sectionindex), sectionframes(animdesc.sectionframes), sectionstallframes(animdesc.sectionstallframes), sectionDataExtra(animdesc.sectionDataExtra), animData(nullptr), animDataAsset(animdesc.animDataAsset),
	nummovements(animdesc.nummovements), movementindex(animdesc.movementindex), framemovementindex(animdesc.framemovementindex), movement(nullptr), parsedBufferIndex(animdesc.parsedBufferIndex)
{
	if (animdesc.sections.size() > 0)
	{
		sections.resize(animdesc.sections.size());
		std::memcpy(sections.data(), animdesc.sections.data(), sizeof(animsection_t) * sections.size());
	}

	if (nullptr != animdesc.movement)
		movement = new animmovement_t(*animdesc.movement);
}

animdesc_t::animdesc_t(const r2::mstudioanimdesc_t* const animdesc) : baseptr(reinterpret_cast<const void* const>(animdesc)), name(animdesc->pszName()), fps(animdesc->fps), flags(animdesc->flags), numframes(animdesc->numframes), animindex(animdesc->animindex),
	sectionindex(animdesc->sectionindex), sectionframes(animdesc->sectionframes), sectionstallframes(0), sectionDataExtra(nullptr), animData(nullptr), animDataAsset(0ull),
	nummovements(animdesc->nummovements), movementindex(animdesc->movementindex), framemovementindex(animdesc->framemovementindex), movement(nullptr), parsedBufferIndex(invalidNoodleIdx)
{
	flags |= eStudioAnimFlags::ANIM_VALID;

	if (sectionframes)
	{
		sections.reserve(SectionCount());

		for (int i = 0; i < SectionCount(); i++)
		{
			sections.emplace_back(animdesc->pSection(i)->animindex);
		}
	}

	if (framemovementindex && (flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT))
	{
		movement = new animmovement_t(animdesc->pFrameMovement());
	}
};

animdesc_t::animdesc_t(const r5::mstudioanimdesc_v8_t* const animdesc) : baseptr(reinterpret_cast<const void* const>(animdesc)), name(animdesc->pszName()), fps(animdesc->fps), flags(animdesc->flags), numframes(animdesc->numframes), animindex(animdesc->animindex),
	sectionindex(animdesc->sectionindex), sectionframes(animdesc->sectionframes), sectionstallframes(0), sectionDataExtra(nullptr), animData(nullptr), animDataAsset(0ull),
	nummovements(animdesc->nummovements), movementindex(animdesc->movementindex), framemovementindex(animdesc->framemovementindex), movement(nullptr), parsedBufferIndex(invalidNoodleIdx)
{
	// [rika]: there's no point to parse these without this flag, and we can't determine what the count will be properly
	// [rika]: it will also never get hit by a panim function, so no need to worry there
	if (sectionframes && (flags & eStudioAnimFlags::ANIM_VALID))
	{
		const int sectionCount = SectionCount();

		sections.reserve(sectionCount);

		for (int i = 0; i < sectionCount; i++)
		{
			sections.emplace_back(animdesc->pSection(i)->animindex);
			assertm(!(sections.back().animindex & 0xFF000000), "section count overflowed");
		}
	}

	if (framemovementindex && (flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT))
	{
		movement = new animmovement_t(animdesc->pFrameMovement(), numframes, false);
	}
};

animdesc_t::animdesc_t(const r5::mstudioanimdesc_v12_1_t* const animdesc, const char* const ext) : baseptr(reinterpret_cast<const void* const>(animdesc)), name(animdesc->pszName()), fps(animdesc->fps), flags(animdesc->flags), numframes(animdesc->numframes), animindex(animdesc->animindex),
	sectionindex(animdesc->sectionindex), sectionframes(animdesc->sectionframes), sectionstallframes(animdesc->sectionstallframes), sectionDataExtra(ext), animData(nullptr), animDataAsset(0ull),
	nummovements(animdesc->nummovements), movementindex(animdesc->movementindex), framemovementindex(animdesc->framemovementindex), movement(nullptr), parsedBufferIndex(invalidNoodleIdx)
{
	// [rika]: there's no point to parse these without this flag, and we can't determine what the count will be properly
	// [rika]: it will also never get hit by a panim function, so no need to worry there
	if (sectionframes && (flags & eStudioAnimFlags::ANIM_VALID))
	{
		const int sectionCount = SectionCount();

		sections.reserve(sectionCount);

		for (int i = 0; i < sectionCount; i++)
		{
			sections.emplace_back(animdesc->pSection(i)->animindex, animdesc->pSection(i)->isExternal);
		}
	}

	if (framemovementindex && (flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT))
	{
		movement = new animmovement_t(animdesc->pFrameMovement(), numframes, false);
	}
};

animdesc_t::animdesc_t(const r5::mstudioanimdesc_v16_t* const animdesc, const char* const ext) : baseptr(reinterpret_cast<const void* const>(animdesc)), name(animdesc->pszName()), fps(animdesc->fps), flags(animdesc->flags), numframes(animdesc->numframes), animindex(animdesc->animindex),
	sectionindex(static_cast<int>(FIX_OFFSET(animdesc->sectionindex))), sectionframes(static_cast<int>(animdesc->sectionframes)), sectionstallframes(static_cast<int>(animdesc->sectionstallframes)), sectionDataExtra(ext), animData(nullptr), animDataAsset(0ull),
	nummovements(0), movementindex(0), framemovementindex(FIX_OFFSET(animdesc->framemovementindex)), movement(nullptr), parsedBufferIndex(invalidNoodleIdx)
{
	assertm(!(animdesc->animindex & 0x80000000), "non section anim with external data");

	// [rika]: there's no point to parse these without this flag, and we can't determine what the count will be properly
	// [rika]: it will also never get hit by a panim function, so no need to worry there
	if (sectionframes && (flags & eStudioAnimFlags::ANIM_VALID))
	{
		const int sectionCount = SectionCount();

		sections.reserve(sectionCount);

		for (int i = 0; i < sectionCount; i++)
		{
			sections.emplace_back(animdesc->pSection(i)->Index(), animdesc->pSection(i)->isExternal());
		}
	}

	if (framemovementindex && (flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT))
	{
		movement = new animmovement_t(animdesc->pFrameMovement(), numframes, true);
	}
};

animdesc_t::animdesc_t(const r5::mstudioanimdesc_v19_1_t* const animdesc, const char* const ext) : baseptr(reinterpret_cast<const void* const>(animdesc)), name(animdesc->pszName()), fps(animdesc->fps), flags(animdesc->flags), numframes(animdesc->numframes), animindex(0),
	sectionindex(static_cast<int>(FIX_OFFSET(animdesc->sectionindex))), sectionframes(static_cast<int>(animdesc->sectionframes)), sectionstallframes(static_cast<int>(animdesc->sectionstallframes)), sectionDataExtra(ext), animData(nullptr), animDataAsset(animdesc->animDataAsset),
	nummovements(0), movementindex(0), framemovementindex(FIX_OFFSET(animdesc->framemovementindex)), movement(nullptr), parsedBufferIndex(invalidNoodleIdx)
{
	// [rika]: there's no point to parse these without this flag, and we can't determine what the count will be properly
	// [rika]: it will also never get hit by a panim function, so no need to worry there
	if (sectionframes && (flags & eStudioAnimFlags::ANIM_VALID))
	{
		const int sectionCount = SectionCount(true);

		sections.reserve(sectionCount);

		for (int i = 0; i < sectionCount; i++)
		{
			sections.emplace_back(animdesc->pSection(i)->Index(), animdesc->pSection(i)->isExternal());
		}
	}

	if (framemovementindex && (flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT))
	{
		movement = new animmovement_t(animdesc->pFrameMovement(), numframes, true);
	}
};

const char* const animdesc_t::pAnimdataNoStall(int* const piFrame, int* const _UNUSED) const
{
	UNUSED(_UNUSED);

	int index = animindex;
	int section = 0;

	if (sectionframes != 0)
	{
		if (numframes > sectionframes && *piFrame == numframes - 1)
		{
			// last frame on long anims is stored separately
			*piFrame = 0;
			section = (numframes - 1) / sectionframes + 1;
		}
		else
		{
			section = *piFrame / sectionframes;
			*piFrame -= section * sectionframes;
		}

		index = pSection(section)->animindex;
		assertm(pSection(section)->isExternal == false, "anim can't have stall frames");
	}

	return ((char*)baseptr + index);
}

// [rika]: the inital revision of pAnimdataStall, does not support datapoint animations
const char* const animdesc_t::pAnimdataStall_0(int* const piFrame, int* const _UNUSED) const
{
	UNUSED(_UNUSED);

	int index = animindex;
	int section = 0;

	if (sectionframes != 0)
	{
		if (*piFrame >= sectionstallframes)
		{
			if (numframes > sectionframes && *piFrame == numframes - 1)
			{
				*piFrame = 0;
				section = (numframes - sectionstallframes - 1) / sectionframes + 2;
			}
			else
			{
				section = (*piFrame - sectionstallframes) / sectionframes + 1;
				*piFrame = *piFrame - sectionframes * ((*piFrame - sectionstallframes) / sectionframes) - sectionstallframes;
			}

			index = pSection(section)->animindex;
		}

		if (pSection(section)->isExternal) // checks if it is external
		{
			if (sectionDataExtra)
				return (sectionDataExtra + index);

			// we will stall if this is not loaded, for whatever reason
			index = pSection(0)->animindex;
			*piFrame = sectionstallframes - 1; // gets set to last frame of 'static'/'stall' section if the external data offset has not been cached(?)
		}
	}

	return ((char*)baseptr + index);
}

// [rika]: revision that appeared when datapoint animations were added, does not read the trailing section of rle animations, and stores the section's frame length
const char* const animdesc_t::pAnimdataStall_1(int* const piFrame, int* const pSectionLength) const
{
	int index = animindex;
	int section = 0;
	int sectionlength = numframes;

	if (sectionframes)
	{
		if (*piFrame >= sectionstallframes)
		{
			const int sectionbase = (*piFrame - sectionstallframes) / sectionframes;
			section = sectionbase + 1;

			const int frameoffset = sectionstallframes + (sectionbase * sectionframes);
			*piFrame -= frameoffset;

			const int remainingframes = numframes - frameoffset;

			sectionlength = sectionframes;
			if (remainingframes <= sectionframes)
				sectionlength = remainingframes;
		}
		else
		{
			section = 0;
			sectionlength = sectionstallframes;
		}

		index = pSection(section)->animindex;

		*pSectionLength = sectionlength;
		if (pSection(section)->isExternal) // checks if it is external
		{
			if (sectionDataExtra)
				return (sectionDataExtra + index);

			// we will stall if this is not loaded, for whatever reason
			index = pSection(0)->animindex;
			*piFrame = sectionstallframes - 1; // gets set to last frame of 'static'/'stall' section if the external data offset has not been cached(?)
		}
	}

	*pSectionLength = sectionlength;
	return ((char*)baseptr + index);
}

// [rika]: built upon the previous revision, but animindex is missing along with the first section, they're always assumed to be in the animdata asset
const char* const animdesc_t::pAnimdataStall_2(int* const piFrame, int* const pSectionLength) const
{
	int index = 0;
	int section = 0;
	int sectionlength = numframes;

	if (sectionframes)
	{
		if (*piFrame >= sectionstallframes)
		{
			const int sectionbase = (*piFrame - sectionstallframes) / sectionframes;
			section = sectionbase + 1;

			const int frameoffset = sectionstallframes + (sectionbase * sectionframes);
			*piFrame -= frameoffset;

			const int remainingframes = numframes - frameoffset;

			sectionlength = sectionframes;
			if (remainingframes <= sectionframes)
				sectionlength = remainingframes;
		}
		else
		{
			section = 0;
			sectionlength = sectionstallframes;
		}

		if (section > 0)
		{
			section--;

			index = pSection(section)->animindex;

			*pSectionLength = sectionlength;
			if (pSection(section)->isExternal) // checks if it is external
			{
				if (sectionDataExtra)
					return (sectionDataExtra + index);

				// we will stall if this is not loaded, for whatever reason
				index = 0;
				*piFrame = sectionstallframes - 1; // gets set to last frame of 'static'/'stall' section if the external data offset has not been cached(?)
			}
		}
	}

	*pSectionLength = sectionlength;
	return ((char*)animData + index);
}

// seqdesc
seqdesc_t::seqdesc_t(const r2::mstudioseqdesc_t* const seqdesc) : baseptr(reinterpret_cast<const void* const>(seqdesc)), szlabel(seqdesc->pszLabel()), szactivityname(seqdesc->pszActivityName()), flags(seqdesc->flags), weightlistindex(seqdesc->weightlistindex), parsedData(seqdesc->AnimCount())
{
	const r2::studiohdr_t* const pStudioHdr = reinterpret_cast<const r2::studiohdr_t* const>(reinterpret_cast<const char* const>(seqdesc) + seqdesc->baseptr);

	for (int i = 0; i < seqdesc->AnimCount(); i++)
	{
		const r2::mstudioanimdesc_t* const pAnimdesc = pStudioHdr->pAnimdesc(seqdesc->GetAnimIndex(i));

		// no need to sanity check here

		anims.emplace_back(pAnimdesc);
	}
};

#define ANIMDESC_SANITY_CHECK(anim) (anim->fps < 0.0f || anim->fps > 2048.f || anim->numframes < 0 || anim->numframes > 0x20000)
seqdesc_t::seqdesc_t(const r5::mstudioseqdesc_v8_t* const seqdesc) : baseptr(reinterpret_cast<const void* const>(seqdesc)), szlabel(seqdesc->pszLabel()), szactivityname(seqdesc->pszActivity()), flags(seqdesc->flags), weightlistindex(seqdesc->weightlistindex), parsedData(seqdesc->AnimCount())
{
	for (int i = 0; i < seqdesc->AnimCount(); i++)
	{
		const r5::mstudioanimdesc_v8_t* const pAnimdesc = seqdesc->pAnimDescV8(i);

		// sanity checks, there are sequences that have animations, but no data for the anim descriptions, and I am unsure how the game checks them.
		// fps can't be negative, fps practically shouldn't be more than 2048, 128k frames is an absurd amount, so this is a very good check, since the number (int) should never have those last bits filled.
		if (ANIMDESC_SANITY_CHECK(pAnimdesc))
		{
			Log("Sequence %s had animation(s) (index %i), but no animation description, skipping...\n", seqdesc->pszLabel(), i);
			break;
		}

		anims.emplace_back(pAnimdesc);
	}
};

seqdesc_t::seqdesc_t(const r5::mstudioseqdesc_v8_t* const seqdesc, const char* const ext) : baseptr(reinterpret_cast<const void* const>(seqdesc)), szlabel(seqdesc->pszLabel()), szactivityname(seqdesc->pszActivity()), flags(seqdesc->flags), weightlistindex(seqdesc->weightlistindex), parsedData(seqdesc->AnimCount())
{
	for (int i = 0; i < seqdesc->AnimCount(); i++)
	{
		const r5::mstudioanimdesc_v12_1_t* const pAnimdesc = seqdesc->pAnimDescV12_1(i);

		// sanity checks, there are sequences that have animations, but no data for the anim descriptions, and I am unsure how the game checks them.
		// fps can't be negative, fps practically shouldn't be more than 2048, 128k frames is an absurd amount, so this is a very good check, since the number (int) should never have those last bits filled.
		if (ANIMDESC_SANITY_CHECK(pAnimdesc))
		{
			Log("Sequence %s had animation(s) (index %i), but no animation description, skipping...\n", seqdesc->pszLabel(), i);
			break;
		}

		anims.emplace_back(pAnimdesc, ext);
	}
};

seqdesc_t::seqdesc_t(const r5::mstudioseqdesc_v16_t* const seqdesc, const char* const ext) : baseptr(reinterpret_cast<const void* const>(seqdesc)), szlabel(seqdesc->pszLabel()), szactivityname(seqdesc->pszActivity()), flags(seqdesc->flags), weightlistindex(FIX_OFFSET(seqdesc->weightlistindex)), parsedData(seqdesc->AnimCount())
{
	for (uint16_t i = 0; i < seqdesc->AnimCount(); i++)
	{
		const r5::mstudioanimdesc_v16_t* const pAnimdesc = seqdesc->pAnimDesc(i);

		// sanity checks, there are sequences that have animations, but no data for the anim descriptions, and I am unsure how the game checks them.
		// fps can't be negative, fps practically shouldn't be more than 2048, 128k frames is an absurd amount, so this is a very good check, since the number (int) should never have those last bits filled.
		if (ANIMDESC_SANITY_CHECK(pAnimdesc))
		{
			Log("Sequence %s had animation(s) (index %i), but no animation description, skipping...\n", seqdesc->pszLabel(), i);
			break;
		}

		anims.emplace_back(pAnimdesc, ext);
	}
};

seqdesc_t::seqdesc_t(const r5::mstudioseqdesc_v18_t* const seqdesc, const char* const ext, const uint32_t version) : baseptr(reinterpret_cast<const void* const>(seqdesc)), szlabel(seqdesc->pszLabel()), szactivityname(seqdesc->pszActivity()), flags(seqdesc->flags), weightlistindex(seqdesc->weightlistindex), parsedData(seqdesc->AnimCount())
{
	if (weightlistindex != 1 && weightlistindex != 3)
		weightlistindex = FIX_OFFSET(weightlistindex);

	switch (version)
	{
	case 0u:
	{
		for (uint16_t i = 0; i < seqdesc->AnimCount(); i++)
		{
			const r5::mstudioanimdesc_v16_t* const pAnimdesc = seqdesc->pAnimDesc(i);

			// sanity checks, there are sequences that have animations, but no data for the anim descriptions, and I am unsure how the game checks them.
			// fps can't be negative, fps practically shouldn't be more than 2048, 128k frames is an absurd amount, so this is a very good check, since the number (int) should never have those last bits filled.
			if (ANIMDESC_SANITY_CHECK(pAnimdesc))
			{
				Log("Sequence %s had animation(s) (index %i), but no animation description, skipping...\n", seqdesc->pszLabel(), i);
				break;
			}

			anims.emplace_back(pAnimdesc, ext);
		}

		break;
	}
	case 1u:
	{
		for (uint16_t i = 0; i < seqdesc->AnimCount(); i++)
		{
			const r5::mstudioanimdesc_v19_1_t* const pAnimdesc = seqdesc->pAnimDesc_V19_1(i);

			// sanity checks, there are sequences that have animations, but no data for the anim descriptions, and I am unsure how the game checks them.
			// fps can't be negative, fps practically shouldn't be more than 2048, 128k frames is an absurd amount, so this is a very good check, since the number (int) should never have those last bits filled.
			if (ANIMDESC_SANITY_CHECK(pAnimdesc))
			{
				Log("Sequence %s had animation(s) (index %i), but no animation description, skipping...\n", seqdesc->pszLabel(), i);
				break;
			}

			anims.emplace_back(pAnimdesc, ext);
		}

		break;
	}
	default:
	{
		assertm(false, "invalid input");
		break;
	}
	}	
};
#undef ANIMDESC_SANITY_CHECK // remove if needed in other files

// studio hw group
studio_hw_groupdata_t::studio_hw_groupdata_t(const r5::studio_hw_groupdata_v16_t* const group) : dataOffset(group->dataOffset), dataSizeCompressed(group->dataSizeCompressed), dataSizeDecompressed(group->dataSizeDecompressed), dataCompression(group->dataCompression),
lodIndex(group->lodIndex), lodCount(group->lodCount), lodMap(group->lodMap)
{

};

studio_hw_groupdata_t::studio_hw_groupdata_t(const r5::studio_hw_groupdata_v12_1_t* const group) : dataOffset(group->dataOffset), dataSizeCompressed(-1), dataSizeDecompressed(group->dataSize), dataCompression(eCompressionType::NONE),
lodIndex(static_cast<uint8_t>(group->lodIndex)), lodCount(static_cast<uint8_t>(group->lodCount)), lodMap(static_cast<uint8_t>(group->lodMap))
{

};

// studiohdr
// clamp silly studiomdl offsets (negative is base ptr)
#define FIX_FILE_OFFSET(offset) (offset < 0 ? 0 : offset)
studiohdr_generic_t::studiohdr_generic_t(const r1::studiohdr_t* const pHdr, StudioLooseData_t* const pData) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->pStudioHdr2()->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->pStudioHdr2()->linearboneindex), srcBoneTransformCount(pHdr->pStudioHdr2()->numsrcbonetransform), srcBoneTransformOffset(pHdr->pStudioHdr2()->srcbonetransformindex), boneFollowerCount(0), boneFollowerOffset(0),
	boneStateCount(0), boneStateOffset(0), bvhOffset(-1),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),
	
	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(pHdr->constdirectionallightdot), rootLOD(pHdr->rootLOD), numAllowedRootLODs(pHdr->numAllowedRootLODs),
	fadeDistance(pHdr->fadeDistance), gatherSize(0.0f), illumpositionattachmentindex(pHdr->pStudioHdr2()->illumpositionattachmentindex), flMaxEyeDeflection(pHdr->pStudioHdr2()->flMaxEyeDeflection),
	
	// file
	vtxOffset(pData->VertOffset(StudioLooseData_t::SLD_VTX)), vvdOffset(pData->VertOffset(StudioLooseData_t::SLD_VVD)), vvcOffset(pData->VertOffset(StudioLooseData_t::SLD_VVC)), vvwOffset(-1), phyOffset(pData->PhysOffset()),
	vtxSize(pData->VertSize(StudioLooseData_t::SLD_VTX)), vvdSize(pData->VertSize(StudioLooseData_t::SLD_VVD)), vvcSize(pData->VertSize(StudioLooseData_t::SLD_VVC)), vvwSize(-1), phySize(pData->PhysSize()), hwDataSize(0),
	groupCount(0), groups(),

	pad(), smallIndices(false)
{
	// update offsets local to studiohdr2_t
	const int baseOffset = static_cast<int>(reinterpret_cast<const char* const>(pHdr->pStudioHdr2()) - baseptr);

	szNameOffset -= baseOffset;
	linearBoneOffset -= baseOffset;
};

studiohdr_generic_t::studiohdr_generic_t(const r2::studiohdr_t* const pHdr) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->linearboneindex), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(pHdr->srcbonetransformindex), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(pHdr->boneFollowerOffset),
	boneStateCount(0), boneStateOffset(0), bvhOffset(-1),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),
	
	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(pHdr->constdirectionallightdot), rootLOD(pHdr->rootLOD), numAllowedRootLODs(pHdr->numAllowedRootLODs),
	fadeDistance(pHdr->fadeDistance), gatherSize(0.0f), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(pHdr->vtxOffset), vvdOffset(pHdr->vvdOffset), vvcOffset(pHdr->vvcOffset), vvwOffset(-1), phyOffset(pHdr->phyOffset),
	vtxSize(pHdr->vtxSize), vvdSize(pHdr->vvdSize), vvcSize(pHdr->vvcSize), vvwSize(-1), phySize(pHdr->phySize), hwDataSize(0),
	groupCount(0), groups(),

	pad(), smallIndices(false)
{

};

studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v8_t* const pHdr) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->linearboneindex), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(pHdr->srcbonetransformindex), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(pHdr->boneFollowerOffset),
	boneStateCount(0), boneStateOffset(0), bvhOffset(pHdr->bvhOffset),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),
	
	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(pHdr->constdirectionallightdot), rootLOD(pHdr->rootLOD), numAllowedRootLODs(pHdr->numAllowedRootLODs), 
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),
	
	// file
	vtxOffset(FIX_FILE_OFFSET(pHdr->vtxOffset)), vvdOffset(FIX_FILE_OFFSET(pHdr->vvdOffset)), vvcOffset(FIX_FILE_OFFSET(pHdr->vvcOffset)), vvwOffset(FIX_FILE_OFFSET(pHdr->vvwOffset)), phyOffset(FIX_FILE_OFFSET(pHdr->phyOffset)),
	vtxSize(pHdr->vtxSize), vvdSize(pHdr->vvdSize), vvcSize(pHdr->vvcSize), vvwSize(pHdr->vvwSize), phySize(pHdr->phySize), hwDataSize(0)/*set in vertex parsing*/,
	groupCount(1), groups(),

	pad(), smallIndices(false)
{

};

studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v12_1_t* const pHdr) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->linearboneindex), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(pHdr->srcbonetransformindex), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(pHdr->boneFollowerOffset),
	boneStateOffset(offsetof(r5::studiohdr_v12_1_t, boneStateOffset) + pHdr->boneStateOffset), boneStateCount(pHdr->boneStateCount), bvhOffset(pHdr->bvhOffset),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),

	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(0), rootLOD(0), numAllowedRootLODs(0),
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(FIX_FILE_OFFSET(pHdr->vtxOffset)), vvdOffset(FIX_FILE_OFFSET(pHdr->vvdOffset)), vvcOffset(FIX_FILE_OFFSET(pHdr->vvcOffset)), vvwOffset(FIX_FILE_OFFSET(pHdr->vvwOffset)), phyOffset(FIX_FILE_OFFSET(pHdr->phyOffset)),
	vtxSize(pHdr->vtxSize), vvdSize(pHdr->vvdSize), vvcSize(pHdr->vvcSize), vvwSize(pHdr->vvwSize), phySize(pHdr->phySize), hwDataSize(pHdr->hwDataSize),
	groupCount(pHdr->groupHeaderCount), groups(),

	pad(), smallIndices(false)
{
	assertm(pHdr->groupHeaderCount <= 8, "model has more than 8 lods");

	for (int i = 0; i < groupCount; i++)
	{
		if (i == 8)
			break;

		groups[i] = studio_hw_groupdata_t(pHdr->pLODGroup(i));
	}
};

studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v12_2_t* const pHdr) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->linearboneindex), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(pHdr->srcbonetransformindex), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(pHdr->boneFollowerOffset),
	boneStateOffset(offsetof(r5::studiohdr_v12_2_t, boneStateOffset) + pHdr->boneStateOffset), boneStateCount(pHdr->boneStateCount), bvhOffset(pHdr->bvhOffset),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),

	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(0), rootLOD(0), numAllowedRootLODs(0),
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(FIX_FILE_OFFSET(pHdr->vtxOffset)), vvdOffset(FIX_FILE_OFFSET(pHdr->vvdOffset)), vvcOffset(FIX_FILE_OFFSET(pHdr->vvcOffset)), vvwOffset(FIX_FILE_OFFSET(pHdr->vvwOffset)), phyOffset(FIX_FILE_OFFSET(pHdr->phyOffset)),
	vtxSize(pHdr->vtxSize), vvdSize(pHdr->vvdSize), vvcSize(pHdr->vvcSize), vvwSize(pHdr->vvwSize), phySize(pHdr->phySize), hwDataSize(pHdr->hwDataSize),
	groupCount(pHdr->groupHeaderCount), groups(),

	pad(), smallIndices(false)
{
	assertm(pHdr->groupHeaderCount <= 8, "model has more than 8 lods");

	for (int i = 0; i < groupCount; i++)
	{
		if (i == 8)
			break;

		groups[i] = studio_hw_groupdata_t(pHdr->pLODGroup(i));
	}
};

// [rika]: v12.4 and v12.5
studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v12_4_t* const pHdr) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->linearboneindex), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(pHdr->srcbonetransformindex), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(pHdr->boneFollowerOffset),
	boneStateOffset(offsetof(r5::studiohdr_v12_4_t, boneStateOffset) + pHdr->boneStateOffset), boneStateCount(pHdr->boneStateCount), bvhOffset(pHdr->bvhOffset),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),

	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(0), rootLOD(0), numAllowedRootLODs(0),
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(FIX_FILE_OFFSET(pHdr->vtxOffset)), vvdOffset(FIX_FILE_OFFSET(pHdr->vvdOffset)), vvcOffset(FIX_FILE_OFFSET(pHdr->vvcOffset)), vvwOffset(FIX_FILE_OFFSET(pHdr->vvwOffset)), phyOffset(FIX_FILE_OFFSET(pHdr->phyOffset)),
	vtxSize(pHdr->vtxSize), vvdSize(pHdr->vvdSize), vvcSize(pHdr->vvcSize), vvwSize(pHdr->vvwSize), phySize(pHdr->phySize), hwDataSize(pHdr->hwDataSize),
	groupCount(pHdr->groupHeaderCount), groups(),

	pad(), smallIndices(false)
{
	assertm(pHdr->groupHeaderCount <= 8, "model has more than 8 lods");

	for (int i = 0; i < groupCount; i++)
	{
		if (i == 8)
			break;

		groups[i] = studio_hw_groupdata_t(pHdr->pLODGroup(i));
	}
};

studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v14_t* const pHdr) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(pHdr->length), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(pHdr->eyeposition), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->numbones), boneOffset(pHdr->boneindex), boneDataOffset(-1), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(pHdr->hitboxsetindex), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(pHdr->localattachmentindex),
	linearBoneOffset(pHdr->linearboneindex), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(pHdr->srcbonetransformindex), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(pHdr->boneFollowerOffset),
	boneStateOffset(offsetof(r5::studiohdr_v14_t, boneStateOffset) + pHdr->boneStateOffset), boneStateCount(pHdr->boneStateCount), bvhOffset(pHdr->bvhOffset),

	// animations
	includeModelCount(pHdr->numincludemodels), includeModelOffset(pHdr->includemodelindex), localAnimationCount(pHdr->numlocalanim), localAnimationOffset(pHdr->localanimindex), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(pHdr->localseqindex),
	ikChainCount(pHdr->numikchains), ikChainOffset(pHdr->ikchainindex), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(pHdr->localposeparamindex), localIkAutoPlayLockCount(pHdr->numlocalikautoplaylocks), localIkAutoPlayLockOffset(pHdr->localikautoplaylockindex),

	// material
	textureCount(pHdr->numtextures), textureOffset(pHdr->textureindex), cdTexturesCount(pHdr->numcdtextures), cdTexturesOffset(pHdr->cdtextureindex), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(pHdr->skinindex),

	// misc
	surfacePropOffset(pHdr->surfacepropindex), keyValueOffset(pHdr->keyvalueindex), keyValueSize(pHdr->keyvaluesize), constdirectionallightdot(0), rootLOD(0), numAllowedRootLODs(0),
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(FIX_FILE_OFFSET(pHdr->vtxOffset)), vvdOffset(FIX_FILE_OFFSET(pHdr->vvdOffset)), vvcOffset(FIX_FILE_OFFSET(pHdr->vvcOffset)), vvwOffset(FIX_FILE_OFFSET(pHdr->vvwOffset)), phyOffset(FIX_FILE_OFFSET(pHdr->phyOffset)),
	vtxSize(pHdr->vtxSize), vvdSize(pHdr->vvdSize), vvcSize(pHdr->vvcSize), vvwSize(pHdr->vvwSize), phySize(pHdr->phySize), hwDataSize(pHdr->hwDataSize),
	groupCount(pHdr->groupHeaderCount), groups(),

	pad(), smallIndices(false)
{
	assertm(pHdr->groupHeaderCount <= 8, "model has more than 8 lods");

	for (int i = 0; i < groupCount; i++)
	{
		if (i == 8)
			break;

		groups[i] = studio_hw_groupdata_t(pHdr->pLODGroup(i));
	}
};
#undef FIX_FILE_OFFSET

studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v16_t* const pHdr, int dataSizePhys, int dataSizeModel) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(dataSizeModel), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(0.0f), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->boneCount), boneOffset(FIX_OFFSET(pHdr->boneHdrOffset)), boneDataOffset(FIX_OFFSET(pHdr->boneDataOffset)), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(FIX_OFFSET(pHdr->hitboxsetindex)), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(FIX_OFFSET(pHdr->localattachmentindex)),
	linearBoneOffset(FIX_OFFSET(pHdr->linearboneindex)), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(FIX_OFFSET(pHdr->srcbonetransformindex)), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(FIX_OFFSET(pHdr->boneFollowerOffset)),
	boneStateOffset(FIX_OFFSET(pHdr->boneStateOffset)), boneStateCount(pHdr->boneStateCount), bvhOffset(FIX_OFFSET(pHdr->bvhOffset)),

	// animations
	includeModelCount(0), includeModelOffset(0), localAnimationCount(0), localAnimationOffset(0), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(FIX_OFFSET(pHdr->localseqindex)),
	ikChainCount(pHdr->numikchains), ikChainOffset(FIX_OFFSET(pHdr->ikchainindex)), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(FIX_OFFSET(pHdr->localposeparamindex)), localIkAutoPlayLockCount(0), localIkAutoPlayLockOffset(0),

	// material
	textureCount(pHdr->numtextures), textureOffset(FIX_OFFSET(pHdr->textureindex)), cdTexturesCount(-1), cdTexturesOffset(-1), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(FIX_OFFSET(pHdr->skinindex)),

	// misc
	surfacePropOffset(FIX_OFFSET(pHdr->surfacepropindex)), keyValueOffset(FIX_OFFSET(pHdr->keyvalueindex)), keyValueSize(-1), constdirectionallightdot(0), rootLOD(0), numAllowedRootLODs(0),
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(-1), vvdOffset(-1), vvcOffset(-1), vvwOffset(-1), phyOffset(0),
	vtxSize(-1), vvdSize(-1), vvcSize(-1), vvwSize(-1), phySize(dataSizePhys), hwDataSize(0),
	groupCount(pHdr->groupHeaderCount), groups(),

	pad(), smallIndices(true)
{
	assertm(pHdr->groupHeaderCount <= 8, "model has more than 8 lods");

	for (int i = 0; i < groupCount; i++)
	{
		if (i == 8)
			break;

		groups[i] = studio_hw_groupdata_t(pHdr->pLODGroup(static_cast<uint16_t>(i)));
		hwDataSize += groups[i].dataSizeDecompressed;
	}
};

studiohdr_generic_t::studiohdr_generic_t(const r5::studiohdr_v17_t* const pHdr, int dataSizePhys, int dataSizeModel) : baseptr(reinterpret_cast<const char*>(pHdr)), szNameOffset(pHdr->sznameindex), length(dataSizeModel), flags(pHdr->flags), mass(pHdr->mass), contents(pHdr->contents),
	eyeposition(0.0f), illumposition(pHdr->illumposition), hull_min(pHdr->hull_min), hull_max(pHdr->hull_max), view_bbmin(pHdr->view_bbmin), view_bbmax(pHdr->view_bbmax),

	// bone
	boneCount(pHdr->boneCount), boneOffset(FIX_OFFSET(pHdr->boneHdrOffset)), boneDataOffset(FIX_OFFSET(pHdr->boneDataOffset)), hitboxSetCount(pHdr->numhitboxsets), hitboxSetOffset(FIX_OFFSET(pHdr->hitboxsetindex)), localAttachmentCount(pHdr->numlocalattachments), localAttachmentOffset(FIX_OFFSET(pHdr->localattachmentindex)),
	linearBoneOffset(FIX_OFFSET(pHdr->linearboneindex)), srcBoneTransformCount(pHdr->numsrcbonetransform), srcBoneTransformOffset(FIX_OFFSET(pHdr->srcbonetransformindex)), boneFollowerCount(pHdr->boneFollowerCount), boneFollowerOffset(FIX_OFFSET(pHdr->boneFollowerOffset)),
	boneStateOffset(FIX_OFFSET(pHdr->boneStateOffset)), boneStateCount(pHdr->boneStateCount), bvhOffset(FIX_OFFSET(pHdr->bvhOffset)),

	// animations
	includeModelCount(0), includeModelOffset(0), localAnimationCount(0), localAnimationOffset(0), localSequenceCount(pHdr->numlocalseq), localSequenceOffset(FIX_OFFSET(pHdr->localseqindex)),
	ikChainCount(pHdr->numikchains), ikChainOffset(FIX_OFFSET(pHdr->ikchainindex)), localPoseParamCount(pHdr->numlocalposeparameters), localPoseParamOffset(FIX_OFFSET(pHdr->localposeparamindex)), localIkAutoPlayLockCount(0), localIkAutoPlayLockOffset(0),

	// material
	textureCount(pHdr->numtextures), textureOffset(FIX_OFFSET(pHdr->textureindex)), cdTexturesCount(-1), cdTexturesOffset(-1), numSkinRef(pHdr->numskinref), numSkinFamilies(pHdr->numskinfamilies), skinOffset(FIX_OFFSET(pHdr->skinindex)),

	// misc
	surfacePropOffset(FIX_OFFSET(pHdr->surfacepropindex)), keyValueOffset(FIX_OFFSET(pHdr->keyvalueindex)), keyValueSize(-1), constdirectionallightdot(0), rootLOD(0), numAllowedRootLODs(0),
	fadeDistance(pHdr->fadeDistance), gatherSize(pHdr->gatherSize), illumpositionattachmentindex(pHdr->illumpositionattachmentindex), flMaxEyeDeflection(0.0f),

	// file
	vtxOffset(-1), vvdOffset(-1), vvcOffset(-1), vvwOffset(-1), phyOffset(0),
	vtxSize(-1), vvdSize(-1), vvcSize(-1), vvwSize(-1), phySize(dataSizePhys), hwDataSize(0),
	groupCount(pHdr->groupHeaderCount), groups(),

	pad(), smallIndices(true)
{
	assertm(pHdr->groupHeaderCount <= 8, "model has more than 8 lods");

	for (int i = 0; i < groupCount; i++)
	{
		if (i == 8)
			break;

		groups[i] = studio_hw_groupdata_t(pHdr->pLODGroup(static_cast<uint16_t>(i)));
		hwDataSize += groups[i].dataSizeDecompressed;
	}
};


// per version funcs that utilize generic data
namespace r1
{
	void Studio_FrameMovement(const animmovement_t* pFrameMovement, int frame, Vector& vecPos, float& yaw)
	{
		for (int i = 0; i < 3; i++)
		{
			ExtractAnimValue(frame, pFrameMovement->pAnimvalue(i), pFrameMovement->scale[i], vecPos[i]);
		}

		ExtractAnimValue(frame, pFrameMovement->pAnimvalue(3), pFrameMovement->scale[3], yaw);
	}

	void Studio_FrameMovement(const animmovement_t* pFrameMovement, int iFrame, Vector& v1Pos, Vector& v2Pos, float& v1Yaw, float& v2Yaw)
	{
		for (int i = 0; i < 3; i++)
		{
			ExtractAnimValue(iFrame, pFrameMovement->pAnimvalue(i), pFrameMovement->scale[i], v1Pos[i], v2Pos[i]);
		}

		ExtractAnimValue(iFrame, pFrameMovement->pAnimvalue(3), pFrameMovement->scale[3], v1Yaw, v2Yaw);
	}

	bool Studio_AnimPosition(const animdesc_t* const panim, float flCycle, Vector& vecPos, QAngle& vecAngle)
	{
		float	prevframe = 0;
		vecPos.Init();
		vecAngle.Init();

		int iLoops = 0;
		if (flCycle > 1.0)
		{
			iLoops = (int)flCycle;
		}
		else if (flCycle < 0.0)
		{
			iLoops = (int)flCycle - 1;
		}
		flCycle = flCycle - iLoops;

		float	flFrame = flCycle * (panim->numframes - 1);

		if (panim->flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT)
		{
			int iFrame = (int)flFrame;
			float s = (flFrame - iFrame);

			if (s == 0)
			{
				Studio_FrameMovement(panim->movement, iFrame, vecPos, vecAngle.y);
				return true;
			}
			else
			{
				Vector v1Pos, v2Pos;
				float v1Yaw, v2Yaw;

				Studio_FrameMovement(panim->movement, iFrame, v1Pos, v2Pos, v1Yaw, v2Yaw);

				vecPos = ((v2Pos - v1Pos) * s) + v1Pos; // this should work on paper?

				float yawDelta = AngleDiff(v2Yaw, v1Yaw);
				vecAngle.y = (yawDelta * s) + v1Yaw;

				return true;
			}
		}
		else
		{
			for (int i = 0; i < panim->nummovements; i++)
			{
				mstudiomovement_t* pmove = panim->pMovement(i);

				if (pmove->endframe >= flFrame)
				{
					float f = (flFrame - prevframe) / (pmove->endframe - prevframe);

					float d = pmove->v0 * f + 0.5f * (pmove->v1 - pmove->v0) * f * f;

					vecPos = vecPos + (pmove->vector * d);
					vecAngle.y = vecAngle.y * (1 - f) + pmove->angle * f;
					if (iLoops != 0)
					{
						mstudiomovement_t* pmove_1 = panim->pMovement(panim->nummovements - 1);
						vecPos = vecPos + (pmove_1->position * iLoops);
						vecAngle.y = vecAngle.y + iLoops * pmove_1->angle;
					}
					return true;
				}
				else
				{
					prevframe = static_cast<float>(pmove->endframe);
					vecPos = pmove->position;
					vecAngle.y = pmove->angle;
				}
			}
		}

		return false;
	}
}

namespace r5
{
	static void Studio_FrameMovement(const animmovement_t* pFrameMovement, int iFrame, Vector& vecPos, float& yaw)
	{
		uint16_t* section = pFrameMovement->pSection(iFrame);

		const int iLocalFrame = iFrame % pFrameMovement->sectionframes; // get the local frame for this section by doing a modulus (returns excess frames)

		for (int i = 0; i < 3; i++)
		{
			if (section[i])
				ExtractAnimValue(iLocalFrame, pFrameMovement->pAnimvalue(i, section), pFrameMovement->scale[i], vecPos[i]);
			else
				vecPos[i] = 0.0f;
		}

		if (section[3])
			ExtractAnimValue(iLocalFrame, pFrameMovement->pAnimvalue(3, section), pFrameMovement->scale[3], yaw);
		else
			yaw = 0.0f;
	}

	static void Studio_FrameMovement(const animmovement_t* pFrameMovement, int iFrame, Vector& v1Pos, Vector& v2Pos, float& v1Yaw, float& v2Yaw)
	{
		uint16_t* section = pFrameMovement->pSection(iFrame);

		const int iLocalFrame = iFrame % pFrameMovement->sectionframes; // get the local frame for this section by doing a modulus (returns excess frames)

		for (int i = 0; i < 3; i++)
		{
			if (section[i])
				ExtractAnimValue(iLocalFrame, pFrameMovement->pAnimvalue(i, section), pFrameMovement->scale[i], v1Pos[i], v2Pos[i]);
			else
			{
				v1Pos[i] = 0.0f;
				v2Pos[i] = 0.0f;
			}
		}

		if (section[3])
			ExtractAnimValue(iLocalFrame, pFrameMovement->pAnimvalue(3, section), pFrameMovement->scale[3], v1Yaw, v2Yaw);
		else
		{
			v1Yaw = 0.0f;
			v2Yaw = 0.0f;
		}
	}

	// [rika]: decode datapoint compressed position track for framemovement
	static void Studio_FrameMovement_DP_Pos(const float fFrame, const int sectionlength, const uint8_t** const panimtrack, Vector& pos)
	{
		const uint16_t* ptrack = *reinterpret_cast<const uint16_t** const>(panimtrack);

		const uint16_t valid = ptrack[0];
		const uint16_t total = ptrack[1];

		const uint16_t* pFrameIndices = ptrack + 2;
		*panimtrack = reinterpret_cast<const uint8_t*>(pFrameIndices);

		// [rika]: return zeros if no data
		if (!total)
		{
			pos.Init(0.0f, 0.0f, 0.0f);
			return;
		}

		int prevFrame = 0, nextFrame = 0;
		float s = 0.0f; // always init as 0!

		// [rika]: get data pointers
		const AnimPos64* pPackedData = nullptr;
		const AxisFixup_t* pAxisFixup = nullptr;

		CalcBoneInterpFrames_DP(prevFrame, nextFrame, s, fFrame, total, sectionlength, pFrameIndices, &pPackedData);

		pAxisFixup = reinterpret_cast<const AxisFixup_t*>(pPackedData + valid);

		// [rika]: see to our datapoint
		int validIdx = 0;
		uint32_t remainingFrames = 0;

		const uint32_t prevTarget = prevFrame;
		CalcBoneSeek_DP(pPackedData, validIdx, remainingFrames, /*valid,*/ prevTarget);

		Vector pos1;
		AnimPos64::Unpack(pos1, pPackedData[validIdx], &pAxisFixup[prevFrame]);

		// [rika]: check if we need interp or not
		if (prevFrame == nextFrame)
		{
			pos = pos1;
		}
		else
		{
			// [rika]: see to our datapoint (the sequal)
			const uint32_t nextTarget = (nextFrame - prevFrame) + remainingFrames; // to check if our current data will contain this interp data, seek until we have it
			CalcBoneSeek_DP(pPackedData, validIdx, remainingFrames, nextTarget);

			Vector pos2;
			AnimPos64::Unpack(pos2, pPackedData[validIdx], &pAxisFixup[nextFrame]);

			pos = pos1 * (1.0f - s) + pos2 * s;
		}

		// [rika]: advance the data ptr for other functions
		*panimtrack = reinterpret_cast<const uint8_t*>(pAxisFixup + total);
	}

	// [rika]: decode datapoint compressed yaw track for framemovement
	static void Studio_FrameMovement_DP_Rot(const float fFrame, const int sectionlength, const uint8_t** const panimtrack, float& yaw)
	{
		const uint16_t* ptrack = *reinterpret_cast<const uint16_t** const>(panimtrack);

		const uint16_t total = ptrack[0];

		const uint16_t* pFrameIndices = ptrack + 1;
		*panimtrack = reinterpret_cast<const uint8_t*>(pFrameIndices);

		// [rika]: return zeros if no data
		if (!total)
		{
			yaw = 0.0f;
			return;
		}

		int prevFrame = 0, nextFrame = 0;
		float s = 0.0f; // always init as 0!

		// [rika]: get data pointers
		const float16* pPackedData = nullptr;

		CalcBoneInterpFrames_DP(prevFrame, nextFrame, s, fFrame, total, sectionlength, pFrameIndices, &pPackedData);

		// [rika]: check if we need interp or not
		if (prevFrame == nextFrame)
		{
			yaw = pPackedData[prevFrame].GetFloat();
		}
		else
		{
			const float v1Yaw = pPackedData[prevFrame].GetFloat();
			const float v2Yaw = pPackedData[nextFrame].GetFloat();

			// [rika]: differs slightly from AngleDiff()
			float yawDelta = v2Yaw;
			if ((v2Yaw - v1Yaw) <= 180.0f)
			{
				if ((v1Yaw - v2Yaw) > 180.0f)
					yawDelta = v2Yaw + 360.0f;
			}
			else
			{
				yawDelta = v2Yaw + -360.0f;
			}

			yaw = v1Yaw * (1.0f - s) + yawDelta * s;
		}

		// [rika]: advance the data ptr for other functions
		*panimtrack = reinterpret_cast<const uint8_t*>(pPackedData + total);
	}

	bool Studio_AnimPosition(const animdesc_t* const panim, float flCycle, Vector& vecPos, QAngle& vecAngle)
	{
		vecPos.Init();
		vecAngle.Init();

		int iLoops = 0;
		if (flCycle > 1.0)
		{
			iLoops = static_cast<int>(flCycle);
		}
		else if (flCycle < 0.0)
		{
			iLoops = static_cast<int>(flCycle) - 1;
		}
		flCycle = flCycle - iLoops;

		float flFrame = flCycle * (panim->numframes - 1);

		if (panim->flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT)
		{
			int iFrame = static_cast<int>(flFrame);
			float s = (flFrame - iFrame);

			if (panim->flags & eStudioAnimFlags::ANIM_DATAPOINT)
			{
				const mstudioframemovement_t* const pFrameMovement = reinterpret_cast<const mstudioframemovement_t* const>((char*)panim->baseptr + panim->framemovementindex);

				// [rika]: remove after testing
#ifdef _DEBUG
				assertm(pFrameMovement->SectionCount(panim->numframes) == 1, "more than one section ?");
#endif // _DEBUG

				const uint8_t* panimtrack = pFrameMovement->pDatapointTrack();

				Studio_FrameMovement_DP_Pos(flFrame, panim->numframes, &panimtrack, vecPos);
				Studio_FrameMovement_DP_Rot(flFrame, panim->numframes, &panimtrack, vecAngle.y);

				return true;
			}
			else if (s == 0.0f)
			{
				Studio_FrameMovement(panim->movement, iFrame, vecPos, vecAngle.y);
				return true;
			}
			else
			{
				Vector v1Pos, v2Pos;
				float v1Yaw, v2Yaw;

				Studio_FrameMovement(panim->movement, iFrame, v1Pos, v2Pos, v1Yaw, v2Yaw);

				vecPos = ((v2Pos - v1Pos) * s) + v1Pos; // this should work on paper?

				float yawDelta = AngleDiff(v2Yaw, v1Yaw);
				vecAngle.y = (yawDelta * s) + v1Yaw;

				return true;
			}
		}

		assertm(false, "we should not be here");

		return false;
	}
}