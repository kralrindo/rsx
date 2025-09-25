#include <pch.h>
#include <core/mdl/modeldata.h>

#include <core/mdl/rmax.h>
#include <core/mdl/cast.h>
#include <core/mdl/smd.h>
#include <core/mdl/qc.h>

//#include <core/render/dx.h>
//#include <thirdparty/imgui/imgui.h>
#include <thirdparty/imgui/misc/imgui_utility.h>

extern CDXParentHandler* g_dxHandler;
extern CBufferManager g_BufferManager;
extern ExportSettings_t g_ExportSettings;
extern CPreviewDrawData g_currentPreviewDrawData;

//
// PARSEDDATA
//
#define VERT_DATA(t, d, o) reinterpret_cast<const t* const>(d + o)
void Vertex_t::ParseVertexFromVG(Vertex_t* const vert, VertexWeight_t* const weights, Vector2D* const texcoords, ModelMeshData_t* const mesh, const char* const rawVertexData, const uint8_t* const boneMap, const vvw::mstudioboneweightextra_t* const weightExtra, int& weightIdx)
{
	int offset = 0;

	// [rika]: older hwdata models used bit flags, but starting in season 11.1 (rmdl 13.1) it's treated more like an enum
	/*if (mesh->rawVertexLayoutFlags & VERT_POSITION_UNPACKED)
	{
		vert->position = *VERT_DATA(Vector, rawVertexData, offset);
		offset += sizeof(Vector);
	}

	if (mesh->rawVertexLayoutFlags & VERT_POSITION_PACKED)
	{
		vert->position = VERT_DATA(Vector64, rawVertexData, offset)->Unpack();
		offset += sizeof(Vector64);
	}*/

	const vg::eVertPositionType posType = static_cast<vg::eVertPositionType>(mesh->rawVertexLayoutFlags & 3);
	switch (posType)
	{
	case vg::eVertPositionType::VG_POS_NONE:
	{
		break;
	}
	case vg::eVertPositionType::VG_POS_UNPACKED:
	{
		vert->position = *VERT_DATA(Vector, rawVertexData, offset);
		offset += sizeof(Vector);

		break;
	}
	case vg::eVertPositionType::VG_POS_PACKED64:
	{
		vert->position = VERT_DATA(Vector64, rawVertexData, offset)->Unpack();
		offset += sizeof(Vector64);

		break;
	}
	case vg::eVertPositionType::VG_POS_PACKED48:
	{
		// [rika]: not sure the format on this one, currently only used on switch and I can't be asked to find tools to decompile a shader at this time
		vert->position = Vector(0.0f);
		offset += 0x6;

		break;
	}
	}

	assertm(nullptr != weights, "weight pointer should be valid");
	vert->weightIndex = weightIdx;

	// we have weight data
	// note: if for some reason 'VERT_BLENDWEIGHTS_UNPACKED' is encountered, weights would not be processed.
	assertm(!(mesh->rawVertexLayoutFlags & VERT_BLENDWEIGHTS_UNPACKED), "mesh had unpacked weights!");
	if (mesh->rawVertexLayoutFlags & (VERT_BLENDINDICES | VERT_BLENDWEIGHTS_PACKED))
	{
		const vg::BlendWeightsPacked_s* const blendWeights = VERT_DATA(vg::BlendWeightsPacked_s, rawVertexData, offset);
		const vg::BlendWeightIndices_s* const blendIndices = VERT_DATA(vg::BlendWeightIndices_s, rawVertexData, offset + 4);

		offset += 8;

		uint8_t curIdx = 0; // current weight
		uint16_t remaining = 32767; // 'weight' remaining to assign to the last bone

		// model has more than 3 weights per vertex
		if (nullptr != weightExtra)
		{
			assertm(blendIndices->boneCount < 16, "model had more than 16 bones on complex weights");

			// first weight, we will always have this
			weights[curIdx].bone = boneMap[blendIndices->bone[0]];
			weights[curIdx].weight = blendWeights->Weight(0);
			remaining -= blendWeights->weight[0];

			curIdx++;

			// only hit if we have over 2 bones/weights
			for (uint8_t i = curIdx; i < blendIndices->boneCount; i++)
			{
				weights[curIdx].bone = boneMap[weightExtra[blendWeights->Index() + (curIdx - 1)].bone];
				weights[curIdx].weight = weightExtra[blendWeights->Index() + (curIdx - 1)].Weight();

				remaining -= weightExtra[blendWeights->Index() + (curIdx - 1)].weight;

				curIdx++;
			}

			// only hit if we have over 1 bone/weight
			if (blendIndices->boneCount > 0)
			{
				weights[curIdx].bone = boneMap[blendIndices->bone[1]];
				weights[curIdx].weight = UNPACKWEIGHT(remaining);

				curIdx++;
			}
		}
		else
		{
			assertm(blendIndices->boneCount < 3, "model had more than 3 bones on simple weights");

			for (uint8_t i = 0; i < blendIndices->boneCount; i++)
			{
				weights[curIdx].bone = boneMap[blendIndices->bone[curIdx]];
				weights[curIdx].weight = blendWeights->Weight(curIdx);

				remaining -= blendWeights->weight[curIdx];

				curIdx++;
			}

			weights[curIdx].bone = boneMap[blendIndices->bone[curIdx]];
			weights[curIdx].weight = UNPACKWEIGHT(remaining);

			curIdx++;
		}

		vert->weightCount = curIdx;
		assert(static_cast<uint8_t>(vert->weightCount) == (blendIndices->boneCount + 1)); // numbones is really 'extra' bones on top of the base weight, verify the count is correct

		weightIdx += curIdx;
	}
	// our mesh does not have weight data, use a set of default weights. 
	// [rika]: this can only happen when a model has one bone
	else
	{
		vert->weightCount = 1;
		weights[0].bone = 0;
		weights[0].weight = 1.0f;

		weightIdx++;
	}

	mesh->weightsPerVert = static_cast<uint16_t>(vert->weightCount) > mesh->weightsPerVert ? static_cast<uint16_t>(vert->weightCount) : mesh->weightsPerVert;

	vert->normalPacked = *VERT_DATA(Normal32, rawVertexData, offset);
	offset += sizeof(Normal32);

	if (mesh->rawVertexLayoutFlags & VERT_COLOR)
	{
		// Vertex Colour
		vert->color = *VERT_DATA(Color32, rawVertexData, offset);
		offset += sizeof(Color32);
	}
	else // no vert colour, write default
	{
		vert->color = Color32(255, 255);
	}

	if (mesh->rawVertexLayoutFlags & VERT_TEXCOORD0)
	{
		vert->texcoord = *VERT_DATA(Vector2D, rawVertexData, offset);
		offset += sizeof(Vector2D);
	}

	for (int localIdx = 1, countIdx = 1; countIdx < mesh->texcoordCount; localIdx++)
	{
		assertm(nullptr != texcoords, "texcoord pointer should be valid");

		if (!VERT_TEXCOORDn(localIdx))
			continue;

		texcoords[countIdx - 1] = *VERT_DATA(Vector2D, rawVertexData, offset);
		offset += sizeof(Vector2D);

		countIdx++;
	}

	assertm(offset == mesh->vertCacheSize, "parsed data size differed from vertexCacheSize");
}
#undef VERT_DATA

// Generic (basic data shared between them)
void Vertex_t::ParseVertexFromVTX(Vertex_t* const vert, Vector2D* const texcoords, ModelMeshData_t* const mesh, const vvd::mstudiovertex_t* const pVerts, const Vector4D* const pTangs, const Color32* const pColors, const Vector2D* const pUVs, const int origId)
{
	vert->position = pVerts[origId].m_vecPosition;

	// not normal
	vert->normalPacked.PackNormal(pVerts[origId].m_vecNormal, pTangs[origId]);

	if (pColors)
		vert->color = pColors[origId];
	else // no vert colour, write default
		vert->color = Color32(255, 255);

	vert->texcoord = pVerts[origId].m_vecTexCoord;

	for (int localIdx = 1, countIdx = 1; countIdx < mesh->texcoordCount; localIdx++)
	{
		assertm(nullptr != texcoords, "texcoord pointer should be valid");

		if (!VERT_TEXCOORDn(localIdx))
			continue;

		texcoords[countIdx - 1] = pUVs[origId]; // [rika]: add proper support for uv3 (though I doubt we'll ever get files for it, I think it would be quite quirky.)

		countIdx++;
	}
}

// Basic Source
void Vertex_t::ParseVertexFromVTX(Vertex_t* const vert, VertexWeight_t* const weights, Vector2D* const texcoords, ModelMeshData_t* mesh, const OptimizedModel::Vertex_t* const pVertex, const vvd::mstudiovertex_t* const pVerts, const Vector4D* const pTangs, const Color32* const pColors, const Vector2D* const pUVs,
	int& weightIdx, const bool isHwSkinned, const OptimizedModel::BoneStateChangeHeader_t* const pBoneStates)
{
	const int origId = pVertex->origMeshVertID;

	ParseVertexFromVTX(vert, texcoords, mesh, pVerts, pTangs, pColors, pUVs, origId);

	const vvd::mstudiovertex_t& oldVert = pVerts[origId];

	assertm(nullptr != weights, "weight pointer should be valid");
	vert->weightIndex = weightIdx;
	vert->weightCount = oldVert.m_BoneWeights.numbones;

	for (int i = 0; i < oldVert.m_BoneWeights.numbones; i++)
	{
		weights[i].weight = oldVert.m_BoneWeights.weight[pVertex->boneWeightIndex[i]];

		// static props can be hardware skinned, but have no bonestates (one bone). this make it skip this, however it's a non issue as the following statement will work fine for this (there is only one bone at idx 0)
		if (isHwSkinned && pBoneStates)
		{
			weights[i].bone = static_cast<int16_t>(pBoneStates[pVertex->boneID[i]].newBoneID);
			continue;
		}

		weights[i].bone = pVertex->boneID[i];
	}

	weightIdx += oldVert.m_BoneWeights.numbones;

	mesh->weightsPerVert = static_cast<uint16_t>(vert->weightCount) > mesh->weightsPerVert ? static_cast<uint16_t>(vert->weightCount) : mesh->weightsPerVert;
}

// Apex Legends
void Vertex_t::ParseVertexFromVTX(Vertex_t* const vert, VertexWeight_t* const weights, Vector2D* const texcoords, ModelMeshData_t* mesh, const OptimizedModel::Vertex_t* const pVertex, const vvd::mstudiovertex_t* const pVerts, const Vector4D* const pTangs, const Color32* const pColors, const Vector2D* const pUVs,
	const vvw::vertexBoneWeightsExtraFileHeader_t* const pVVW, int& weightIdx)
{
	const int origId = pVertex->origMeshVertID;

	ParseVertexFromVTX(vert, texcoords, mesh, pVerts, pTangs, pColors, pUVs, origId);

	const vvd::mstudiovertex_t& oldVert = pVerts[origId];

	assertm(nullptr != weights, "weight pointer should be valid");
	vert->weightIndex = weightIdx;
	vert->weightCount = oldVert.m_BoneWeights.numbones;

	if (nullptr != pVVW)
	{
		const vvw::mstudioboneweightextra_t* const pExtraWeights = pVVW->GetWeightData(oldVert.m_BoneWeights.weightextra.extraweightindex);

		for (int i = 0; i < oldVert.m_BoneWeights.numbones; i++)
		{
			if (i >= 3)
			{
				weights[i].bone = pExtraWeights[i - 3].bone;
				weights[i].weight = pExtraWeights[i - 3].Weight();

				continue;
			}

			weights[i].bone = oldVert.m_BoneWeights.bone[i];
			weights[i].weight = oldVert.m_BoneWeights.weightextra.Weight(i);
		}
	}
	else
	{
		for (int i = 0; i < oldVert.m_BoneWeights.numbones; i++)
		{
			weights[i].bone = oldVert.m_BoneWeights.bone[i];
			weights[i].weight = oldVert.m_BoneWeights.weight[i];
		}
	}

	weightIdx += oldVert.m_BoneWeights.numbones;

	mesh->weightsPerVert = static_cast<uint16_t>(vert->weightCount) > mesh->weightsPerVert ? static_cast<uint16_t>(vert->weightCount) : mesh->weightsPerVert;
}

void ModelMeshData_t::ParseTexcoords()
{
	if (rawVertexLayoutFlags & VERT_TEXCOORD0)
	{
		int texCoordIdx = 0;
		int texCoordShift = 24;

		uint64_t inputFlagsShifted = rawVertexLayoutFlags >> texCoordShift;
		do
		{
			inputFlagsShifted = rawVertexLayoutFlags >> texCoordShift;

			int8_t texCoordFormat = inputFlagsShifted & VERT_TEXCOORD_MASK;

			assertm(texCoordFormat == 2 || texCoordFormat == 0, "invalid texcoord format");

			if (texCoordFormat != 0)
			{
				texcoordCount++;
				texcoodIndices |= (1 << texCoordIdx);
			}

			texCoordShift += VERT_TEXCOORD_BITS;
			texCoordIdx++;
		} while (inputFlagsShifted >= (1 << VERT_TEXCOORD_BITS)); // while the flag value is large enough that there is more than just one 
	}
}

void ModelMeshData_t::ParseMaterial(ModelParsedData_t* const parsed, const int material)
{
	// [rika]: handling mesh's material here since we've already looped through everything
	assertm(material < parsed->materials.size() && material >= 0, "invalid mesh material index");
	materialId = material;
	materialAsset = parsed->materials.at(material).asset;
}

// bones
void ParseModelBoneData_v8(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	const r5::mstudiobone_v8_t* const bones = reinterpret_cast<const r5::mstudiobone_v8_t* const>(pStudioHdr->pBones());
	parsedData->bones.resize(pStudioHdr->boneCount);

	for (int i = 0; i < pStudioHdr->boneCount; i++)
	{
		parsedData->bones.at(i) = ModelBone_t(&bones[i]);
	}
}

void ParseModelBoneData_v12_1(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	const r5::mstudiobone_v12_1_t* const bones = reinterpret_cast<const r5::mstudiobone_v12_1_t* const>(pStudioHdr->pBones());
	parsedData->bones.resize(pStudioHdr->boneCount);

	for (int i = 0; i < pStudioHdr->boneCount; i++)
	{
		parsedData->bones.at(i) = ModelBone_t(&bones[i]);
	}
}

void ParseModelBoneData_v16(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	const r5::mstudiobonehdr_v16_t* const bonehdrs = reinterpret_cast<const r5::mstudiobonehdr_v16_t* const>(pStudioHdr->pBones());
	const r5::mstudiobonedata_v16_t* const bonedata = reinterpret_cast<const r5::mstudiobonedata_v16_t* const>(pStudioHdr->pBoneData());

	parsedData->bones.resize(pStudioHdr->boneCount);

	for (int i = 0; i < pStudioHdr->boneCount; i++)
		parsedData->bones.at(i) = ModelBone_t(&bonehdrs[i], &bonedata[i]);
}

void ParseModelBoneData_v19(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	const r5::mstudiobonehdr_v16_t* const bonehdrs = reinterpret_cast<const r5::mstudiobonehdr_v16_t* const>(pStudioHdr->pBones());
	const r5::mstudiobonedata_v19_t* const bonedata = reinterpret_cast<const r5::mstudiobonedata_v19_t* const>(pStudioHdr->pBoneData());
	const r5::mstudiolinearbone_v19_t* const linearbone = reinterpret_cast<const r5::mstudiolinearbone_v19_t* const>(pStudioHdr->pLinearBone());

	parsedData->bones.resize(pStudioHdr->boneCount);

	for (int i = 0; i < pStudioHdr->boneCount; i++)
		parsedData->bones.at(i) = ModelBone_t(&bonehdrs[i], &bonedata[i], linearbone, i);
}

void ParseModelAttachmentData_v8(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();
	const r5::mstudioattachment_v8_t* const pAttachments = reinterpret_cast<const r5::mstudioattachment_v8_t* const>(pStudioHdr->baseptr + pStudioHdr->localAttachmentOffset);

	parsedData->attachments.reserve(pStudioHdr->localAttachmentCount);

	for (int i = 0; i < pStudioHdr->localAttachmentCount; i++)
	{
		parsedData->attachments.emplace_back(pAttachments + i);
	}
}

void ParseModelAttachmentData_v16(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();
	const r5::mstudioattachment_v16_t* const pAttachments = reinterpret_cast<const r5::mstudioattachment_v16_t* const>(pStudioHdr->baseptr + pStudioHdr->localAttachmentOffset);

	parsedData->attachments.reserve(pStudioHdr->localAttachmentCount);

	for (int i = 0; i < pStudioHdr->localAttachmentCount; i++)
	{
		parsedData->attachments.emplace_back(pAttachments + i);
	}
}

void ParseModelHitboxData_v8(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();
	const mstudiohitboxset_t* const pHitboxSets = reinterpret_cast<const mstudiohitboxset_t* const>(pStudioHdr->baseptr + pStudioHdr->hitboxSetOffset);

	parsedData->hitboxsets.reserve(pStudioHdr->hitboxSetCount);

	for (int i = 0; i < pStudioHdr->hitboxSetCount; i++)
	{
		parsedData->hitboxsets.emplace_back(pHitboxSets + i, pHitboxSets->pHitbox<r5::mstudiobbox_v8_t>(0));
	}
}

void ParseModelHitboxData_v16(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();
	const r5::mstudiohitboxset_v16_t* const pHitboxSets = reinterpret_cast<const r5::mstudiohitboxset_v16_t* const>(pStudioHdr->baseptr + pStudioHdr->hitboxSetOffset);

	parsedData->hitboxsets.reserve(pStudioHdr->hitboxSetCount);

	for (int i = 0; i < pStudioHdr->hitboxSetCount; i++)
	{
		parsedData->hitboxsets.emplace_back(pHitboxSets + i);
	}
}

void ParseModelDrawData(ModelParsedData_t* const parsedData, CDXDrawData* const drawData, const uint64_t lod)
{
	// [rika]: eventually parse through models
	for (size_t i = 0; i < parsedData->lods.at(lod).meshes.size(); ++i)
	{
		const ModelMeshData_t& mesh = parsedData->lods.at(lod).meshes.at(i);
		DXMeshDrawData_t* const meshDrawData = &drawData->meshBuffers[i];

		meshDrawData->visible = true;
		meshDrawData->doFrustumCulling = false;

		if (mesh.materialAsset)
			meshDrawData->uberStaticBuf = mesh.GetMaterialAsset()->uberStaticBuffer;

		assertm(mesh.meshVertexDataIndex != invalidNoodleIdx, "mesh data hasn't been parsed ??");

		std::unique_ptr<char[]> parsedVertexDataBuf = parsedData->meshVertexData.getIdx(mesh.meshVertexDataIndex);
		const CMeshData* const parsedVertexData = reinterpret_cast<CMeshData*>(parsedVertexDataBuf.get());

		if (!meshDrawData->vertexBuffer)
		{
#if defined(ADVANCED_MODEL_PREVIEW)
			const UINT vertStride = mesh.vertCacheSize;
#else
			const UINT vertStride = sizeof(Vertex_t);
#endif

			D3D11_BUFFER_DESC desc = {};

			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.ByteWidth = vertStride * mesh.vertCount;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;

#if defined(ADVANCED_MODEL_PREVIEW)
			const void* vertexData = mesh.rawVertexData;
#else
			const void* vertexData = parsedVertexData->GetVertices();
#endif

			D3D11_SUBRESOURCE_DATA srd{ vertexData };

			if (FAILED(g_dxHandler->GetDevice()->CreateBuffer(&desc, &srd, &meshDrawData->vertexBuffer)))
				return;

			meshDrawData->vertexStride = vertStride;

#if defined(ADVANCED_MODEL_PREVIEW)
			delete[] mesh.rawVertexData;
#endif
		}

		if (!meshDrawData->indexBuffer)
		{
			D3D11_BUFFER_DESC desc = {};

			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.ByteWidth = static_cast<UINT>(mesh.indexCount * sizeof(uint16_t));
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;

			D3D11_SUBRESOURCE_DATA srd = { parsedVertexData->GetIndices() };
			if (FAILED(g_dxHandler->GetDevice()->CreateBuffer(&desc, &srd, &meshDrawData->indexBuffer)))
				return;

			meshDrawData->numIndices = mesh.indexCount;
		}
	}

	return;
}

// [rika]: this is for model internal sequence data (r5)
void ParseModelSequenceData_NoStall(ModelParsedData_t* const parsedData, char* const baseptr)
{
	assertm(parsedData->bones.size() > 0, "should have bones");

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->localSequenceCount == 0)
		return;

	parsedData->sequences = new seqdesc_t[pStudioHdr->localSequenceCount];

	for (int i = 0; i < pStudioHdr->localSequenceCount; i++)
	{
		parsedData->sequences[i] = seqdesc_t(reinterpret_cast<r5::mstudioseqdesc_v8_t* const>(baseptr + pStudioHdr->localSequenceOffset) + i);

		ParseSeqDesc_R5(&parsedData->sequences[i], &parsedData->bones, AnimdataFuncType_t::ANIM_FUNC_NOSTALL);
	}
}

void ParseModelSequenceData_Stall_V8(ModelParsedData_t* const parsedData, char* const baseptr)
{
	assertm(parsedData->bones.size() > 0, "should have bones");

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->localSequenceCount == 0)
		return;

	parsedData->sequences = new seqdesc_t[pStudioHdr->localSequenceCount];

	for (int i = 0; i < pStudioHdr->localSequenceCount; i++)
	{
		parsedData->sequences[i] = seqdesc_t(reinterpret_cast<r5::mstudioseqdesc_v8_t* const>(baseptr + pStudioHdr->localSequenceOffset) + i, nullptr);

		ParseSeqDesc_R5(&parsedData->sequences[i], &parsedData->bones, AnimdataFuncType_t::ANIM_FUNC_STALL_BASEPTR);
	}
}

void ParseModelSequenceData_Stall_V16(ModelParsedData_t* const parsedData, char* const baseptr)
{
	assertm(parsedData->bones.size() > 0, "should have bones");

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->localSequenceCount == 0)
		return;

	parsedData->sequences = new seqdesc_t[pStudioHdr->localSequenceCount];

	for (int i = 0; i < pStudioHdr->localSequenceCount; i++)
	{
		parsedData->sequences[i] = seqdesc_t(reinterpret_cast<r5::mstudioseqdesc_v16_t* const>(baseptr + pStudioHdr->localSequenceOffset) + i, nullptr);

		ParseSeqDesc_R5(&parsedData->sequences[i], &parsedData->bones, AnimdataFuncType_t::ANIM_FUNC_STALL_BASEPTR);
	}
}

void ParseModelSequenceData_Stall_V18(ModelParsedData_t* const parsedData, char* const baseptr)
{
	assertm(parsedData->bones.size() > 0, "should have bones");

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->localSequenceCount == 0)
		return;

	parsedData->sequences = new seqdesc_t[pStudioHdr->localSequenceCount];

	for (int i = 0; i < pStudioHdr->localSequenceCount; i++)
	{
		parsedData->sequences[i] = seqdesc_t(reinterpret_cast<r5::mstudioseqdesc_v16_t* const>(baseptr + pStudioHdr->localSequenceOffset) + i, nullptr);

		ParseSeqDesc_R5(&parsedData->sequences[i], &parsedData->bones, AnimdataFuncType_t::ANIM_FUNC_STALL_BASEPTR);
	}
}

extern void ParseAnimSeqDataForSeqdesc(seqdesc_t* const seqdesc, const size_t boneCount);
void ParseModelSequenceData_Stall_V19_1(ModelParsedData_t* const parsedData, char* const baseptr)
{
	assertm(parsedData->bones.size() > 0, "should have bones");

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->localSequenceCount == 0)
		return;

	parsedData->sequences = new seqdesc_t[pStudioHdr->localSequenceCount];

	for (int i = 0; i < pStudioHdr->localSequenceCount; i++)
	{
		parsedData->sequences[i] = seqdesc_t(reinterpret_cast<r5::mstudioseqdesc_v8_t* const>(baseptr + pStudioHdr->localSequenceOffset) + i, nullptr);

		ParseAnimSeqDataForSeqdesc(parsedData->sequences + i, parsedData->bones.size());

		ParseSeqDesc_R5(&parsedData->sequences[i], &parsedData->bones, AnimdataFuncType_t::ANIM_FUNC_STALL_ANIMDATA);
	}
}

void ParseModelAnimTypes_V8(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->ikChainCount > 0)
	{
		parsedData->ikchains = new ModelIKChain_t[pStudioHdr->ikChainCount];
		const r5::mstudioikchain_v8_t* const ikchains = reinterpret_cast<const r5::mstudioikchain_v8_t* const>(pStudioHdr->baseptr + pStudioHdr->ikChainOffset);

		for (int i = 0; i < pStudioHdr->ikChainCount; i++)
		{
			const ModelIKChain_t ikchain(ikchains + i);
			memcpy_s(parsedData->ikchains + i, sizeof(ModelIKChain_t), &ikchain, sizeof(ModelIKChain_t));
		}
	}

	if (pStudioHdr->localPoseParamCount > 0)
	{
		parsedData->poseparams = new ModelPoseParam_t[pStudioHdr->localPoseParamCount];
		const mstudioposeparamdesc_t* const poseparams = reinterpret_cast<const mstudioposeparamdesc_t* const>(pStudioHdr->baseptr + pStudioHdr->localPoseParamOffset);

		for (int i = 0; i < pStudioHdr->localPoseParamCount; i++)
		{
			const ModelPoseParam_t poseparam(poseparams + i);
			memcpy_s(parsedData->poseparams + i, sizeof(ModelPoseParam_t), &poseparam, sizeof(ModelPoseParam_t));
		}
	}

	// technically supported but never used
	/*if (pStudioHdr->localIkAutoPlayLockCount > 0)
	{
		printf("wooowowww~~!! iklocks in: %s\n", pStudioHdr->pszName());

		parsedData->iklocks = new ModelIKLock_t[pStudioHdr->localIkAutoPlayLockCount];
		const r5::mstudioiklock_v8_t* const iklocks = reinterpret_cast<const r5::mstudioiklock_v8_t* const>(pStudioHdr->baseptr + pStudioHdr->localIkAutoPlayLockOffset);

		for (int i = 0; i < pStudioHdr->localIkAutoPlayLockCount; i++)
		{
			const ModelIKLock_t iklock(iklocks + i);
			memcpy_s(parsedData->iklocks + i, sizeof(ModelIKLock_t), &iklock, sizeof(ModelIKLock_t));
		}
	}*/
}

void ParseModelAnimTypes_V16(ModelParsedData_t* const parsedData)
{
	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	if (pStudioHdr->ikChainCount > 0)
	{
		parsedData->ikchains = new ModelIKChain_t[pStudioHdr->ikChainCount];
		const r5::mstudioikchain_v16_t* const ikchains = reinterpret_cast<const r5::mstudioikchain_v16_t* const>(pStudioHdr->baseptr + pStudioHdr->ikChainOffset);

		for (int i = 0; i < pStudioHdr->ikChainCount; i++)
		{
			const ModelIKChain_t ikchain(ikchains + i);
			memcpy_s(parsedData->ikchains + i, sizeof(ModelIKChain_t), &ikchain, sizeof(ModelIKChain_t));
		}
	}

	if (pStudioHdr->localPoseParamCount > 0)
	{
		parsedData->poseparams = new ModelPoseParam_t[pStudioHdr->localPoseParamCount];
		const r5::mstudioposeparamdesc_v16_t* const poseparams = reinterpret_cast<const r5::mstudioposeparamdesc_v16_t* const>(pStudioHdr->baseptr + pStudioHdr->localPoseParamOffset);

		for (int i = 0; i < pStudioHdr->localPoseParamCount; i++)
		{
			const ModelPoseParam_t poseparam(poseparams + i);
			memcpy_s(parsedData->poseparams + i, sizeof(ModelPoseParam_t), &poseparam, sizeof(ModelPoseParam_t));
		}
	}

	// no global iklocks in v16 and later	
}

void ParseAnimDesc_Origin(animdesc_t* const animdesc, CAnimData& animData, bool(*Studio_AnimPosition)(const animdesc_t* const, float, Vector&, QAngle&))
{
	// [rika]: adjust the origin bone
	// [rika]: do after we get anim data, so our rotation does not get overwritten
	CAnimDataBone& originBone = animData.GetBone(0);
	originBone.SetFlags(CAnimDataBone::ANIMDATA_POS | CAnimDataBone::ANIMDATA_ROT); // make sure it has position and rotation

	for (int frame = 0; frame < animdesc->numframes; frame++)
	{
		Vector pos(originBone.GetPosPtr()[frame]);
		Quaternion q(originBone.GetRotPtr()[frame]);
		const Vector& scale = originBone.GetSclPtr()[frame];

		QAngle vecAngleBase(q);

		const float cycle = animdesc->GetCycle(frame);

		if ((nullptr != animdesc->movement && animdesc->flags & eStudioAnimFlags::ANIM_FRAMEMOVEMENT) || (animdesc->nummovements && animdesc->movementindex))
		{
			Vector vecPos;
			QAngle vecAngle;

			Studio_AnimPosition(animdesc, cycle, vecPos, vecAngle);

			pos += vecPos; // add our base movement position to our base position 
			vecAngleBase.y += (vecAngle.y);
		}

		vecAngleBase.y += -90; // rotate -90 degree on the yaw axis

		// adjust position as we are rotating on the Z axis
		const float x = pos.x;
		const float y = pos.y;

		pos.x = y;
		pos.y = -x;

		AngleQuaternion(vecAngleBase, q);

		originBone.SetFrame(frame, pos, q, scale);
	}
}

void ParseAnimDesc_R2(seqdesc_t* const seqdesc, animdesc_t* const animdesc, const std::vector<ModelBone_t>* const bones, const r2::studiohdr_t* const pStudioHdr)
{
	const int boneCount = static_cast<int>(bones->size());

	Vector positions[256]{};
	Quaternion quats[256]{};
	Vector scales[256]{};

	if (animdesc->flags & eStudioAnimFlags::ANIM_DELTA)
	{
		for (int i = 0; i < boneCount; i++)
		{
			positions[i].Init(0.0f, 0.0f, 0.0f);
			quats[i].Init(0.0f, 0.0f, 0.0f, 1.0f);
			scales[i].Init(1.0f, 1.0f, 1.0f);
		}
	}
	else
	{
		for (int i = 0; i < boneCount; i++)
		{
			const ModelBone_t* const bone = &bones->at(i);

			positions[i] = bone->pos;
			quats[i] = bone->quat;
			scales[i] = bone->scale;
		}
	}

	// no point to allocate memory on empty animations!
	CAnimData animData(boneCount, animdesc->numframes);
	animData.ReserveVector();

	// [rika]: parse through the bone tracks here
	for (int frame = 0; frame < animdesc->numframes; frame++)
	{
		const float cycle = animdesc->GetCycle(frame);

		float fFrame = cycle * static_cast<float>(animdesc->numframes - 1);

		const int iFrame = static_cast<int>(fFrame);
		const float s = (fFrame - static_cast<float>(iFrame));

		int iLocalFrame = iFrame;
		const r2::mstudio_rle_anim_t* const panimstart = reinterpret_cast<const r2::mstudio_rle_anim_t*>(animdesc->pAnimdataNoStall(&iLocalFrame, nullptr));

		for (int bone = 0; bone < boneCount; bone++)
		{
			Vector pos;
			Quaternion q;
			Vector scale;

			const r2::mstudio_rle_anim_t* panim = panimstart;
			
			// [rika]: titanfall 2 cycles through all the bone headers to get right one for a given bone
			// [rika]: handle like the game since sometimes there can be blocks of bone headers with '0xff' bone id (if my memory serves me)
			// [rika]: slightly slower this way which is likely why it's changed in apex
			while (panim->bone < bone)
			{
				panim = panim->pNext();

				if (!panim)
					break;
			}

			uint8_t boneFlags = 0u;

			if (panim && panim->bone == bone)
			{
				// r2 animations will always have position
				boneFlags |= CAnimDataBone::ANIMDATA_POS | (panim->flags & r2::RleFlags_t::STUDIO_ANIM_NOROT ? 0 : CAnimDataBone::ANIMDATA_ROT) | (seqdesc->flags & eStudioAnimFlags::ANIM_SCALE ? CAnimDataBone::ANIMDATA_SCL : 0);

				// need to add a bool here, we do not want the interpolated values (inbetween frames)
				r2::CalcBonePosition(iLocalFrame, s, pStudioHdr->pBone(panim->bone), pStudioHdr->pLinearBones(), panim, pos);
				r2::CalcBoneQuaternion(iLocalFrame, s, pStudioHdr->pBone(panim->bone), pStudioHdr->pLinearBones(), panim, q);
				r2::CalcBoneScale(iLocalFrame, s, pStudioHdr->pBone(panim->bone)->scale, pStudioHdr->pBone(panim->bone)->scalescale, panim, scale);
			}
			else
			{
				pos = positions[bone];
				q = quats[bone];
				scale = scales[bone];
			}

			CAnimDataBone& animDataBone = animData.GetBone(bone);
			animDataBone.SetFlags(boneFlags);
			animDataBone.SetFrame(frame, pos, q, scale);
		}
	}

	ParseAnimDesc_Origin(animdesc, animData, &r1::Studio_AnimPosition);

	// parse into memory and compress
	CManagedBuffer* buffer = g_BufferManager.ClaimBuffer();

	const size_t sizeInMem = animData.ToMemory(buffer->Buffer());
	animdesc->parsedBufferIndex = seqdesc->parsedData.addBack(buffer->Buffer(), sizeInMem);

	g_BufferManager.RelieveBuffer(buffer);
}

void ParseSeqDesc_R2(seqdesc_t* const seqdesc, const std::vector<ModelBone_t>* const bones, const r2::studiohdr_t* const pStudioHdr)
{
	for (int i = 0; i < seqdesc->AnimCount(); i++)
	{
		animdesc_t* const animdesc = &seqdesc->anims.at(i);

		ParseAnimDesc_R2(seqdesc, animdesc, bones, pStudioHdr);
	}
}

void ParseAnimDesc_R5(seqdesc_t* const seqdesc, animdesc_t* const animdesc, const std::vector<ModelBone_t>* const bones, const AnimdataFuncType_t funcType)
{
	const int boneCount = static_cast<int>(bones->size());

	Vector positions[256]{};
	Quaternion quats[256]{};
	Vector scales[256]{};
	RadianEuler rotations[256]{};

	if (animdesc->flags & eStudioAnimFlags::ANIM_DELTA)
	{
		for (int i = 0; i < boneCount; i++)
		{
			positions[i].Init(0.0f, 0.0f, 0.0f);
			quats[i].Init(0.0f, 0.0f, 0.0f, 1.0f);
			scales[i].Init(1.0f, 1.0f, 1.0f);
			rotations[i].Init(0.0f, 0.0f, 0.0f);
		}
	}
	else
	{
		if (animdesc->flags & eStudioAnimFlags::ANIM_DATAPOINT)
		{
			for (int i = 0; i < boneCount; i++)
			{
				const ModelBone_t* const bone = &bones->at(i);

				positions[i].Init(0.0f, 0.0f, 0.0f);
				quats[i].Init(0.0f, 0.0f, 0.0f, 1.0f);
				scales[i].Init(1.0f, 1.0f, 1.0f);
				rotations[i] = bone->rot;
			}
		}
		else
		{
			for (int i = 0; i < boneCount; i++)
			{
				const ModelBone_t* const bone = &bones->at(i);

				positions[i] = bone->pos;
				quats[i] = bone->quat;
				scales[i] = bone->scale;
				rotations[i] = bone->rot;
			}
		}
	}

	CAnimData animData(boneCount, animdesc->numframes);
	animData.ReserveVector();

	// [rika]: parse through the bone tracks here
	if (animdesc->flags & eStudioAnimFlags::ANIM_VALID && animdesc->flags & eStudioAnimFlags::ANIM_DATAPOINT)
	{
		for (int frame = 0; frame < animdesc->numframes; frame++)
		{
			const float cycle = animdesc->GetCycle(frame);

			float fFrame = cycle * static_cast<float>(animdesc->numframes - 1);

			const int iFrame = static_cast<int>(fFrame);
			const float s = (fFrame - static_cast<float>(iFrame));

			int iLocalFrame = iFrame;

			int sectionlength = 0;
			const uint8_t* const boneFlagArray = reinterpret_cast<const uint8_t* const>((animdesc->*s_AnimdataFuncs_DP[funcType])(&iLocalFrame, &sectionlength));
			const r5::mstudio_rle_anim_t* panim = reinterpret_cast<const r5::mstudio_rle_anim_t*>(&boneFlagArray[ANIM_BONEFLAG_SIZE(boneCount)]);

			for (int bone = 0; bone < boneCount; bone++)
			{
				Vector pos(positions[bone]);
				Quaternion q(quats[bone]);
				Vector scale(scales[bone]);
				RadianEuler baseRot(rotations[bone]);


				uint8_t boneFlags = ANIM_BONEFLAGS_FLAG(boneFlagArray, bone); // truncate byte offset then shift if needed
				const uint8_t* panimtrack = reinterpret_cast<const uint8_t*>(panim + 1);
				const float fLocalFrame = static_cast<float>(iLocalFrame) + s;

				if (boneFlags & (r5::RleBoneFlags_t::STUDIO_ANIM_DATA)) // check if this bone has data
				{
					if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_ROT)
						r5::CalcBoneQuaternion_DP(sectionlength, &panimtrack, fLocalFrame, q);

					if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_POS)
					{
						if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_UNK8)
							r5::CalcBonePositionVirtual_DP(sectionlength, &panimtrack, fLocalFrame, pos);
						else
							r5::CalcBonePosition_DP(sectionlength, &panimtrack, fLocalFrame, pos);
					}

					if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_SCALE)
						r5::CalcBoneScale_DP(sectionlength, &panimtrack, fLocalFrame, pos);

					panim = panim->pNext();
				}

				if (!(animdesc->flags & eStudioAnimFlags::ANIM_DELTA))
				{

				}

				assertm(pos.IsValid(), "invalid position");
				assertm(q.IsValid(), "invalid quaternion");

				CAnimDataBone& animDataBone = animData.GetBone(bone);
				animDataBone.SetFlags(boneFlags);
				animDataBone.SetFrame(frame, pos, q, scale);
			}
		}
	}
	else if (animdesc->flags & eStudioAnimFlags::ANIM_VALID)
	{
		for (int frame = 0; frame < animdesc->numframes; frame++)
		{
			const float cycle = animdesc->GetCycle(frame);

			float fFrame = cycle * static_cast<float>(animdesc->numframes - 1);

			const int iFrame = static_cast<int>(fFrame);
			const float s = (fFrame - static_cast<float>(iFrame));

			int iLocalFrame = iFrame;

			int sectionlength = 0;
			const uint8_t* const boneFlagArray = reinterpret_cast<const uint8_t* const>((animdesc->*s_AnimdataFuncs_RLE[funcType])(&iLocalFrame, &sectionlength));
			const r5::mstudio_rle_anim_t* panim = reinterpret_cast<const r5::mstudio_rle_anim_t*>(&boneFlagArray[ANIM_BONEFLAG_SIZE(boneCount)]);
			UNUSED(sectionlength);

			for (int bone = 0; bone < boneCount; bone++)
			{
				Vector pos(positions[bone]);
				Quaternion q(quats[bone]);
				Vector scale(scales[bone]);
				RadianEuler baseRot(rotations[bone]);

				uint8_t boneFlags = ANIM_BONEFLAGS_FLAG(boneFlagArray, bone); // truncate byte offset then shift if needed

				if (boneFlags & (r5::RleBoneFlags_t::STUDIO_ANIM_DATA)) // check if this bone has data
				{
					if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_POS)
						CalcBonePosition(iLocalFrame, s, panim, pos);
					if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_ROT)
						CalcBoneQuaternion(iLocalFrame, s, panim, baseRot, q, boneFlags);
					if (boneFlags & r5::RleBoneFlags_t::STUDIO_ANIM_SCALE)
						CalcBoneScale(iLocalFrame, s, panim, scale, boneFlags);

					panim = panim->pNext();
				}

				CAnimDataBone& animDataBone = animData.GetBone(bone);
				animDataBone.SetFlags(boneFlags);
				animDataBone.SetFrame(frame, pos, q, scale);
			}
		}
	}
	// [rika]: or don't, if we don't have any
	else
	{
		// [rika]: does this sequence use scale?
		const uint8_t boneFlags = 0x0/*(seqdesc->flags & eStudioAnimFlags::ANIM_SCALE) ? CAnimDataBone::ANIMDATA_DATA : (CAnimDataBone::ANIMDATA_POS | CAnimDataBone::ANIMDATA_ROT)*/;

		for (int bone = 0; bone < boneCount; bone++)
		{
			CAnimDataBone& animDataBone = animData.GetBone(bone);
			
			for (int frame = 0; frame < animdesc->numframes; frame++)
			{
				animDataBone.SetFlags(boneFlags);
				animDataBone.SetFrame(frame, positions[bone], quats[bone], scales[bone]);
			}
		}
	}

	ParseAnimDesc_Origin(animdesc, animData, &r5::Studio_AnimPosition);

	// parse into memory and compress
	CManagedBuffer* buffer = g_BufferManager.ClaimBuffer();

	const size_t sizeInMem = animData.ToMemory(buffer->Buffer());
	animdesc->parsedBufferIndex = seqdesc->parsedData.addBack(buffer->Buffer(), sizeInMem);

	g_BufferManager.RelieveBuffer(buffer);		
}

void ParseSeqDesc_R5(seqdesc_t* const seqdesc, const std::vector<ModelBone_t>* const bones, const AnimdataFuncType_t funcType)
{
	// check flags
	assertm(static_cast<uint8_t>(CAnimDataBone::ANIMDATA_POS) == static_cast<uint8_t>(r5::RleBoneFlags_t::STUDIO_ANIM_POS), "flag mismatch");
	assertm(static_cast<uint8_t>(CAnimDataBone::ANIMDATA_ROT) == static_cast<uint8_t>(r5::RleBoneFlags_t::STUDIO_ANIM_ROT), "flag mismatch");
	assertm(static_cast<uint8_t>(CAnimDataBone::ANIMDATA_SCL) == static_cast<uint8_t>(r5::RleBoneFlags_t::STUDIO_ANIM_SCALE), "flag mismatch");

	for (int i = 0; i < seqdesc->AnimCount(); i++)
	{
		animdesc_t* const animdesc = &seqdesc->anims.at(i);

		// [rika]: data for this anim is not loaded, skip it
		if (animdesc->animDataAsset && !animdesc->animData)
		{
			Log("sequence %s required asset %llx but it was not loaded, skipping...\n", seqdesc->szlabel, animdesc->animDataAsset);
			continue;
		}

		ParseAnimDesc_R5(seqdesc, animdesc, bones, funcType);
	}
}

//
// COMPDATA
//

// CMeshData
void CMeshData::AddIndices(const uint16_t* const indices, const size_t indiceCount)
{
	assertm(writer, "attempting to write data, but writer is not initialized.");

	indiceOffset = writer - reinterpret_cast<char*>(this);

	const size_t bufferSize = indiceCount * sizeof(uint16_t);
	assertm((writer + bufferSize) - reinterpret_cast<char*>(this) < managedBufferSize, "data exceeded buffer size!!!");
	if (indices)
		memcpy(writer, indices, bufferSize);

	writer += IALIGN16(bufferSize);
}

void CMeshData::AddVertices(const Vertex_t* const vertices, const size_t vertexCount)
{
	assertm(writer, "attempting to write data, but writer is not initialized.");

	vertexOffset = writer - reinterpret_cast<char*>(this);

	const size_t bufferSize = vertexCount * sizeof(Vertex_t);
	assertm((writer + bufferSize) - reinterpret_cast<char*>(this) < managedBufferSize, "data exceeded buffer size!!!");
	if (vertices)
		memcpy(writer, vertices, bufferSize);

	writer += IALIGN16(bufferSize);
}

void CMeshData::AddWeights(const VertexWeight_t* const weights, const size_t weightCount)
{
	assertm(writer, "attempting to write data, but writer is not initialized.");

	weightOffset = writer - reinterpret_cast<char*>(this);

	const size_t bufferSize = weightCount * sizeof(VertexWeight_t);
	assertm((writer + bufferSize) - reinterpret_cast<char*>(this) < managedBufferSize, "data exceeded buffer size!!!");
	if (weights)
		memcpy(writer, weights, bufferSize);

	writer += IALIGN16(bufferSize);
}

void CMeshData::AddTexcoords(const Vector2D* const texcoords, const size_t texcoordCount)
{
	assertm(writer, "attempting to write data, but writer is not initialized.");

	texcoordOffset = writer - reinterpret_cast<char*>(this);

	const size_t bufferSize = texcoordCount * sizeof(Vector2D);
	assertm((writer + bufferSize) - reinterpret_cast<char*>(this) < managedBufferSize, "data exceeded buffer size!!!");
	if (texcoords)
		memcpy(writer, texcoords, bufferSize);

	writer += IALIGN16(bufferSize);
}

// CAnimData
CAnimData::CAnimData(char* const buf) : pBuffer(buf), memory(true)
{
	assertm(nullptr != pBuffer, "invalid pointer provided");

	const char* curpos = pBuffer;

	memcpy(&numBones, curpos, sizeof(int) * 2);
	curpos += sizeof(int) * 2;

	pOffsets = reinterpret_cast<const size_t* const>(curpos);
	curpos += IALIGN16(sizeof(size_t) * numBones);

	pFlags = reinterpret_cast<const uint8_t*>(curpos);
	curpos += IALIGN16(sizeof(uint8_t) * numBones);
};

// access memory data
const Vector* const CAnimData::GetBonePosForFrame(const int bone, const int frame) const
{
	const uint8_t boneFlags = GetFlag(bone);
	assertm(boneFlags & CAnimDataBone::ANIMDATA_POS, "bone did not have position");

	const Vector* const tmp = reinterpret_cast<const Vector* const>(pBuffer + pOffsets[bone]);

	return tmp + frame;
}

const Quaternion* const CAnimData::GetBoneQuatForFrame(const int bone, const int frame) const
{
	const uint8_t boneFlags = GetFlag(bone);
	assertm(boneFlags & CAnimDataBone::ANIMDATA_ROT, "bone did not have rotation");

	const Quaternion* const tmp = reinterpret_cast<const Quaternion* const>(pBuffer + pOffsets[bone] + (s_AnimDataBoneSizeLUT[boneFlags & CAnimDataBone::ANIMDATA_POS] * numFrames));

	return tmp + frame;
}

const Vector* const CAnimData::GetBoneScaleForFrame(const int bone, const int frame) const
{
	const uint8_t boneFlags = GetFlag(bone);
	assertm(boneFlags & CAnimDataBone::ANIMDATA_SCL, "bone did not have scale");

	const Vector* const tmp = reinterpret_cast<const Vector* const>(pBuffer + pOffsets[bone] + (s_AnimDataBoneSizeLUT[boneFlags & (CAnimDataBone::ANIMDATA_POS | CAnimDataBone::ANIMDATA_ROT)] * numFrames));

	return tmp + frame;
}

const size_t CAnimData::ToMemory(char* const buf)
{
	char* curpos = buf;

	// dumb
	memcpy(curpos, &numBones, sizeof(int) * 2);
	curpos += sizeof(int) * 2;

	size_t* offsets = reinterpret_cast<size_t*>(curpos);
	curpos += IALIGN16(sizeof(size_t) * numBones);

	uint8_t* flags = reinterpret_cast<uint8_t*>(curpos);
	curpos += IALIGN16(sizeof(uint8_t) * numBones);

	for (size_t i = 0; i < numBones; i++)
	{
		const CAnimDataBone& bone = bones.at(i);

		offsets[i] = static_cast<size_t>(curpos - buf);

		flags[i] = bone.GetFlags();

		if (flags[i] & CAnimDataBone::ANIMDATA_POS)
		{
			memcpy(curpos, bone.GetPosPtr(), sizeof(Vector) * numFrames);
			curpos += sizeof(Vector) * numFrames;
		}

		if (flags[i] & CAnimDataBone::ANIMDATA_ROT)
		{
			memcpy(curpos, bone.GetRotPtr(), sizeof(Quaternion) * numFrames);
			curpos += sizeof(Quaternion) * numFrames;
		}

		if (flags[i] & CAnimDataBone::ANIMDATA_SCL)
		{
			memcpy(curpos, bone.GetSclPtr(), sizeof(Vector) * numFrames);
			curpos += sizeof(Vector) * numFrames;
		}
	}

	const size_t size = static_cast<size_t>(curpos - buf);
	assertm(size < CBufferManager::MaxBufferSize(), "animation data too large");

	return size;
};


//
// EXPORT SETTINGS
//

struct ModelMaterialExport_t
{
	ModelMaterialExport_t(MaterialAsset* const material, const int materialId) : asset(material), id(materialId) {}

	std::unordered_map<uint32_t, MaterialTextureExportInfo_s> textures;
	MaterialAsset* asset;
	int id;
};

// export materials from parsed data
void HandleModelMaterials(const ModelParsedData_t* const parsedData, std::unordered_map<int, ModelMaterialExport_t>& materials, const std::filesystem::path& exportPath)
{
	// [rika]: this will decide if we want textures/materials local to the model, or to use full paths in the future.
	const bool useFullPaths = false; // temp

	// [rika]: make sure materials are empty
	materials.clear();
	materials.reserve(parsedData->materials.size());

	// [rika]: pick a skin !
	if (g_ExportSettings.exportModelSkin && g_ExportSettings.previewedSkinIndex >= static_cast<int>(parsedData->skins.size()))
	{
		assertm(false, "skin index out of range");
		g_ExportSettings.previewedSkinIndex = 0;
	}

	const int skin = g_ExportSettings.exportModelSkin ? g_ExportSettings.previewedSkinIndex : 0;

	const ModelSkinData_t* const skinData = &parsedData->skins.at(skin);

	// [rika]: we don't need to cycle through all the LODs here, any used mesh should be in the top LOD and cannot change per LOD
	// this gets the data we need to export materials, and set them properly in meshes later
	// keep track of the materials that are actually used by meshes, so we don't export unneeded ones (speeds up export)
	for (const ModelMeshData_t& mesh : parsedData->lods.front().meshes)
	{
		// [rika]: no vertices, this mesh will not be exported.
		if (!mesh.vertCount)
			continue;

		const int baseId = mesh.materialId;
		const int skinId = static_cast<int>(skinData->indices[baseId]);

		const ModelMaterialData_t* const materialData = parsedData->pMaterial(skinId);
		MaterialAsset* asset = materialData->GetMaterialAsset();

		ModelMaterialExport_t material(asset, skinId);

		if (!asset)
		{
			materials.emplace(baseId, material);

			continue;
		}

		ParseMaterialTextureExportInfo(material.textures, material.asset, exportPath, eTextureExportName::TXTR_NAME_TEXT, useFullPaths);

		materials.emplace(baseId, material);
	}

	// [rika]: don't export material textures if it's not enabled
	if (!g_ExportSettings.exportMaterialTextures)
		return;

	// [rika]: export material textures
	std::atomic<uint32_t> remainingMaterials = 0; // we don't actually need thread safe here
	const ProgressBarEvent_t* const materialExportProgress = g_pImGuiHandler->AddProgressBarEvent("Exporting Materials..", static_cast<uint32_t>(materials.size()), &remainingMaterials, true);

	// [rika]: so we don't export textures per lod, we should exclude skins
	// todo: move this into the base function, don't export if raw
	for (const auto& it : materials)
	{
		++remainingMaterials;

		const ModelMaterialExport_t& material = it.second;

		if (!material.asset)
			continue;

		const MaterialAsset* const matlAsset = material.asset;

		// skip this material if it has no textures
		if (matlAsset->txtrAssets.size() == 0ull)
			continue;

		// enable exporting other image formats! (would if blender didn't smell)
		// [rika]: the extension also needs to be altered in model export formats if we do this!
		ExportMaterialTextures(eTextureExportSetting::PNG_HM, matlAsset, material.textures); // NOTE: LOOK INTO MAKING A FOLDER PER MATERIAL ?

	}
	g_pImGuiHandler->FinishProgressBarEvent(materialExportProgress);
}

// [rika]: todo also fix this up
// export parsed data to rmax
bool ExportModelRMAX(const ModelParsedData_t* const parsedData, std::filesystem::path& exportPath)
{
	std::string fileNameBase = exportPath.stem().string();
	const std::filesystem::path filePath(exportPath.parent_path());

	rmax::RMAXExporter rmaxFile(filePath, fileNameBase.c_str(), fileNameBase.c_str());

	// do bones
	rmaxFile.ReserveBones(parsedData->bones.size());
	for (auto& bone : parsedData->bones)
		rmaxFile.AddBone(bone.name, bone.parent, bone.pos, bone.quat, bone.scale);

	// [rika]: model is skin and bones, no meat
	if (parsedData->lods.size() == 0)
	{
		const std::string tmpName(std::format("{}.rmax", fileNameBase));
		rmaxFile.SetName(tmpName);

		rmaxFile.ToFile();

		return true;
	}

	const std::filesystem::path texturePath(std::format("{}/{}", filePath.string(), fileNameBase)); // todo, remove duplicate code
	std::unordered_map<int, ModelMaterialExport_t> materials;
	HandleModelMaterials(parsedData, materials, texturePath);

	// [rika]: now we parse lods
	for (size_t lodIdx = 0; lodIdx < parsedData->lods.size(); lodIdx++)
	{
		const ModelLODData_t& lodData = parsedData->lods.at(lodIdx);

		const std::string tmpName = std::format("{}_LOD{}.rmax", fileNameBase, lodIdx);
		rmaxFile.SetName(tmpName);		

		// do materials
		rmaxFile.ReserveMaterials(parsedData->materials.size());
		for (size_t matIdx = 0; matIdx < parsedData->materials.size(); matIdx++)
		{
			const ModelMaterialData_t& materialData = parsedData->materials.at(matIdx);
			const int materialId = static_cast<int>(matIdx);

			// [rika]: if it's unloaded, or unused we will just write a stub material
			if (!materialData.asset)
			{
				rmaxFile.AddMaterial(materialData.name);
				continue;
			}

			assert(materialData.GetMaterialAsset());
			const MaterialAsset* const matlAsset = materialData.GetMaterialAsset();
			rmaxFile.AddMaterial(matlAsset->name);

			// [rika]: write stub material (unused material)
			if (!materials.contains(materialId) || !matlAsset->resourceBindings.size())
				continue;

			const ModelMaterialExport_t& materialExport = materials.find(materialId)->second;

			rmax::RMAXMaterial* const matl = rmaxFile.GetMaterialLast();

			for (const TextureAssetEntry_t& entry : matlAsset->txtrAssets)
			{
				// [rika]: we don't have a resource binding or we don't have a name for the texture
				if (!matlAsset->resourceBindings.count(entry.index) || !materialExport.textures.contains(entry.index))
					continue;

				const std::string resource = matlAsset->resourceBindings.find(entry.index)->second.name;

				// [rika]: do we need this resource?
				if (!rmax::s_TextureTypeMap.count(resource))
					continue;

				const MaterialTextureExportInfo_s& info = materialExport.textures.find(entry.index)->second;
				const std::string path = std::format("{}/{}", info.exportPath.string(), info.exportName);

				matl->AddTexture(path.c_str(), rmax::s_TextureTypeMap.find(resource)->second);
			}
		}

		// do models
		rmaxFile.ReserveCollections(lodData.models.size());
		rmaxFile.ReserveMeshes(lodData.meshes.size());
		rmaxFile.ReserveVertices(lodData.vertexCount, lodData.texcoordsPerVert, lodData.weightsPerVert);
		rmaxFile.ReserveIndices(lodData.indexCount);
		for (auto& model : lodData.models)
		{
			if (!model.meshCount)
				continue;

			rmaxFile.AddCollection(model.name.c_str(), model.meshCount);

			for (uint32_t meshIdx = 0; meshIdx < model.meshCount; meshIdx++)
			{
				const ModelMeshData_t& meshData = model.meshes[meshIdx];

				assertm(materials.contains(meshData.materialId), "material should be parsed as it is used");
				const ModelMaterialExport_t& material = materials.find(meshData.materialId)->second;

				assertm(meshData.meshVertexDataIndex != invalidNoodleIdx, "mesh data hasn't been parsed ??");

				std::unique_ptr<char[]> parsedVertexDataBuf = parsedData->meshVertexData.getIdx(meshData.meshVertexDataIndex);
				const CMeshData* const parsedVertexData = reinterpret_cast<CMeshData*>(parsedVertexDataBuf.get());

				rmaxFile.AddMesh(static_cast<int16_t>(rmaxFile.CollectionCount() - 1), static_cast<int16_t>(material.id), meshData.texcoordCount, meshData.texcoodIndices, (meshData.rawVertexLayoutFlags & VERT_COLOR));

				rmax::RMAXMesh* const mesh = rmaxFile.GetMeshLast();

				// data parsing
				for (uint32_t i = 0; i < meshData.vertCount; i++)
				{
					const Vertex_t* const vertData = &parsedVertexData->GetVertices()[i];

					Vector normal;
					vertData->normalPacked.UnpackNormal(normal);

					mesh->AddVertex(vertData->position, normal);

					if (meshData.rawVertexLayoutFlags & VERT_COLOR)
						mesh->AddColor(vertData->color);

					for (uint16_t texcoordIdx = 0; texcoordIdx < meshData.texcoordCount; texcoordIdx++)
						mesh->AddTexcoord(*vertData->GetTexcoordForVertex(texcoordIdx, meshData.texcoordCount, parsedVertexData->GetTexcoords(), i));

					for (uint32_t weightIdx = 0; weightIdx < vertData->weightCount; weightIdx++)
					{
						const VertexWeight_t* const weight = &parsedVertexData->GetWeights()[vertData->weightIndex + weightIdx];
						mesh->AddWeight(i, weight->bone, weight->weight);
					}
				}

				for (uint32_t i = 0; i < meshData.indexCount; i += 3)
					mesh->AddIndice(parsedVertexData->GetIndices()[i], parsedVertexData->GetIndices()[i + 1], parsedVertexData->GetIndices()[i + 2]);
			}
		}

		rmaxFile.ToFile();
		rmaxFile.ResetMeshData();
	}

	return true;
}

// export parsed data to cast
// [rika]: todo rewrite this soon tm (it is so bad)
bool ExportModelCast(const ModelParsedData_t* const parsedData, std::filesystem::path& exportPath, const uint64_t guid)
{
	std::string fileNameBase = exportPath.stem().string();

	// [rika]: build the skeleton once, and reuse it
	cast::CastNode skelNode(cast::CastId::Skeleton, RTech::StringToGuid(fileNameBase.c_str()));
	{
		const size_t boneCount = parsedData->bones.size();
		skelNode.ReserveChildren(boneCount);

		// uses hashes for lookup, still gets bone parents by index :clown:
		for (size_t i = 0; i < boneCount; i++)
		{
			const ModelBone_t& boneData = parsedData->bones.at(i);

			cast::CastNodeBone boneNode(&skelNode);
			boneNode.MakeBone(boneData.name, boneData.parent, &boneData.pos, &boneData.quat, false);
		}
	}
	const cast::CastNode& skelNodeConst = skelNode;

	// [rika]: model is skin and bones, no meat
	if (parsedData->lods.size() == 0)
	{
		const std::string tmpName(std::format("{}.cast", fileNameBase));
		exportPath.replace_filename(tmpName);

		cast::CastExporter cast(exportPath.string());

		// cast
		cast::CastNode* const rootNode = cast.GetChild(0); // we only have one root node, no hash
		cast::CastNode* const modelNode = rootNode->AddChild(cast::CastId::Model, guid);

		// [rika]: we can predict how big this vector needs to be, however resizing it will make adding new members a pain.
		const size_t modelChildrenCount = 1; // skeleton (one)
		modelNode->ReserveChildren(modelChildrenCount);

		// do skeleton
		modelNode->AddChild(skelNode); // one time use

		cast.ToFile();

		return true;
	}

	const std::filesystem::path texturePath(std::format("{}/{}", exportPath.parent_path().string(), fileNameBase)); // todo, remove duplicate code
	std::unordered_map<int, ModelMaterialExport_t> materials;
	HandleModelMaterials(parsedData, materials, texturePath);

	for (size_t lodIdx = 0; lodIdx < parsedData->lods.size(); lodIdx++)
	{
		const ModelLODData_t& lodData = parsedData->lods.at(lodIdx);

		std::string tmpName(std::format("{}_LOD{}.cast", fileNameBase, std::to_string(lodIdx)));
		exportPath.replace_filename(tmpName);

		cast::CastExporter cast(exportPath.string());

		// cast
		cast::CastNode* rootNode = cast.GetChild(0); // we only have one root node, no hash
		cast::CastNode* modelNode = rootNode->AddChild(cast::CastId::Model, guid);

		// [rika]: we can predict how big this vector needs to be, however resizing it will make adding new members a pain.
		const size_t modelChildrenCount = 1 + parsedData->materials.size() + lodData.meshes.size(); // skeleton (one), materials (varies), meshes (varies)
		modelNode->ReserveChildren(modelChildrenCount);

		// do skeleton
		modelNode->AddChild(skelNodeConst);

		// do materials
		for (const auto& it : materials)
		{
			const ModelMaterialExport_t& material = it.second;
			const ModelMaterialData_t* const materialData = &parsedData->materials.at(static_cast<size_t>(material.id));

			// [rika]: a cast material has at least two properties, name and material type (pbr in our case)
			cast::CastNode matlNode(cast::CastId::Material, 2, materialData->guid);
			matlNode.SetProperty(1, cast::CastPropertyId::String, static_cast<int>(cast::CastPropsMaterial::Type), "pbr", 1u);

			if (!material.asset)
			{
				matlNode.SetProperty(0, cast::CastPropertyId::String, static_cast<int>(cast::CastPropsMaterial::Name), keepAfterLastSlashOrBackslash(materialData->name), 1u); // unsure why it does this but we're rolling with it!
				modelNode->AddChild(matlNode);
				continue;
			}

			const MaterialAsset* const materialAsset = material.asset;

			matlNode.SetProperty(0, cast::CastPropertyId::String, static_cast<int>(cast::CastPropsMaterial::Name), keepAfterLastSlashOrBackslash(materialAsset->name), 1u);

			// [rika]: parse out our textures if we have bindings for them, don't if not
			// [rika]: exit early if no textures
			if (materialAsset->resourceBindings.empty())
			{
				modelNode->AddChild(matlNode);
				continue;
			}

			for (const TextureAssetEntry_t& entry : materialAsset->txtrAssets)
			{
				// [rika]: texture cannot be accurately identified, skip it
				if (!materialAsset->resourceBindings.count(entry.index))
					continue;

				const std::string resource(materialAsset->resourceBindings.find(entry.index)->second.name);

				// [rika]: texture type isn't supported, skip it
				if (!cast::s_TextureTypeMap.count(resource))
					continue;

				// [rika]: if MaterialTextureExportInfo_s doesn't exist for this texture it's not loaded, and by extension is not exported
				if (!material.textures.contains(entry.index))
				{
					// todo: store a name in parsed data
					//Log("Material %s for model %s did not have a valid texture pointer for res idx %i\n", materialAsset->name, name, entry.index);

					continue;
				}

				const MaterialTextureExportInfo_s& info = material.textures.find(entry.index)->second;

				const uint64_t textureGuid = entry.asset->data()->guid; // texture guid

				const cast::CastPropsMaterial matlTxtrProp = cast::s_TextureTypeMap.find(resource)->second;

				matlNode.AddProperty(cast::CastPropertyId::Integer64, static_cast<int>(matlTxtrProp), textureGuid);

				cast::CastNode fileNode(cast::CastId::File, 1, textureGuid);

				// [rika]: need to figure out how this works more
				const std::string filePath(std::format("{}/{}.png", fileNameBase, info.exportName));
				fileNode.SetString(filePath); // materials exported from models always use png, as blender support for dds is bad, todo: make it so we can use ALL formats!
				fileNode.SetProperty(0, cast::CastPropertyId::String, static_cast<int>(cast::CastPropsFile::Path), fileNode.GetString(), 1u);

				matlNode.AddChild(fileNode);
			}

			modelNode->AddChild(matlNode);
		}

		// !!! TODO !!!
		// this needs to get cleaned up, but if I don't push I am gonna be stuck forever

		// [rika]: a system to have these allocated to each node would be cleaner, but more expensive.
		struct DataPtrs_t {
			Vector* positions;
			Vector* normals;
			Color32* colors;
			Vector2D* texcoords; // all uvs stored here
			uint8_t* blendIndices;
			float* blendWeights;

			uint16_t* indices;
		};

		DataPtrs_t vertexData{ nullptr };

		//                                    Postion & Normal       Color
		constexpr size_t castMinSizePerVert = (sizeof(Vector) * 2) + sizeof(Color32);

		char* vertDataBlockBuf = new char[(castMinSizePerVert + (sizeof(Vector2D) * lodData.texcoordsPerVert)) * lodData.vertexCount] {};

		vertexData.positions = reinterpret_cast<Vector*>(vertDataBlockBuf);
		vertexData.normals = &vertexData.positions[lodData.vertexCount];
		vertexData.colors = reinterpret_cast<Color32*>(&vertexData.normals[lodData.vertexCount]); // discarded if unneeded
		vertexData.texcoords = reinterpret_cast<Vector2D*>(&vertexData.colors[lodData.vertexCount]);
		vertexData.blendIndices = new uint8_t[lodData.vertexCount * lodData.weightsPerVert]{};
		vertexData.blendWeights = new float[lodData.vertexCount * lodData.weightsPerVert] {};

		vertexData.indices = new uint16_t[lodData.indexCount];

		size_t curIndex = 0; // current index into vertex data
		size_t idxIndex = 0; // shit format

		// do meshes
		for (auto& modelData : lodData.models)
		{
			for (size_t i = 0; i < modelData.meshCount; i++)
			{
				const ModelMeshData_t& meshData = modelData.meshes[i];

				assertm(materials.contains(meshData.materialId), "material should be parsed as it is used");
				const uint64_t materialGuid = parsedData->materials.at(materials.find(meshData.materialId)->second.id).guid;

				assertm(meshData.meshVertexDataIndex != invalidNoodleIdx, "mesh data hasn't been parsed ??");

				std::unique_ptr<char[]> parsedVertexDataBuf = parsedData->meshVertexData.getIdx(meshData.meshVertexDataIndex);
				const CMeshData* const parsedVertexData = reinterpret_cast<CMeshData*>(parsedVertexDataBuf.get());

				std::string matl = nullptr != meshData.materialAsset ? keepAfterLastSlashOrBackslash(meshData.GetMaterialAsset()->name) : std::to_string(materialGuid);
				std::string meshName = std::format("{}_{}", modelData.name, matl);
				cast::CastNode meshNode(cast::CastId::Mesh, 1, RTech::StringToGuid(meshName.c_str())); // name

				// name, pos, normal, blendweight, blendindices, indices, uv count, max blends, material, texcoords, color
				const size_t meshPropertiesCount = 9 + meshData.texcoordCount + (meshData.rawVertexLayoutFlags & VERT_COLOR ? 1 : 0);
				meshNode.ReserveProperties(meshPropertiesCount);

				// works on files but not here, why?
				// update: now it works after changing how the string is formed, lovely.
				meshNode.SetString(meshName);
				meshNode.SetProperty(0, cast::CastPropertyId::String, static_cast<int>(cast::CastPropsMesh::Name), meshNode.GetString(), 1u);

				meshNode.AddProperty(cast::CastPropertyId::Vector3, static_cast<int>(cast::CastPropsMesh::Vertex_Postion_Buffer), &vertexData.positions[curIndex], meshData.vertCount);
				meshNode.AddProperty(cast::CastPropertyId::Vector3, static_cast<int>(cast::CastPropsMesh::Vertex_Normal_Buffer), &vertexData.normals[curIndex], meshData.vertCount);

				if (meshData.rawVertexLayoutFlags & VERT_COLOR)
					meshNode.AddProperty(cast::CastPropertyId::Integer32, static_cast<int>(cast::CastPropsMesh::Vertex_Color_Buffer), &vertexData.colors[curIndex], meshData.vertCount);

				// cast cries if we use the proper index
				for (int16_t texcoordIdx = 0; texcoordIdx < meshData.texcoordCount; texcoordIdx++)
					meshNode.AddProperty(cast::CastPropertyId::Vector2, static_cast<int>(cast::CastPropsMesh::Vertex_UV_Buffer), &vertexData.texcoords[(lodData.vertexCount * texcoordIdx) + curIndex], meshData.vertCount, true, texcoordIdx);

				meshNode.AddProperty(cast::CastPropertyId::Byte, static_cast<int>(cast::CastPropsMesh::Vertex_Weight_Bone_Buffer), &vertexData.blendIndices[lodData.weightsPerVert * curIndex], (meshData.vertCount * meshData.weightsPerVert));
				meshNode.AddProperty(cast::CastPropertyId::Float, static_cast<int>(cast::CastPropsMesh::Vertex_Weight_Value_Buffer), &vertexData.blendWeights[lodData.weightsPerVert * curIndex], (meshData.vertCount * meshData.weightsPerVert));

				// parse our vertices into the buffer, so cringe!
				for (uint32_t vertIdx = 0; vertIdx < meshData.vertCount; vertIdx++)
				{
					const Vertex_t& vert = parsedVertexData->GetVertices()[vertIdx];

					vertexData.positions[curIndex + vertIdx] = vert.position;
					vert.normalPacked.UnpackNormal(vertexData.normals[curIndex + vertIdx]);
					vertexData.colors[curIndex + vertIdx] = vert.color;

					for (uint16_t texcoordIdx = 0; texcoordIdx < meshData.texcoordCount; texcoordIdx++)
					{
						const Vector2D* const texcoord = vert.GetTexcoordForVertex(texcoordIdx, meshData.texcoordCount, parsedVertexData->GetTexcoords(), vertIdx);
						vertexData.texcoords[(lodData.vertexCount * texcoordIdx) + curIndex + vertIdx] = *texcoord;
					}

					for (uint32_t weightIdx = 0; weightIdx < vert.weightCount; weightIdx++)
					{
						vertexData.blendIndices[(lodData.weightsPerVert * curIndex) + (meshData.weightsPerVert * vertIdx) + weightIdx] = static_cast<uint8_t>(parsedVertexData->GetWeights()[vert.weightIndex + weightIdx].bone);
						vertexData.blendWeights[(lodData.weightsPerVert * curIndex) + (meshData.weightsPerVert * vertIdx) + weightIdx] = parsedVertexData->GetWeights()[vert.weightIndex + weightIdx].weight;
					}
				}

				// parse our indices into the buffer, and shuffle them! extra cringe!
				const uint32_t indexCount = meshData.indexCount;
				meshNode.AddProperty(cast::CastPropertyId::Short, static_cast<int>(cast::CastPropsMesh::Face_Buffer), &vertexData.indices[idxIndex], indexCount);

				for (uint32_t idxIdx = 0; idxIdx < indexCount; idxIdx += 3)
				{
					vertexData.indices[idxIndex + idxIdx] = parsedVertexData->GetIndices()[idxIdx + 2];
					vertexData.indices[idxIndex + idxIdx + 1] = parsedVertexData->GetIndices()[idxIdx + 1];
					vertexData.indices[idxIndex + idxIdx + 2] = parsedVertexData->GetIndices()[idxIdx];
				}

				meshNode.AddProperty(cast::CastPropertyId::Short, static_cast<int>(cast::CastPropsMesh::UV_Layer_Count), meshData.texcoordCount);
				meshNode.AddProperty(cast::CastPropertyId::Short, static_cast<int>(cast::CastPropsMesh::Max_Weight_Influence), meshData.weightsPerVert);

				meshNode.AddProperty(cast::CastPropertyId::Integer64, static_cast<int>(cast::CastPropsMesh::Material), materialGuid);

				modelNode->AddChild(meshNode);

				curIndex += meshData.vertCount;
				idxIndex += indexCount;
			}
		}

		cast.ToFile();

		// cleanup our allocated buffers
		delete[] vertDataBlockBuf;
		delete[] vertexData.blendIndices;
		delete[] vertexData.blendWeights;

		delete[] vertexData.indices;
	}

	return true;
}

// parse a Vertex_t into a smd vertex
inline void ParseVertexIntoSMD(const Vertex_t* const srcVert, const VertexWeight_t* const srcWeights, smd::Vertex* const vert, const bool isStaticProp, const uint32_t texcoordWidth = 1u, const Vector2D* const extraTexcoords = nullptr, const uint32_t vertexIndex = 0u)
{
	vert->position = srcVert->position;
	srcVert->normalPacked.UnpackNormal(vert->normal);

	if (isStaticProp)
	{
		StaticPropFlipFlop(vert->position);
		StaticPropFlipFlop(vert->normal);
	}

	vert->numTexcoords = texcoordWidth > smd::maxTexcoords ? smd::maxTexcoords : texcoordWidth;
	for (uint32_t texcoordIdx = 0u; texcoordIdx < vert->numTexcoords; texcoordIdx++)
	{
		vert->texcoords[texcoordIdx] = *srcVert->GetTexcoordForVertex(texcoordIdx, texcoordWidth, extraTexcoords, vertexIndex);
		Vertex_t::InvertTexcoord(vert->texcoords[texcoordIdx]); // [rika]: the texcoord has to be inverted for proper recompile
	}	

	vert->numBones = srcVert->weightCount > smd::maxBoneWeights ? smd::maxBoneWeights : srcVert->weightCount;
	for (uint32_t weightIdx = 0u; weightIdx < vert->numBones; weightIdx++)
	{
		const VertexWeight_t* const weight = &srcWeights[srcVert->weightIndex + weightIdx];

		vert->bone[weightIdx] = weight->bone;
		vert->weight[weightIdx] = weight->weight;
	}
}

// export parsed data into smd files
bool ExportModelSMD(const ModelParsedData_t* const parsedData, std::filesystem::path& exportPath)
{
	std::string fileNameBase = exportPath.stem().string();
	const std::filesystem::path filePath(exportPath.parent_path());

	smd::CStudioModelData* const smd = new smd::CStudioModelData(filePath, parsedData->bones.size(), 1ull);

	// [rika]: initialize the nodes, and in this case the frames since we should only have one
	for (size_t i = 0; i < parsedData->bones.size(); i++)
	{
		const ModelBone_t& bone = parsedData->bones.at(i);
		const int ibone = static_cast<int>(i);

		smd->InitNode(bone.name, ibone, bone.parent);
		smd->InitFrameBone(0, ibone, bone.pos, bone.rot);
	}

	// [rika]: model is skin and bones, no meat
	if (parsedData->lods.size() == 0)
	{
		smd->SetName(fileNameBase);
		smd->Write();

		return true;
	}

	const std::filesystem::path texturePath(std::format("{}/{}", filePath.string(), fileNameBase)); // todo, remove duplicate code
	std::unordered_map<int, ModelMaterialExport_t> materials;
	HandleModelMaterials(parsedData, materials, texturePath);

	const bool isStaticProp = parsedData->studiohdr.flags & STUDIOHDR_FLAGS_STATIC_PROP ? true : false;

	CManagedBuffer* const buf = g_BufferManager.ClaimBuffer();

	for (size_t lodIdx = 0; lodIdx < parsedData->lods.size(); lodIdx++)
	{
		const ModelLODData_t& lod = parsedData->lods.at(lodIdx);

		for (const ModelModelData_t& model : lod.models)
		{
			std::string name(model.name);
			FixupExportLodNames(name, static_cast<int>(lodIdx));

			// unique prefix so we don't overwrite files
			name = std::format("{}_{}", fileNameBase, name);
			smd->SetName(name);

			for (uint32_t meshIdx = 0; meshIdx < model.meshCount; meshIdx++)
			{
				const ModelMeshData_t& meshData = lod.meshes.at(model.meshIndex + meshIdx);

				assertm(meshData.meshVertexDataIndex != invalidNoodleIdx, "mesh data hasn't been parsed ??");

				std::unique_ptr<char[]> parsedVertexDataBuf = parsedData->meshVertexData.getIdx(meshData.meshVertexDataIndex);
				const CMeshData* const parsedVertexData = reinterpret_cast<CMeshData*>(parsedVertexDataBuf.get());

				const uint16_t* const indices = parsedVertexData->GetIndices();
				const Vertex_t* const vertices = parsedVertexData->GetVertices();
				const VertexWeight_t* const weights = parsedVertexData->GetWeights();

				// [rika]: add more triangles
				smd->AddMeshCapacity(meshData.indexCount / 3u);

				const ModelMaterialData_t* const materialData = parsedData->pMaterial(meshData.materialId);

				// [rika]: making the choice to use the stored rmdl name here when possible, as that is what it was likely compiled with
				const char* material = materialData->GetName(true);
				assertm(material, "material name should always be valid");

				material = g_ExportSettings.exportModelMatsTruncated ? material : keepAfterLastSlashOrBackslash(material);

				for (uint32_t indiceIdx = 0; indiceIdx < meshData.indexCount; indiceIdx += 3)
				{
					smd->InitTriangle(material);
					smd::Triangle* const tri = smd->TopTri();

					ParseVertexIntoSMD(&vertices[indices[indiceIdx]], weights, &tri->vertices[0], isStaticProp);
					ParseVertexIntoSMD(&vertices[indices[indiceIdx + 2]], weights, &tri->vertices[1], isStaticProp);
					ParseVertexIntoSMD(&vertices[indices[indiceIdx + 1]], weights, &tri->vertices[2], isStaticProp);
				}
			}

			smd->Write(buf->Buffer(), managedBufferSize);
			smd->ResetMeshData();
		}
	}

	FreeAllocVar(smd);
	g_BufferManager.RelieveBuffer(buf);

	return true;
}

// get the used material indices for skins
// todo: move to qc
const uint32_t QC_GetUsedSkinIndices(const int16_t* const skins, const int numSkinRef, const int numSkinFamilies, int16_t* const indices)
{
	// [rika]: no actual skins, just base materials
	assertm(numSkinFamilies >= 2, "model had no additional skins");

	memset(indices, 0xff, sizeof(int16_t) * numSkinRef);

	const int16_t* skinGroup = skins + numSkinRef;

	// [rika]: parse through each skingroup to see which indices changed
	// todo: setting to skip this and write all materials
	for (int family = 0; family < numSkinFamilies - 1; family++)
	{
		for (int material = 0; material < numSkinRef; material++)
		{
			if (skins[material] == skinGroup[material])
				continue;

			indices[material] = static_cast<int16_t>(material);
		}

		skinGroup += numSkinRef;
	}

	// [rika]: make our indices sequential
	uint32_t curIdx = 0;
	for (int i = 0; i < numSkinRef; i++)
	{
		if (indices[i] == -1)
			continue;

		indices[curIdx] = indices[i];
		curIdx++;
	}

	return curIdx;
}

// parse qc commands from header
void QC_ParseStudioHeader(qc::QCFile* const qc, const ModelParsedData_t* const parsedData, const int version)
{
	using namespace qc;

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	CmdParse(qc, QC_MODELNAME, pStudioHdr->pszName());
	CmdParse(qc, QC_CONTENTS, &pStudioHdr->contents);
	CmdParse(qc, QC_SURFACEPROP, pStudioHdr->pszSurfaceProp());

	// [rika]: flag based options
	if (pStudioHdr->flags & STUDIOHDR_FLAGS_STATIC_PROP)
		CmdParse(qc, QC_STATICPROP, nullptr);

	if (version < 52)
	{
		if (pStudioHdr->flags & STUDIOHDR_FLAGS_FORCE_OPAQUE)
		{
			assertm(!(pStudioHdr->flags & STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS), "translucent and opaque");
			CmdParse(qc, QC_OPAQUE, nullptr);
		}

		// removed in r1, parsed from materials. flag replaced in r5, used by arms and loading models a lot!
		if (pStudioHdr->flags & STUDIOHDR_FLAGS_TRANSLUCENT_TWOPASS)
		{
			assertm(!(pStudioHdr->flags & STUDIOHDR_FLAGS_FORCE_OPAQUE), "translucent and opaque");
			CmdParse(qc, QC_MOSTLYOPAQUE, nullptr);
		}
	}
	else
	{
		// [rika]: pretty sure this one is still supported!
		if (pStudioHdr->flags & STUDIOHDR_FLAGS_FORCE_OPAQUE)
		{
			CmdParse(qc, QC_OPAQUE, nullptr);
		}
	}

	if (version != 54 && pStudioHdr->flags & STUDIOHDR_FLAGS_OBSOLETE)
	{
		CmdParse(qc, QC_OBSOLETE, nullptr);
	}

	if (pStudioHdr->flags & STUDIOHDR_FLAGS_NO_FORCED_FADE)
	{
		CmdParse(qc, QC_NOFORCEDFADE, nullptr);
	}

	if (pStudioHdr->flags & STUDIOHDR_FLAGS_FORCE_PHONEME_CROSSFADE)
	{
		CmdParse(qc, QC_FORCEPHONEMECROSSFADE, nullptr);
	}

	if (pStudioHdr->flags & STUDIOHDR_FLAGS_CONSTANT_DIRECTIONAL_LIGHT_DOT)
	{
		CmdParse(qc, QC_CONSTANTDIRECTIONALLIGHT, &pStudioHdr->constdirectionallightdot);
	}
	
	// believe this is different later seasons 
	if (pStudioHdr->flags & STUDIOHDR_FLAGS_AMBIENT_BOOST)
	{
		CmdParse(qc, QC_AMBIENTBOOST, nullptr);
	}

	if (pStudioHdr->flags & STUDIOHDR_FLAGS_DO_NOT_CAST_SHADOWS)
	{
		CmdParse(qc, QC_DONOTCASTSHADOWS, nullptr);
	}

	// believe this is different later seasons (used on non static props)
	if (pStudioHdr->flags & STUDIOHDR_FLAGS_CAST_TEXTURE_SHADOWS)
	{
		CmdParse(qc, QC_CASTTEXTURESHADOWS, nullptr);
	}

	if (version != 54 && pStudioHdr->flags & STUDIOHDR_FLAGS_SUBDIVISION_SURFACE)
	{
		CmdParse(qc, QC_SUBD, nullptr);
	}

	CmdParse(qc, QC_FADEDISTANCE, &pStudioHdr->fadeDistance);

	CmdParse(qc, QC_EYEPOSITION, &pStudioHdr->eyeposition, 3);

	if (pStudioHdr->flMaxEyeDeflection != 0.0f)
	{
		CmdParse(qc, QC_MAXEYEDEFLECTION, &pStudioHdr->flMaxEyeDeflection);
	}

	if (pStudioHdr->numSkinRef > 0)
	{
		const char** materials = new const char*[pStudioHdr->numSkinRef]{};
		const char** materialFullPaths = nullptr;

		// if we are going to rename materials
		if (g_ExportSettings.exportModelMatsTruncated)
		{
			materialFullPaths = new const char*[pStudioHdr->numSkinRef]{};
		}

		for (int i = 0; i < pStudioHdr->numSkinRef; i++)
		{
			materials[i] = parsedData->pMaterial(i)->GetName(true);

			// are the materials getting shortened ?
			if (!g_ExportSettings.exportModelMatsTruncated)
				continue;

			assertm(materialFullPaths, "material path array was not allocated");

			materialFullPaths[i] = materials[i];
			materials[i] = keepAfterLastSlashOrBackslash(materials[i]);
		}

		if (pStudioHdr->numSkinFamilies > 1)
		{
			const int16_t* const skins = pStudioHdr->pSkinref(0);
			int16_t* const indices = new int16_t[pStudioHdr->numSkinRef]{};

			const uint32_t usedIndices = QC_GetUsedSkinIndices(skins, pStudioHdr->numSkinRef, pStudioHdr->numSkinFamilies, indices);

			const TextureGroupData_t texturegroup(materials, skins, indices, pStudioHdr->numSkinRef, pStudioHdr->numSkinFamilies, usedIndices);
			CmdParse(qc, QC_TEXTUREGROUP, &texturegroup);

			FreeAllocArray(indices);
		}

		if(g_ExportSettings.exportModelMatsTruncated)
		{
			for (int i = 0; i < pStudioHdr->numSkinRef; i++)
			{
				const CommandOptionPair_t renameData(materials[i], materialFullPaths[i]);
				CmdParse(qc, QC_RENAMEMATERIAL, &renameData);
			}
		}

		FreeAllocArray(materials);
		FreeAllocArray(materialFullPaths);
	}

	// [rika]: most models will have at least one (unless it's retail apex), it will be the path prefixing 'mdl' or 'models', in most cases this is just an empty string
	for (int i = 0; i < pStudioHdr->cdTexturesCount; i++)
		CmdParse(qc, QC_CDMATERIALS, pStudioHdr->pCdtexture(i));

	if (pStudioHdr->keyValueSize > 0 || (pStudioHdr->keyValueOffset && pStudioHdr->keyValueSize == -1))
		CmdParse(qc, QC_KEYVALUES, pStudioHdr->KeyValueText());

	if (pStudioHdr->numAllowedRootLODs > 0)
		CmdParse(qc, QC_ALLOWROOTLODS, &pStudioHdr->numAllowedRootLODs);
	
	if (pStudioHdr->rootLOD > 0)
		CmdParse(qc, QC_MINLOD, &pStudioHdr->rootLOD);
}

void QC_ParseStudioBone(qc::QCFile* const qc, const ModelParsedData_t* const parsedData, const ModelBone_t* const bone, const int version)
{
	using namespace qc;

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	// [rika]: parse out the data required for a $definebone (and other) command(s)
	const char* const parent = bone->parent > -1 ? parsedData->pBone(bone->parent)->pszName() : nullptr;
	const matrix3x4_t* pPostTransform = nullptr;

	// [rika]: get data for fixups if it exits
	if (pStudioHdr->srcBoneTransformCount)
	{
		const mstudiosrcbonetransform_t* const pSrcBoneTransform = GetSrcBoneTransform(bone->name, pStudioHdr->pSrcBoneTransform(0), pStudioHdr->srcBoneTransformCount);

		if (pSrcBoneTransform)
			pPostTransform = &pSrcBoneTransform->posttransform;
	}

	const BoneData_t boneData(bone->pszName(), parent/*, bone->pszSurfaceProp()*/, bone->contents, &bone->pos, &bone->rot, pPostTransform);
	const CommandOptionPair_t boneSurface(bone->pszName(), bone->pszSurfaceProp());

	CmdParse(qc, QC_DEFINEBONE, &boneData);

	// THESE NEED NAMES, save data in DefineBoneData_t and pass to these funcs
	CmdParse(qc, QC_JOINTCONTENTS, &boneData);
	CmdParse(qc, QC_JOINTSURFACEPROP, &boneSurface);

	if (bone->flags & BONE_USED_BY_BONE_MERGE)
	{
		CmdParse(qc, QC_BONEMERGE, bone->pszName());
	}

	if (bone->proctype > 0)
	{
		switch (bone->proctype)
		{
		case STUDIO_PROC_JIGGLE:
		{
			JiggleData_t* jiggleData = nullptr;

			if (version == 54)
			{
				const r5::mstudiojigglebone_v8_t* const jigglebone = reinterpret_cast<const r5::mstudiojigglebone_v8_t* const>(bone->pProcedure());

				jiggleData = new JiggleData_t(bone->pszName(), jigglebone->flags);

				// todo: just do this in the constructor (I was lazy tonight)
				jiggleData->FixFlagsApex(); // not ideal but will work for now
				jiggleData->SetGeneral(jigglebone->length, jigglebone->tipMass, jigglebone->tipFriction);
				jiggleData->SetFlexible(jigglebone->yawStiffness, jigglebone->yawDamping, jigglebone->pitchStiffness, jigglebone->pitchDamping, jigglebone->alongStiffness, jigglebone->alongDamping);
				jiggleData->SetAngleConstraint(jigglebone->angleLimit);
				jiggleData->SetYawConstraint(jigglebone->minYaw, jigglebone->maxYaw, jigglebone->yawFriction, jigglebone->yawBounce);
				jiggleData->SetPitchConstraint(jigglebone->minPitch, jigglebone->maxPitch, jigglebone->pitchFriction, jigglebone->pitchBounce);
				jiggleData->SetBaseSpring(jigglebone->baseMass, jigglebone->baseStiffness, jigglebone->baseDamping, jigglebone->baseMinLeft, jigglebone->baseMaxLeft, jigglebone->baseLeftFriction,
					jigglebone->baseMinUp, jigglebone->baseMaxUp, jigglebone->baseUpFriction, jigglebone->baseMinForward, jigglebone->baseMaxForward, jigglebone->baseForwardFriction);
			}
			else
			{
				// temp
				break;
			}
		
			CmdParse(qc, QC_JIGGLEBONE, jiggleData);

			FreeAllocVar(jiggleData);

			break;
		}
		}
	}
}

constexpr const char* const s_QCModelNameFormat = "%s_%s%s\0"; // file name, model name, extension
static uint32_t s_QCMaxVerts = 0u;
void QC_ParseStudioBodypart(qc::QCFile* const qc, const ModelParsedData_t* const parsedData, const ModelBodyPart_t* const bodypart, const char* const stem, const int setting)
{
	using namespace qc;

	if (bodypart->numModels < 1)
	{
		assertm(false, "invalid model count");
		return;
	}

	assertm(!parsedData->lods.empty(), "model had bodyparts but no lods");

	const ModelLODData_t* const lodData0 = parsedData->pLOD(0);

	char buf[MAX_PATH]{};

	// single model
	// handle $model (vertex anim) eventually
	if (bodypart->numModels == 1)
	{
		const ModelModelData_t* const modelData = lodData0->pModel(bodypart->modelIndex);

		std::string name(modelData->name);
		FixupExportLodNames(name, 0);
		snprintf(buf, MAX_PATH, s_QCModelNameFormat, stem, name.c_str(), s_ModelExportExtensions[setting]);

		const CommandOptionPair_t bodyData(bodypart->GetNameCStr(), buf);

		CmdParse(qc, QC_BODY, &bodyData, 1, true);

		s_QCMaxVerts = modelData->vertCount > s_QCMaxVerts ? modelData->vertCount : s_QCMaxVerts;

		return;
	}

	BodyGroupData_t bodyGroupData(bodypart->GetNameCStr(), bodypart->numModels);

	for (uint32_t i = 0; i < static_cast<uint32_t>(bodypart->numModels); i++)
	{
		const ModelModelData_t* const modelData = lodData0->pModel(bodypart->modelIndex + i);

		if (modelData->meshCount == 0)
		{
			bodyGroupData.SetBlank(i);
			continue;
		}

		std::string name(modelData->name);
		FixupExportLodNames(name, 0);
		snprintf(buf, MAX_PATH, s_QCModelNameFormat, stem, name.c_str(), s_ModelExportExtensions[setting]);
		
		bodyGroupData.SetPart(qc, i, buf);

		s_QCMaxVerts = modelData->vertCount > s_QCMaxVerts ? modelData->vertCount : s_QCMaxVerts;
	}

	CmdParse(qc, QC_BODYGROUP, &bodyGroupData, 1, true);
}

const uint32_t QC_ParseStudioLODBone(const ModelParsedData_t* const parsedData, const size_t lod, const uint32_t boneId)
{
	const ModelBone_t* const bone = parsedData->pBone(boneId);

	// bone is used by this lod, don't replace
	if (bone->flags & BONE_USED_BY_VERTEX_AT_LOD(lod))
		return boneId;

	if (bone->parent == -1)
	{
		assertm(false, "bone with no parent unused by LOD");
		return boneId;
	}

	const uint32_t out = QC_ParseStudioLODBone(parsedData, lod, bone->parent);
	return out;
}

void QC_ParseStudioLOD(qc::QCFile* const qc, const ModelParsedData_t* const parsedData, const size_t lod, const bool isShadowLOD, const char* const stem, const int setting)
{
	using namespace qc;

	const ModelLODData_t* const lodData0 = parsedData->pLOD(lod);
	const ModelLODData_t* const lodData = parsedData->pLOD(lod);
	assertm(lodData->GetModelCount() == lodData0->GetModelCount(), "mismatched model count, very bad!");
	
	LodData_t lodGroupData(lodData->GetModelCount(), parsedData->BoneCount(), lodData->switchPoint, isShadowLOD);

	// models
	char bufBase[MAX_PATH]{};
	char bufReplace[MAX_PATH]{};

	for (uint32_t i = 0; i < static_cast<uint32_t>(lodData->GetModelCount()); i++)
	{
		const ModelModelData_t* const modelData0 = lodData0->pModel(i);
		const ModelModelData_t* const modelData = lodData->pModel(i);

		// blank model
		if (modelData->vertCount == 0 && modelData0->vertCount == 0)
			continue;

		std::string base(modelData->name);
		FixupExportLodNames(base, 0);
		snprintf(bufBase, MAX_PATH, s_QCModelNameFormat, stem, base.c_str(), s_ModelExportExtensions[setting]);

		if (modelData->vertCount == 0 && modelData0->vertCount > 0)
		{
			lodGroupData.RemoveModel(qc, i, bufBase);
			continue;
		}
		
		std::string replace(modelData->name);
		FixupExportLodNames(replace, static_cast<int>(lod));
		snprintf(bufReplace, MAX_PATH, s_QCModelNameFormat, stem, replace.c_str(), s_ModelExportExtensions[setting]);

		lodGroupData.ReplaceModel(qc, i, bufBase, bufReplace);

		s_QCMaxVerts = modelData->vertCount > s_QCMaxVerts ? modelData->vertCount : s_QCMaxVerts;
	}

	// bones
	// [rika]: do we need to check for attachments ? I don't think we can remove a bone if it has attachments
	for (uint32_t i = 0; i < static_cast<uint32_t>(parsedData->BoneCount()); i++)
	{	
		const uint32_t boneId = QC_ParseStudioLODBone(parsedData, lod, i);

		if (boneId == i)
			continue;

		const ModelBone_t* const pBoneChild = parsedData->pBone(i);
		const ModelBone_t* const pBoneParent = parsedData->pBone(boneId);

		lodGroupData.ReplaceBone(i, pBoneChild->pszName(), pBoneParent->pszName());
	}

	if (lodGroupData.isShadowLOD && parsedData->pStudioHdr()->flags & STUDIOHDR_FLAGS_USE_SHADOWLOD_MATERIALS)
		lodGroupData.useShadowLODMaterials = true;

	const CommandList_t lodType = isShadowLOD ? QC_SHADOWLOD : QC_LOD;

	CmdParse(qc, lodType, &lodGroupData);
}

void QC_ParseStudioAttachments(qc::QCFile* const qc, const ModelParsedData_t* const parsedData)
{
	using namespace qc;

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	const bool staticProp = pStudioHdr->flags & STUDIOHDR_FLAGS_STATIC_PROP;
	const bool useIllumAttachment = IllumPositionData_t::useAttachment(pStudioHdr->illumpositionattachmentindex);
	size_t illumpositionattachmentindex = pStudioHdr->illumpositionattachmentindex - 1;

	bool useAutoCenter = false;
	//size_t autoCenterIndex = 0ull;

	const size_t numAttachments = parsedData->attachments.size();
	for (size_t i = 0; i < numAttachments; i++)
	{
		// skip __illumPosition attachment
		if (useIllumAttachment && illumpositionattachmentindex == i)
			continue;

		const ModelAttachment_t* const attachment = parsedData->pAttachment(i);

		// should we skip this
		if (staticProp && !useAutoCenter && !strncmp("placementOrigin", attachment->name, 16))
		{
			useAutoCenter = true;
			//autoCenterIndex = i;
		}

		// invalid index in apex (sometimes?), yippee!!!
		if (useIllumAttachment && illumpositionattachmentindex >= numAttachments && !strncmp("__illumPosition", attachment->name, 16))
		{
			illumpositionattachmentindex = i;
			continue;
		}

		const AttachmentData_t attachmentData(attachment->name, parsedData->pBone(attachment->localbone)->pszName(), attachment->flags, attachment->localmatrix);

		CmdParse(qc, QC_ATTACHMENT, &attachmentData);
	}

	// illumposition
	// [rika]: not a fan of this being done outside qc, but also don't wanna put out custom types into qc (supposed to be universial for [redacted])
	{
		const char* localbone = nullptr;
		const matrix3x4_t* localmatrix = nullptr;

		if (IllumPositionData_t::useAttachment(pStudioHdr->illumpositionattachmentindex))
		{
			const ModelAttachment_t* const pAttachment = parsedData->pAttachment(illumpositionattachmentindex); // use fixed index incase it is invalid

			localbone = parsedData->pBone(pAttachment->localbone)->name;
			localmatrix = pAttachment->localmatrix;
		}

		const IllumPositionData_t illumdata(&pStudioHdr->illumposition, pStudioHdr->illumpositionattachmentindex, localbone, localmatrix);

		CmdParse(qc, QC_ILLUMPOSITION, &illumdata);
	}

	if (useAutoCenter)
	{
		CmdParse(qc, QC_AUTOCENTER, nullptr);
	}
}

void QC_ParseStudioBoxes(qc::QCFile* const qc, const ModelParsedData_t* const parsedData)
{
	using namespace qc;

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	const bool autoGeneratedHitbox = pStudioHdr->flags & STUDIOHDR_FLAGS_AUTOGENERATED_HITBOX;
	bool skipBoneInBBox = false;

	const CommandOptionPair_t bboxData(pStudioHdr->hull_min.Base(), pStudioHdr->hull_max.Base(), 3, 3);
	CmdParse(qc, QC_BBOX, &bboxData);

	const CommandOptionPair_t cboxData(pStudioHdr->view_bbmin.Base(), pStudioHdr->view_bbmax.Base(), 3, 3);
	CmdParse(qc, QC_CBOX, &cboxData);

	// [rika]: it's important to parse these in order
	for (size_t i = 0; i < parsedData->hitboxsets.size(); i++)
	{
		const ModelHitboxSet_t* const hitboxSetData = parsedData->pHitboxSet(i);
		CmdParse(qc, QC_HBOXSET, hitboxSetData->name, 1, false, autoGeneratedHitbox);

		for (int boxIdx = 0; boxIdx < hitboxSetData->numHitboxes; boxIdx++)
		{
			const ModelHitbox_t* const hitboxData = hitboxSetData->hitboxes + boxIdx;
			const Hitbox_t hboxData(parsedData->pBone(hitboxData->bone)->pszName(), hitboxData->group, hitboxData->bbmin, hitboxData->bbmax, hitboxData->name, hitboxData->forceCritPoint);

			CmdParse(qc, QC_HBOX, &hboxData, 1, false, autoGeneratedHitbox);

			// hgroup
			if (autoGeneratedHitbox)
			{
				const CommandOptionPair_t hgroupData(hitboxData->group, parsedData->pBone(hitboxData->bone)->pszName());
				CmdParse(qc, QC_HGROUP, &hgroupData);
			}

			if (autoGeneratedHitbox && !skipBoneInBBox)
			{
				const Vector& bbmin = *hitboxData->bbmin;
				const Vector& bbmax = *hitboxData->bbmax;

				for (uint32_t axis = 0; axis < 3; axis++)
				{
					skipBoneInBBox = bbmin[axis] > 0.0f || bbmax[axis] < 0.0f ? true : skipBoneInBBox;
				}
			}
		}
	}

	if (skipBoneInBBox)
	{
		CmdParse(qc, QC_SKIPBONEINBBOX, nullptr);
	}
}

void QC_ParseStudioAnimation(qc::QCFile* const qc, const ModelParsedData_t* const parsedData)
{
	using namespace qc;

	const studiohdr_generic_t* const pStudioHdr = parsedData->pStudioHdr();

	for (int i = 0; i < pStudioHdr->localPoseParamCount; i++)
	{
		assertm(parsedData->poseparams, "invalid pointer, wicked bad");

		const ModelPoseParam_t* const poseParam = parsedData->poseparams + i;
		const PoseParamData_t poseParamData(poseParam->name, poseParam->flags, poseParam->start, poseParam->end, poseParam->loop);
		CmdParse(qc, QC_POSEPARAMETER, &poseParamData);
	}

	for (int i = 0; i < pStudioHdr->ikChainCount; i++)
	{
		assertm(parsedData->ikchains, "invalid pointer, wicked bad");

		const ModelIKChain_t* const ikChain = parsedData->ikchains + i;
		const IKChainData_t ikChainData(ikChain->name, parsedData->pBone(ikChain->links[ModelIKChain_t::IKLINK_FOOT].bone)->pszName(), &ikChain->links[ModelIKChain_t::IKLINK_THIGH].kneeDir, ikChain->unk_10);
		CmdParse(qc, QC_IKCHAIN, &ikChainData);
	}

	for (int i = 0; i < pStudioHdr->localIkAutoPlayLockCount; i++)
	{
		assertm(parsedData->iklocks, "invalid pointer, wicked bad");

		const ModelIKLock_t* const ikLock = parsedData->iklocks + i;
		assertm(ikLock->chain < pStudioHdr->ikChainCount, "invaild ik chain index");

		const IKLockData_t ikLockData(parsedData->ikchains[ikLock->chain].name, ikLock->flPosWeight, ikLock->flLocalQWeight);
		CmdParse(qc, QC_IKAUTOPLAYLOCK, &ikLockData);
	}



	for (int i = 0; i < pStudioHdr->includeModelCount; i++)
	{
		const mstudiomodelgroup_t* const includemodel = reinterpret_cast<const mstudiomodelgroup_t* const>(pStudioHdr->baseptr + pStudioHdr->includeModelOffset) + i;
		CmdParse(qc, QC_INCLUDEMODEL, includemodel->pszName());
	}
}

bool ExportModelQC(const ModelParsedData_t* const parsedData, std::filesystem::path& exportPath, const int setting, const int version)
{
	if (!parsedData->studiohdr.baseptr)
		return false;

	const std::string fileStem(exportPath.stem().string());

	CManagedBuffer* buf = g_BufferManager.ClaimBuffer();

	qc::QCFile qc(exportPath, buf->Buffer(), managedBufferSize);

	QC_ParseStudioHeader(&qc, parsedData, version);

	for (int i = 0; i < parsedData->BoneCount(); i++)
		QC_ParseStudioBone(&qc, parsedData, parsedData->pBone(i), version);

	s_QCMaxVerts = 0u;
	for (size_t i = 0; i < parsedData->bodyParts.size(); i++)
		QC_ParseStudioBodypart(&qc, parsedData, parsedData->pBodypart(i), fileStem.c_str(), setting);

	if (parsedData->lods.size() > 1)
	{
		for (size_t i = 1; i < parsedData->lods.size(); i++)
		{
			if (parsedData->pStudioHdr()->flags & STUDIOHDR_FLAGS_HASSHADOWLOD && parsedData->pLOD(i)->switchPoint == -1.0f)
			{
				QC_ParseStudioLOD(&qc, parsedData, i, true, fileStem.c_str(), setting);
				continue;
			}

			QC_ParseStudioLOD(&qc, parsedData, i, false, fileStem.c_str(), setting);
		}
	}

	s_QCMaxVerts++;
	assertm(s_QCMaxVerts < s_MaxStudioVerts, "invalid max vert value");
	if (s_QCMaxVerts > (s_MaxStudioVerts / 3))
		qc::CmdParse(&qc, qc::QC_MAXVERTS, &s_QCMaxVerts);

	// attachments
	QC_ParseStudioAttachments(&qc, parsedData);

	// boxes and boxes and boxes and
	QC_ParseStudioBoxes(&qc, parsedData);

	// animation and sequences
	QC_ParseStudioAnimation(&qc, parsedData);

	// get version from settings
	qc::Version_t qcTargetVersion;
	qcTargetVersion.SetVersion(static_cast<qc::MajorVersion_t>(g_ExportSettings.qcMajorVersion), g_ExportSettings.qcMinorVersion);

	// export qc file
	qc::QCFile::SetExportVersion(qcTargetVersion);
	qc.ParseToText(false);

	g_BufferManager.RelieveBuffer(buf);

	return true;
}

// export a seqdesc to rmax
bool ExportSeqDescRMAX(const seqdesc_t* const seqdesc, std::filesystem::path& exportPath, const char* const skelName, const std::vector<ModelBone_t>* const bones)
{
	const std::string fileNameBase = exportPath.stem().string();
	const std::string skelNameBase = std::filesystem::path(skelName).stem().string();

	const size_t boneCount = bones->size();

	for (int animIdx = 0; animIdx < seqdesc->AnimCount(); animIdx++)
	{
		const std::string animName = std::format("{}{}", fileNameBase.c_str(), animIdx);

		const std::string tmpName = std::format("{}.rmax", animName);
		exportPath.replace_filename(tmpName);

		rmax::RMAXExporter rmaxFile(exportPath, fileNameBase.c_str(), fileNameBase.c_str());

		// do bones
		rmaxFile.ReserveBones(boneCount);
		for (auto& bone : *bones)
			rmaxFile.AddBone(bone.name, bone.parent, bone.pos, bone.quat, bone.scale);

		const animdesc_t* const animdesc = &seqdesc->anims.at(animIdx); // check flag 0x20000

		uint16_t animFlags = 0;

		if (animdesc->flags & eStudioAnimFlags::ANIM_DELTA) // delta flag
			animFlags |= rmax::AnimFlags_t::ANIM_DELTA;

		// [rika]: not touching this for now since we really don't care about empty bones on types not for re import
		if (!(animdesc->flags & eStudioAnimFlags::ANIM_VALID) || animdesc->parsedBufferIndex == invalidNoodleIdx)
			animFlags |= rmax::AnimFlags_t::ANIM_EMPTY;

		rmaxFile.AddAnim(animName.c_str(), static_cast<uint16_t>(animdesc->numframes), animdesc->fps, animFlags, boneCount);
		rmax::RMAXAnim* const anim = rmaxFile.GetAnimLast();

		if (anim->GetFlags() & rmax::AnimFlags_t::ANIM_EMPTY)
		{
			rmaxFile.ToFile();

			continue;
		}

		const std::unique_ptr<char[]> noodle = seqdesc->parsedData.getIdx(animdesc->parsedBufferIndex);
		CAnimData animData(noodle.get());

		for (int i = 0; i < boneCount; i++)
		{
			const uint8_t flags = animData.GetFlag(i);

			anim->SetTrack(flags, static_cast<uint16_t>(i));
			rmax::RMAXAnimTrack* const track = anim->GetTrack(i);

			const Vector* pos = nullptr;
			const Quaternion* q = nullptr;
			const Vector* scale = nullptr;

			if (flags & CAnimDataBone::ANIMDATA_POS)
				pos = animData.GetBonePosForFrame(i, 0);

			if (flags & CAnimDataBone::ANIMDATA_ROT)
				q = animData.GetBoneQuatForFrame(i, 0);

			if (flags & CAnimDataBone::ANIMDATA_SCL)
				scale = animData.GetBoneScaleForFrame(i, 0);

			for (int frameIdx = 0; frameIdx < animdesc->numframes; frameIdx++)
			{
				track->AddFrame(frameIdx, &pos[frameIdx], &q[frameIdx], &scale[frameIdx]);
			}
		}

		rmaxFile.ToFile();
	}

	return true;
}

// export a seq desc to cast
bool ExportSeqDescCast(const seqdesc_t* const seqdesc, std::filesystem::path& exportPath, const char* const skelName, const std::vector<ModelBone_t>* const bones, const uint64_t guid)
{
	const std::string fileNameBase = exportPath.stem().string();
	const std::string skelNameBase = std::filesystem::path(skelName).stem().string();

	const size_t boneCount = bones->size();

	for (int animIdx = 0; animIdx < seqdesc->AnimCount(); animIdx++)
	{
		const animdesc_t* const animdesc = &seqdesc->anims.at(animIdx);

		const std::string tmpName(std::format("{}_{}.cast", fileNameBase, std::to_string(animIdx)));
		exportPath.replace_filename(tmpName);

		cast::CastExporter cast(exportPath.string());

		// cast
		cast::CastNode* const rootNode = cast.GetChild(0); // we only have one root node, no hash
		cast::CastNode* const animNode = rootNode->AddChild(cast::CastId::Animation, guid);

		// [rika]: we can predict how big this vector needs to be, however resizing it will make adding new members a pain.
		const size_t animChildrenCount = 1 + (boneCount * 7); // skeleton (one), curve per bone per data type
		animNode->ReserveChildren(animChildrenCount);
		animNode->ReserveProperties(2); // framerate, looping

		animNode->AddProperty(cast::CastPropertyId::Float, static_cast<int>(cast::CastPropsAnimation::Framerate), FLOAT_AS_UINT(animdesc->fps));
		animNode->AddProperty(cast::CastPropertyId::Byte, static_cast<int>(cast::CastPropsAnimation::Looping), animdesc->flags & eStudioAnimFlags::ANIM_LOOPING ? true : false);

		// do skeleton
		{
			// it would be more ideal to just feed it bones, but I don't want to deal with that mess of functions currently
			cast::CastNode* const skelNode = animNode->AddChild(cast::CastId::Skeleton, RTech::StringToGuid(fileNameBase.c_str()));
			skelNode->ReserveChildren(boneCount);

			// uses hashes for lookup, still gets bone parents by index :clown:
			for (size_t i = 0; i < boneCount; i++)
			{
				const ModelBone_t* const boneData = &bones->at(i);

				cast::CastNodeBone boneNode(skelNode);
				boneNode.MakeBone(boneData->name, boneData->parent, &boneData->pos, &boneData->quat, false);
			}
		}

		// [rika]: not touching this for now since we really don't care about empty bones on types not for re import
		if (!(animdesc->flags & eStudioAnimFlags::ANIM_VALID) || animdesc->parsedBufferIndex == invalidNoodleIdx)
		{
			cast.ToFile();

			continue;
		}

		const std::unique_ptr<char[]> noodle = seqdesc->parsedData.getIdx(animdesc->parsedBufferIndex);
		CAnimData animData(noodle.get());

		const cast::CastPropsCurveMode curveMode = animdesc->flags & eStudioAnimFlags::ANIM_DELTA ? cast::CastPropsCurveMode::MODE_ADDITIVE : cast::CastPropsCurveMode::MODE_ABSOLUTE;

		// setup the stupid key frame buffer thing that cast curves use
		cast::CastPropertyId frameBufferId;
		void* const frameBuffer = cast::CastNodeCurve::MakeCurveKeyFrameBuffer(static_cast<size_t>(animdesc->numframes), frameBufferId);

		Vector deltaPos(0.0f, 0.0f, 0.0f);
		Quaternion deltaQuat(0.0f, 0.0f, 0.0f, 1.0f);
		Vector deltaScale(1.0f, 1.0f, 1.0f);

		for (int i = 0; i < boneCount; i++)
		{
			const ModelBone_t* const boneData = &bones->at(i);

			// parsed data
			const uint8_t flags = animData.GetFlag(i);

			// weight for delta anims
			const float animWeight = seqdesc->Weight(i);

			if (flags & CAnimDataBone::ANIMDATA_POS)
			{
				cast::CastNodeCurve curveNode(animNode);
				curveNode.MakeCurveVector(boneData->name, animData.GetBonePosForFrame(i, 0), static_cast<size_t>(animdesc->numframes), frameBuffer, static_cast<size_t>(animdesc->numframes), cast::CastPropsCurveValue::POS_X, curveMode, animWeight);
			}
			else
			{
				const Vector* const track = animdesc->flags & eStudioAnimFlags::ANIM_DELTA ? &deltaPos : &boneData->pos;

				cast::CastNodeCurve curveNode(animNode);
				curveNode.MakeCurveVector(boneData->name, track, 1ull, frameBuffer, static_cast<size_t>(animdesc->numframes), cast::CastPropsCurveValue::POS_X, curveMode, animWeight);
			}

			if (flags & CAnimDataBone::ANIMDATA_ROT)
			{
				cast::CastNodeCurve curveNode(animNode);
				curveNode.MakeCurveQuaternion(boneData->name, animData.GetBoneQuatForFrame(i, 0), static_cast<size_t>(animdesc->numframes), frameBuffer, static_cast<size_t>(animdesc->numframes), curveMode, animWeight);
			}
			else
			{
				const Quaternion* const track = animdesc->flags & eStudioAnimFlags::ANIM_DELTA ? &deltaQuat : &boneData->quat;

				cast::CastNodeCurve curveNode(animNode);
				curveNode.MakeCurveQuaternion(boneData->name, track, 1ull, frameBuffer, static_cast<size_t>(animdesc->numframes), curveMode, animWeight);
			}

			// check if the sequence has scale data.
			if (seqdesc->flags & 0x20000)
			{
				if (flags & CAnimDataBone::ANIMDATA_SCL)
				{
					cast::CastNodeCurve curveNode(animNode);
					curveNode.MakeCurveVector(boneData->name, animData.GetBoneScaleForFrame(i, 0), static_cast<size_t>(animdesc->numframes), frameBuffer, static_cast<size_t>(animdesc->numframes), cast::CastPropsCurveValue::SCL_X, curveMode, animWeight);
				}
				else
				{
					const Vector* track = animdesc->flags & eStudioAnimFlags::ANIM_DELTA ? &deltaScale : &boneData->scale;

					cast::CastNodeCurve curveNode(animNode);
					curveNode.MakeCurveVector(boneData->name, track, 1ull, frameBuffer, static_cast<size_t>(animdesc->numframes), cast::CastPropsCurveValue::SCL_X, curveMode, animWeight);
				}
			}
		}

		cast.ToFile();

		delete[] frameBuffer;
	}

	return true;
}

bool ExportSeqDescSMD(const seqdesc_t* const seqdesc, std::filesystem::path& exportPath, const char* const skelName, const std::vector<ModelBone_t>* const bones)
{
	const std::string fileNameBase = exportPath.stem().string();
	const std::string skelNameBase = std::filesystem::path(skelName).stem().string();

	const size_t boneCount = bones->size();

	smd::CStudioModelData* const smd = new smd::CStudioModelData(exportPath.parent_path(), bones->size(), 1ull);

	// [rika]: initialize the nodes
	for (size_t i = 0; i < boneCount; i++)
	{
		const ModelBone_t& bone = bones->at(i);

		smd->InitNode(bone.name, static_cast<int>(i), bone.parent);
	}

	const Vector deltaPos(0.0f, 0.0f, 0.0f);
	const Quaternion deltaQuat(0.0f, 0.0f, 0.0f, 1.0f);

	CManagedBuffer* const buf = g_BufferManager.ClaimBuffer();

	for (int animIdx = 0; animIdx < seqdesc->AnimCount(); animIdx++)
	{
		const animdesc_t* const animdesc = &seqdesc->anims.at(animIdx);

		assertm(animdesc->name, "name was nullptr");
		std::string animname(animdesc->name);
		animname.append(".smd");

		smd->ResetFrameData(static_cast<size_t>(animdesc->numframes));
		smd->SetName(animname);

		if (animdesc->parsedBufferIndex == invalidNoodleIdx)
		{
			smd->Write();

			continue;
		}

		const std::unique_ptr<char[]> noodle = seqdesc->parsedData.getIdx(animdesc->parsedBufferIndex);
		CAnimData animData(noodle.get());

		for (int frame = 0; frame < animdesc->numframes; frame++)
		{
			for (int bone = 0; bone < boneCount; bone++)
			{
				const ModelBone_t* const boneData = &bones->at(bone);

				// parsed data
				const uint8_t flags = animData.GetFlag(bone);

				const Vector* pos = nullptr;
				const Quaternion* q = nullptr;

				if (flags & CAnimDataBone::ANIMDATA_POS)
					pos = animData.GetBonePosForFrame(bone, frame);
				else
					pos = animdesc->flags & eStudioAnimFlags::ANIM_DELTA ? &deltaPos : &boneData->pos;

				if (flags & CAnimDataBone::ANIMDATA_ROT)
					q = animData.GetBoneQuatForFrame(bone, frame);
				else
					q = animdesc->flags & eStudioAnimFlags::ANIM_DELTA ? &deltaQuat : &boneData->quat;

				assertm(pos, "should not be nullptr");
				assertm(q, "should not be nullptr");

				const RadianEuler rot(*q);

				smd->InitFrameBone(frame, bone, *pos, rot);
			}
		}

		smd->Write(buf->Buffer(), managedBufferSize);
	}

	FreeAllocVar(smd);
	g_BufferManager.RelieveBuffer(buf);

	return true;
}

bool ExportSeqDesc(const int setting, const seqdesc_t* const seqdesc, std::filesystem::path& exportPath, const char* const skelName, const std::vector<ModelBone_t>* const bones, const uint64_t guid)
{
	switch (setting)
	{

	case eAnimSeqExportSetting::ANIMSEQ_CAST:
	{
		return ExportSeqDescCast(seqdesc, exportPath, skelName, bones, guid);
	}
	case eAnimSeqExportSetting::ANIMSEQ_RMAX:
	{
		return ExportSeqDescRMAX(seqdesc, exportPath, skelName, bones);
	}
	case eAnimSeqExportSetting::ANIMSEQ_SMD:
	{
		return ExportSeqDescSMD(seqdesc, exportPath, skelName, bones);
	}
	case eAnimSeqExportSetting::ANIMSEQ_RSEQ:
	{
		return false;
	}
	default:
	{
		assertm(false, "Export setting is not handled.");
		return false;
	}
	}
}

//
// PREVIEWDATA
//
void UpdateModelBoneMatrix(CDXDrawData* const drawData, const ModelParsedData_t* const parsedData)
{
	ID3D11DeviceContext* const ctx = g_dxHandler->GetDeviceContext();

	D3D11_MAPPED_SUBRESOURCE resource;
	HRESULT hr = ctx->Map(
		drawData->boneMatrixBuffer, 0,
		D3D11_MAP_WRITE_DISCARD, 0,
		&resource
	);

	assert(SUCCEEDED(hr));

	if (FAILED(hr))
		return;

	matrix3x4_t* boneArray = reinterpret_cast<matrix3x4_t*>(resource.pData);

	int i = 0;
	for (const ModelBone_t& bone : parsedData->bones)
	{
		const XMMATRIX bonePosMatrix = XMLoadFloat3x4(reinterpret_cast<const XMFLOAT3X4*>(&bone.poseToBone));
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(boneArray + i), bonePosMatrix);

		i++;
	}

	ctx->Unmap(drawData->boneMatrixBuffer, 0);
}

void InitModelBoneMatrix(CDXDrawData* const drawData, const ModelParsedData_t* const parsedData)
{
	ID3D11Device* const device = g_dxHandler->GetDevice();

	D3D11_BUFFER_DESC desc{};

	desc.ByteWidth = static_cast<UINT>(parsedData->bones.size()) * sizeof(matrix3x4_t);
	desc.StructureByteStride = sizeof(matrix3x4_t);

	// make sure this buffer can be updated every frame
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	// pixel is a const buffer
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

	HRESULT hr = device->CreateBuffer(&desc, NULL, &drawData->boneMatrixBuffer);

#if defined(ASSERTS)
	assert(!FAILED(hr));
#else
	if (FAILED(hr))
		return;
#endif

	// SRV
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(parsedData->bones.size());

	hr = device->CreateShaderResourceView(drawData->boneMatrixBuffer, &srvDesc, &drawData->boneMatrixSRV);

#if defined(ASSERTS)
	assert(!FAILED(hr));
#else
	if (FAILED(hr))
		return;
#endif

	UpdateModelBoneMatrix(drawData, parsedData);
}

void* PreviewParsedData(ModelPreviewInfo_t* const info, ModelParsedData_t* const parsedData, char* const assetName, const uint64_t assetGUID, const bool firstFrameForAsset)
{
	// [rika]: set up CDXDrawData
	g_currentPreviewDrawData.CheckForMonitorChange();

	if (assetGUID != g_currentPreviewDrawData.guid || g_currentPreviewDrawData.GetDrawData() == nullptr || info->selectedLODLevel != g_currentPreviewDrawData.activeLODLevel)
	{
		g_currentPreviewDrawData.FreeDrawData();

		CDXDrawData* const drawData = new CDXDrawData();
		drawData->meshBuffers.resize(parsedData->lods.at(info->selectedLODLevel).meshes.size());
		drawData->modelName = assetName;

		ParseModelDrawData(parsedData, drawData, info->selectedLODLevel);

		g_currentPreviewDrawData.UpdateAssetInfo(drawData, assetGUID, info->selectedLODLevel);
	}

	CDXDrawData* const drawData = g_currentPreviewDrawData.GetDrawData();
	if (!drawData)
		return nullptr;

	drawData->vertexShader = g_dxHandler->GetShaderManager()->LoadShaderFromString("shaders/model_vs", s_PreviewVertexShader, eShaderType::Vertex);;
	drawData->pixelShader = g_dxHandler->GetShaderManager()->LoadShaderFromString("shaders/model_ps", s_PreviewPixelShader, eShaderType::Pixel);

	// [rika]: do the preview stuff here!
	assertm(parsedData->lods.size() > 0, "no lods in preview?");
	const ModelLODData_t& lodData = parsedData->lods.at(info->selectedLODLevel);

	ImGui::Text("Bones: %llu", parsedData->bones.size());
	ImGui::Text("LODs: %llu", parsedData->lods.size());
	ImGui::Text("Local Sequences: %i", parsedData->NumLocalSeq());

	if (info->minLODIndex != info->maxLODIndex)
	{
		ImGui::SliderScalar("LOD Level", ImGuiDataType_U8, &info->selectedLODLevel, &info->minLODIndex, &info->maxLODIndex);
	}

	if (parsedData->skins.size())
	{
		ImGui::TextUnformatted("Skins:");
		ImGui::SameLine();

		// [rika]: cheat a little here since the first skin name should always be 'STUDIO_DEFAULT_SKIN_NAME'
		static const char* label = nullptr;
		if (firstFrameForAsset)
			label = STUDIO_DEFAULT_SKIN_NAME;

		if (ImGui::BeginCombo("##SKins", label, ImGuiComboFlags_NoArrowButton))
		{
			for (size_t i = 0; i < parsedData->skins.size(); i++)
			{
				const ModelSkinData_t& skin = parsedData->skins.at(i);

				const bool isSelected = info->selectedSkinIndex == i || (firstFrameForAsset && info->selectedSkinIndex == info->lastSelectedSkinIndex);

				if (ImGui::Selectable(skin.name, isSelected))
				{
					info->selectedSkinIndex = i;
					label = skin.name;
				}

				if (isSelected) ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}
	}

	g_ExportSettings.previewedSkinIndex = static_cast<int>(info->selectedSkinIndex);

	if (parsedData->bodyParts.size())
	{
		ImGui::TextUnformatted("Bodypart:");
		ImGui::SameLine();

		// [rika]: previous implemention was loading an invalid string upon loading different files without closing the tool
		static const char* bodypartLabel = nullptr;
		if (firstFrameForAsset)
			bodypartLabel = parsedData->bodyParts.at(0).GetNameCStr();

		if (ImGui::BeginCombo("##Bodypart", bodypartLabel, ImGuiComboFlags_NoArrowButton))
		{
			for (size_t i = 0; i < parsedData->bodyParts.size(); i++)
			{
				const ModelBodyPart_t& bodypart = parsedData->bodyParts.at(i);

				const bool isSelected = info->selectedBodypartIndex == i || (firstFrameForAsset && info->selectedBodypartIndex == info->lastSelectedBodypartIndex);

				if (ImGui::Selectable(bodypart.GetNameCStr(), isSelected))
				{
					info->selectedBodypartIndex = i;
					bodypartLabel = bodypart.GetNameCStr();
				}

				if (isSelected) ImGui::SetItemDefaultFocus();
			}

			ImGui::EndCombo();
		}

		if (parsedData->bodyParts.at(info->selectedBodypartIndex).numModels > 1)
		{
			const ModelBodyPart_t& bodypart = parsedData->bodyParts.at(info->selectedBodypartIndex);
			size_t& selectedModelIndex = info->bodygroupModelSelected.at(info->selectedBodypartIndex);

			ImGui::TextUnformatted("Model:");
			ImGui::SameLine();

			static const char* label = nullptr;

			// update label if our bodypart changes
			if (info->selectedBodypartIndex != info->lastSelectedBodypartIndex || firstFrameForAsset)
				label = lodData.models.at(bodypart.modelIndex + selectedModelIndex).name.c_str();

			if (ImGui::BeginCombo("##Model", label, ImGuiComboFlags_NoArrowButton))
			{
				for (int i = 0; i < bodypart.numModels; i++)
				{
					const bool isSelected = selectedModelIndex == i;

					const char* tmp = lodData.models.at(bodypart.modelIndex + i).name.c_str();
					if (ImGui::Selectable(tmp, isSelected))
					{
						selectedModelIndex = static_cast<size_t>(i);
						label = tmp;
					}

					if (isSelected) ImGui::SetItemDefaultFocus();
				}

				ImGui::EndCombo();
			}
		}
	}

	// [rika]: our currently selected skin
	const ModelSkinData_t& skinData = parsedData->skins.at(info->selectedSkinIndex);
	for (size_t i = 0; i < lodData.meshes.size(); ++i)
	{
		const ModelMeshData_t& mesh = lodData.meshes.at(i);
		DXMeshDrawData_t* const meshDrawData = &drawData->meshBuffers[i];

		meshDrawData->indexFormat = DXGI_FORMAT_R16_UINT;

		// If this body part is disabled, don't draw the mesh.
		drawData->meshBuffers[i].visible = parsedData->bodyParts[mesh.bodyPartIndex].IsPreviewEnabled();

		const ModelBodyPart_t& bodypart = parsedData->bodyParts[mesh.bodyPartIndex];
		const ModelModelData_t& model = lodData.models.at(bodypart.modelIndex + info->bodygroupModelSelected.at(mesh.bodyPartIndex));
		if (i >= model.meshIndex && i < model.meshIndex + model.meshCount)
			drawData->meshBuffers[i].visible = true;
		else
			drawData->meshBuffers[i].visible = false;

		// the rest of this loop requires the material to be valid
		// so if it isn't just continue to the next iteration
		CPakAsset* const matlAsset = parsedData->materials.at(skinData.indices[mesh.materialId]).asset;
		if (!matlAsset)
			continue;

		const MaterialAsset* const matl = reinterpret_cast<MaterialAsset*>(matlAsset->extraData());

#if defined(ADVANCED_MODEL_PREVIEW)
		if (matl->shaderSetAsset)
		{
			ShaderSetAsset* const shaderSet = reinterpret_cast<ShaderSetAsset*>(matl->shaderSetAsset->extraData());

			if (shaderSet->vertexShaderAsset && shaderSet->pixelShaderAsset)
			{
				ShaderAsset* const vertexShader = reinterpret_cast<ShaderAsset*>(shaderSet->vertexShaderAsset->extraData());
				ShaderAsset* const pixelShader = reinterpret_cast<ShaderAsset*>(shaderSet->pixelShaderAsset->extraData());

				meshDrawData->vertexShader = vertexShader->vertexShader;
				meshDrawData->pixelShader = pixelShader->pixelShader;

				meshDrawData->inputLayout = vertexShader->vertexInputLayout;
			}
		}
#endif

		if ((meshDrawData->textures.size() == 0 || info->lastSelectedSkinIndex != info->selectedSkinIndex) && matl)
		{
			meshDrawData->textures.clear();
			for (auto& texEntry : matl->txtrAssets)
			{
				if (texEntry.asset)
				{
					TextureAsset* txtr = reinterpret_cast<TextureAsset*>(texEntry.asset->extraData());
					const std::shared_ptr<CTexture> highestTextureMip = CreateTextureFromMip(texEntry.asset, &txtr->mipArray[txtr->mipArray.size() - 1], s_PakToDxgiFormat[txtr->imgFormat]);
					meshDrawData->textures.push_back({ texEntry.index, highestTextureMip });
				}
			}
		}
	}

	if (!drawData->boneMatrixBuffer)
		InitModelBoneMatrix(drawData, parsedData);

	if (!drawData->transformsBuffer)
	{
		D3D11_BUFFER_DESC desc{};

#if defined(ADVANCED_MODEL_PREVIEW)
		constexpr UINT transformsBufferSizeAligned = IALIGN(sizeof(CBufModelInstance), 16);
#else
		constexpr UINT transformsBufferSizeAligned = IALIGN(sizeof(VS_TransformConstants), 16);
#endif

		desc.ByteWidth = transformsBufferSizeAligned;

		// make sure this buffer can be updated every frame
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		// const buffer
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		g_dxHandler->GetDevice()->CreateBuffer(&desc, NULL, &drawData->transformsBuffer);
	}

	D3D11_MAPPED_SUBRESOURCE resource;
	g_dxHandler->GetDeviceContext()->Map(
		drawData->transformsBuffer, 0,
		D3D11_MAP_WRITE_DISCARD, 0,
		&resource
	);

	CDXCamera* const camera = g_dxHandler->GetCamera();
	const XMMATRIX view = camera->GetViewMatrix();
	const XMMATRIX model = XMMatrixTranslationFromVector(drawData->position.AsXMVector());
	const XMMATRIX projection = g_dxHandler->GetProjMatrix();

#if !defined(ADVANCED_MODEL_PREVIEW)
	VS_TransformConstants* const transforms = reinterpret_cast<VS_TransformConstants*>(resource.pData);
	transforms->modelMatrix = XMMatrixTranspose(model);
	transforms->viewMatrix = XMMatrixTranspose(view);
	transforms->projectionMatrix = XMMatrixTranspose(projection);
#else
	CBufModelInstance* const modelInstance = reinterpret_cast<CBufModelInstance*>(resource.pData);
	CBufModelInstance::ModelInstance& mi = modelInstance->c_modelInst;

	// sub_18001FB40 in r2
	XMMATRIX modelMatrix = {};
	memcpy(&modelMatrix, &modelAsset->bones[0].poseToBone, sizeof(ModelBone_t::poseToBone));

	modelMatrix.r[3].m128_f32[3] = 1.f;

	mi.objectToCameraRelativePrevFrame = mi.objectToCameraRelative;

	modelMatrix.r[0].m128_f32[3] += camera->position.x;
	modelMatrix.r[1].m128_f32[3] += camera->position.y;
	modelMatrix.r[2].m128_f32[3] += camera->position.z;

	modelMatrix -= XMMatrixRotationRollPitchYaw(camera->rotation.x, camera->rotation.y, camera->rotation.z);

	modelMatrix = XMMatrixTranspose(modelMatrix);

	mi.diffuseModulation = { 1.f, 1.f, 1.f, 1.f };

	XMStoreFloat3x4(&mi.objectToCameraRelative, modelMatrix);
	XMStoreFloat3x4(&mi.objectToCameraRelativePrevFrame, modelMatrix);

#endif

	if (info->lastSelectedBodypartIndex != info->selectedBodypartIndex)
		info->lastSelectedBodypartIndex = info->selectedBodypartIndex;

	if (info->lastSelectedSkinIndex != info->selectedSkinIndex)
		info->lastSelectedSkinIndex = info->selectedSkinIndex;

	g_dxHandler->GetDeviceContext()->Unmap(drawData->transformsBuffer, 0);

	return drawData;
}

void PreviewAnimDesc(const animdesc_t* const animdesc, const int index)
{
	if (ImGui::TreeNodeEx(std::to_string(index).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
	{
		ImGui::Text("Name: %s", animdesc->name);
		ImGui::Text("Flags: 0x%x", animdesc->flags);

		ImGui::Text("Frame Rate: %f", animdesc->fps);
		ImGui::Text("Frame Count: %i", animdesc->numframes);

		ImGui::TreePop();
	}
}

void PreviewSeqDesc(const seqdesc_t* const seqdesc)
{
	ImGui::Text("Label: %s", seqdesc->szlabel);
	ImGui::Text("Activity: %s", seqdesc->szactivityname);

	ImGui::Text("Flags: 0x%x", seqdesc->flags);

	if (!seqdesc->AnimCount())
		return;

	ImGui::TextUnformatted("Animations:");

	for (int i = 0; i < seqdesc->AnimCount(); i++)
	{
		PreviewAnimDesc(&seqdesc->anims.at(i), i);
	}
}