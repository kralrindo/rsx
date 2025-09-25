#pragma once
#include <game/rtech/utils/studio/studio.h>
#include <game/rtech/utils/studio/studio_r1.h>
#include <game/rtech/utils/studio/studio_r2.h>
#include <game/rtech/utils/studio/studio_r5.h>
#include <game/rtech/utils/studio/studio_r5_v12.h>
#include <game/rtech/utils/studio/studio_r5_v16.h>

struct animmovement_t
{
	animmovement_t(const animmovement_t& movement);
	animmovement_t(const r1::mstudioframemovement_t* const movement);
	animmovement_t(const r5::mstudioframemovement_t* const movement, const int frameCount, const bool indexType);

	~animmovement_t()
	{
		// [rika]: make sure we're using sections in this
		if (sectioncount > 0)
		{
			FreeAllocArray(sections);
		}
	}

	const void* baseptr;
	float scale[4];

	union
	{
		int* sections;
		short offset[4];
	};
	int sectionframes;
	int sectioncount;

	inline int SectionIndex(const int frame) const { return frame / sectionframes; } // get the section index for this frame
	inline int SectionOffset(const int frame) const { return sections[SectionIndex(frame)]; }
	inline uint16_t* pSection(const int frame) const { return reinterpret_cast<uint16_t*>((char*)baseptr + SectionOffset(frame)); }

	inline const mstudioanimvalue_t* const pAnimvalue(const int i, const uint16_t* section) const { return (section[i] > 0) ? reinterpret_cast<const mstudioanimvalue_t* const>((char*)section + section[i]) : nullptr; }
	inline const mstudioanimvalue_t* const pAnimvalue(int i) const { return (offset[i] > 0) ? reinterpret_cast<const mstudioanimvalue_t* const>((char*)baseptr + offset[i]) : nullptr; }
};

struct animsection_t
{
	animsection_t() = default;
	animsection_t(const int index, const bool bExternal = false) : animindex(index), isExternal(bExternal) {};

	int animindex;
	bool isExternal;
};

struct animdesc_t
{
	animdesc_t(const animdesc_t& animdesc);
	animdesc_t(const r2::mstudioanimdesc_t* const animdesc);
	animdesc_t(const r5::mstudioanimdesc_v8_t* const animdesc);
	animdesc_t(const r5::mstudioanimdesc_v12_1_t* const animdesc, const char* const ext);
	animdesc_t(const r5::mstudioanimdesc_v16_t* const animdesc, const char* const ext);
	animdesc_t(const r5::mstudioanimdesc_v19_1_t* const animdesc, const char* const ext);

	~animdesc_t()
	{
		if (nullptr != movement) delete movement;
	}

	const void* baseptr; // for getting to the animations

	const char* name;

	float fps;
	int flags;

	int numframes;

	int nummovements;
	int movementindex;
	int framemovementindex;
	animmovement_t* movement;
	inline r1::mstudiomovement_t* const pMovement(int i) const { return reinterpret_cast<r1::mstudiomovement_t*>((char*)this + movementindex) + i; };

	int animindex;

	// data array, starting with per bone flags
	const char* const pAnimdataNoStall(int* const piFrame, int* const _UNUSED) const;			// v8 - v12
	const char* const pAnimdataStall_0(int* const piFrame, int* const _UNUSED) const;			// v12.1 - v18
	const char* const pAnimdataStall_1(int* const piFrame, int* const sectionFrameCount) const;	// v19
	const char* const pAnimdataStall_2(int* const piFrame, int* const sectionFrameCount) const;	// v19.1 - retail

	int sectionindex; // can be safely removed
	int sectionstallframes; // number of static frames inside the animation, the reset excluding the final frame are stored externally. when external data is not loaded(?)/found(?) it falls back on the last frame of this as a stall
	int sectionframes; // number of frames used in each fast lookup section, zero if not used
	std::vector<animsection_t> sections;
	inline const bool HasStall() const { return sectionstallframes > 0; }
	inline const animsection_t* pSection(const int i) const { return &sections.at(i); }

	inline const int SectionCount(const bool useAnimData = false) const
	{
		const int useTrailSection = (flags & eStudioAnimFlags::ANIM_DATAPOINT) ? false : true; // rle anims have an extra section at the end, with only the last frame in full types (Quaternion48, Vector48, etc)
		const int useStallSection = HasStall();

		const int maxFrameIndex = (numframes - sectionstallframes - 1); // subtract to get the last frame index outside of stall

		const int sectionBase = (maxFrameIndex / sectionframes); // basic section index
		const int sectionMaxIndex = sectionBase + useTrailSection + useStallSection; // max index of a section

		// [rika]: where we'd normally add one to get the count, in retail the first section omitted as it's expected to always be at offset 0 in animdata
		// [rika]: only retail animations will have this set, and it is not optional, if not set there is no animation data
		if (useAnimData)
			return sectionMaxIndex;

		return sectionMaxIndex + 1; // add one to make this max index a max count
	}

	const char* sectionDataExtra;
	const char* animData;
	uint64_t animDataAsset;

	size_t parsedBufferIndex;

	inline const float GetCycle(const int iframe) const
	{ 
		const float cycle = numframes > 1 ? static_cast<float>(iframe) / static_cast<float>(numframes - 1) : 0.0f;
		assertm(isfinite(cycle), "cycle was nan");
		return cycle;
	}
};
typedef const char* const (animdesc_t::* AnimdataFunc_t)(int* const, int* const) const;

enum AnimdataFuncType_t : uint8_t
{
	ANIM_FUNC_NOSTALL,
	ANIM_FUNC_STALL_BASEPTR,
	ANIM_FUNC_STALL_ANIMDATA,

	ANIM_FUNC_COUNT,
};

static AnimdataFunc_t s_AnimdataFuncs_RLE[AnimdataFuncType_t::ANIM_FUNC_COUNT] =
{
	&animdesc_t::pAnimdataNoStall,
	&animdesc_t::pAnimdataStall_0,
	&animdesc_t::pAnimdataStall_2,
};

static AnimdataFunc_t s_AnimdataFuncs_DP[AnimdataFuncType_t::ANIM_FUNC_COUNT] =
{
	&animdesc_t::pAnimdataNoStall,
	&animdesc_t::pAnimdataStall_1,
	&animdesc_t::pAnimdataStall_2,
};

struct seqdesc_t
{
	// todo: studio version 12.3 has a different event struct, will need to parse this for qc
	seqdesc_t() = default;
	seqdesc_t(const r2::mstudioseqdesc_t* const seqdesc);
	seqdesc_t(const r5::mstudioseqdesc_v8_t* const seqdesc);
	seqdesc_t(const r5::mstudioseqdesc_v8_t* const seqdesc, const char* const ext);
	seqdesc_t(const r5::mstudioseqdesc_v16_t* const seqdesc, const char* const ext);
	seqdesc_t(const r5::mstudioseqdesc_v18_t* const seqdesc, const char* const ext, const uint32_t version);

	seqdesc_t& operator=(seqdesc_t&& seqdesc) noexcept
	{
		if (this != &seqdesc)
		{
			baseptr = seqdesc.baseptr;
			szlabel = seqdesc.szlabel;
			szactivityname = seqdesc.szactivityname;
			flags = seqdesc.flags;
			weightlistindex = seqdesc.weightlistindex;

			anims.swap(seqdesc.anims);
			parsedData.move(seqdesc.parsedData);
		}

		return *this;
	}

	const void* baseptr;

	const char* szlabel;
	const char* szactivityname;

	int flags; // looping/non-looping flags

	int weightlistindex;
	const float* pWeightList() const
	{
		switch (weightlistindex)
		{
		case 1:
			return r5::s_StudioWeightList_1;
		case 3:
			return r5::s_StudioWeightList_3;
		default:
			return reinterpret_cast<const float*>((char*)baseptr + weightlistindex);
		}
	}
	const float* pWeight(const int i) const { return pWeightList() + i; };
	const float Weight(const int i) const { return *pWeight(i); };

	std::vector<animdesc_t> anims;
	CRamen parsedData;

	const int AnimCount() const { return static_cast<int>(anims.size()); }
};

struct studio_hw_groupdata_t
{
	studio_hw_groupdata_t() = default;
	studio_hw_groupdata_t(const r5::studio_hw_groupdata_v16_t* const group);
	studio_hw_groupdata_t(const r5::studio_hw_groupdata_v12_1_t* const group);

	int dataOffset;				// offset to this section in compressed vg
	int dataSizeCompressed;		// compressed size of this lod buffer in hwData
	int dataSizeDecompressed;	// decompressed size of this lod buffer in hwData

	eCompressionType dataCompression; // none and oodle, haven't seen anything else used.

	//
	uint8_t lodIndex;		// base lod idx?
	uint8_t lodCount;		// number of lods contained within this group
	uint8_t lodMap;		// lods in this group, each bit is a lod
};
static_assert(sizeof(studio_hw_groupdata_t) == 16);

struct studiohdr_generic_t
{
	studiohdr_generic_t() = default;
	studiohdr_generic_t(const r1::studiohdr_t* const pHdr, StudioLooseData_t* const pData);
	studiohdr_generic_t(const r2::studiohdr_t* const pHdr);
	studiohdr_generic_t(const r5::studiohdr_v8_t* const pHdr);
	studiohdr_generic_t(const r5::studiohdr_v12_1_t* const pHdr);
	studiohdr_generic_t(const r5::studiohdr_v12_2_t* const pHdr);
	studiohdr_generic_t(const r5::studiohdr_v12_4_t* const pHdr);
	studiohdr_generic_t(const r5::studiohdr_v14_t* const pHdr);
	studiohdr_generic_t(const r5::studiohdr_v16_t* const pHdr, int dataSizePhys, int dataSizeModel);
	studiohdr_generic_t(const r5::studiohdr_v17_t* const pHdr, int dataSizePhys, int dataSizeModel);

	const char* baseptr;	// studiohdr_t pointer and by extension mdl/rmdl ptr

	int szNameOffset;		// file path and interally stored path, this should match across the real directory, name[64] memeber, and sznameindex member.
	inline const char* const pszName() const { return baseptr + szNameOffset; }

	int length;				// file length in bytes

	Vector eyeposition;		// ideal eye position
	Vector illumposition;	// illumination center

	Vector hull_min;	// ideal movement hull size
	Vector hull_max;	// ideal movement hull size

	Vector view_bbmin;	// clipping bounding box
	Vector view_bbmax;	// clipping bounding box

	int flags;

	int boneCount;
	int boneOffset;		// offset to mstudiobone_t or mstudiobonehdr_t
	int boneDataOffset; // offset to mstudiobonedata_t (rmdl v16+)
	inline const char* const pBones() const { return baseptr + boneOffset; };
	inline const char* const pBoneData() const { return baseptr + boneDataOffset; };
	inline const char* const pLinearBone() const { return baseptr + linearBoneOffset; };

	int hitboxSetCount;
	int hitboxSetOffset;

	int localAnimationCount;
	int localAnimationOffset;	// offset to mstudioanimdesc_t (unused in apex, removed in retail)

	int localSequenceCount;
	int localSequenceOffset;	// offset to mstudioseqdesc_t

	int textureCount;
	int textureOffset;	// offset to mstudiotexture_t
	inline const char* const pTextures() const { return baseptr + textureOffset; }

	int cdTexturesCount;
	int cdTexturesOffset;
	inline const char* const pCdtexture(const int i) const { return baseptr + reinterpret_cast<const int* const>(baseptr + cdTexturesOffset)[i]; }

	int numSkinRef;			// skingroup width (how many textures/indices per group)
	int numSkinFamilies;	// number of skingroups
	int skinOffset;
	inline const int16_t* const pSkinref(const int i) const { return reinterpret_cast<const int16_t* const>(baseptr + skinOffset) + i; }
	inline const int16_t* const pSkinFamily(const int i) const { return pSkinref(numSkinRef * i); };
	inline const char* const pSkinName(const int i) const
	{
		// only stored for index 1 and up
		if (i == 0)
			return STUDIO_DEFAULT_SKIN_NAME;

		const int index = reinterpret_cast<const int* const>(pSkinFamily(0) + (IALIGN2(numSkinRef * numSkinFamilies)))[i - 1];

		const char* const name = baseptr + index;
		if (IsStringZeroLength(name))
			return STUDIO_NULL_SKIN_NAME;

		return name;
	}

	inline const char* const pSkinName_V16(const int i) const
	{
		// only stored for index 1 and up
		if (i == 0)
			return STUDIO_DEFAULT_SKIN_NAME;

		const uint16_t index = *(reinterpret_cast<const uint16_t* const>(pSkinFamily(numSkinFamilies)) + (i - 1));

		const char* const name = baseptr + FIX_OFFSET(index);
		if (IsStringZeroLength(name))
			return STUDIO_NULL_SKIN_NAME;

		return name;
	}

	// bodypart

	int localAttachmentCount;
	int localAttachmentOffset;	// offset to mstudioattachment_t

	// node

	int ikChainCount;
	int ikChainOffset;

	int localPoseParamCount;
	int localPoseParamOffset;

	int surfacePropOffset;	// offset to surface prop string
	inline const char* const pszSurfaceProp() const { return baseptr + surfacePropOffset; }

	int keyValueOffset;		// offset to keyvalues
	int keyValueSize;		// removed in later rmdl, keyvalues are null terminated
	inline const char* const KeyValueText() const { return baseptr + keyValueOffset; }

	int localIkAutoPlayLockCount;
	int localIkAutoPlayLockOffset;

	float mass;
	int contents;	// contents mask, see bspflags.h

	int includeModelCount;
	int includeModelOffset;

	uint8_t constdirectionallightdot;
	uint8_t rootLOD;
	uint8_t numAllowedRootLODs;

	uint8_t pad;

	float fadeDistance; // set to -1 to never fade. set above 0 if you want it to fade out, distance is in feet.
	float gatherSize;

	int	illumpositionattachmentindex;

	float flMaxEyeDeflection;

	int linearBoneOffset; // offset to mstudiolinearbone_t (null if not present)

	int srcBoneTransformCount;
	int srcBoneTransformOffset;
	inline const mstudiosrcbonetransform_t* const pSrcBoneTransform(const int i) const { return reinterpret_cast<const mstudiosrcbonetransform_t* const>(baseptr + srcBoneTransformOffset) + i; }

	int boneFollowerCount;
	int boneFollowerOffset;

	int vtxOffset; // VTX
	int vvdOffset; // VVD / IDSV
	int vvcOffset; // VVC / IDCV
	int vvwOffset; // index will come last after other vertex files
	int phyOffset; // VPHY / IVPS

	int vtxSize;
	int vvdSize;
	int vvcSize;
	int vvwSize;
	int phySize; // still used in models using vg

	size_t hwDataSize;
	
	int boneStateOffset;
	int boneStateCount;
	inline const uint8_t* const pBoneStates() const { return boneStateCount > 0 ? reinterpret_cast<uint8_t*>((char*)baseptr + boneStateOffset) : nullptr; }

	int bvhOffset;

	int groupCount;
	studio_hw_groupdata_t groups[8]; // early 'vg' will only have one group
};

// make x axis y axis, and y axis negative x axis
inline void StaticPropFlipFlop(Vector& in)
{
	const Vector tmp(in);

	in.x = tmp.y;
	in.y = -tmp.x;
}

// shouldn't append on first lod if name doesn't have _lod%i in it
inline void FixupExportLodNames(std::string& fileName, int lodLevel)
{
	char lodName[16]{};

	if (fileName.rfind("_lod0") != std::string::npos || fileName.rfind("_LOD0") != std::string::npos)
	{
		snprintf(lodName, 8, "%i", lodLevel);
		fileName.replace(fileName.length() - 1, 8, lodName);

		return;
	}

	snprintf(lodName, 8, "_lod%i", lodLevel);
	fileName.append(lodName);
}