#include <pch.h>
#include <core/filehandling/validation.h>
#include <game/asset.h>
#include <game/rtech/cpakfile.h>

struct PakAssetValidationValues_t
{
	// Version -> Vector of known header sizes
	std::map<int, std::vector<int>> knownVersions;
	
	bool expectStarpakOffset : 1;
	bool requireStarpakOffset : 1;

	bool expectOptStarpakOffset : 1;
	bool requireOptStarpakOffset : 1;

	bool expectDataPage : 1;
	bool requireDataPage : 1;
};

bool ValidateLoadedPakFiles()
{
	printf("\nVALIDATION: Checking %lld assets across %lld files\n", g_assetData.GetNumAssets(), g_assetData.GetNumContainers());

	uint32_t numSegmentErrors = 0;
	uint32_t numAssetErrors = 0;

	for (CAssetContainer* container : g_assetData.v_assetContainers)
	{
		if (container->GetContainerType() != CAssetContainer::ContainerType::PAK)
			continue;

		CPakFile* pakFile = reinterpret_cast<CPakFile*>(container);

		const std::string stemString = container->GetFilePath().stem().string();
		
		printf("\tChecking container file: %s\n", stemString.c_str());
	
		if (pakFile->segmentPaddingTooBig)
			printf("\t\tError: segment padding is too big\n");
		if (pakFile->segmentPaddingTooSmall)
			printf("\t\tError: segment padding is too small\n");
		
		numSegmentErrors += pakFile->segmentPaddingTooBig + pakFile->segmentPaddingTooSmall;

		for (auto& [type, loadedInfo] : pakFile->GetLoadedAssetTypeInfo())
		{
			const std::string assetTypeFourCC = fourCCToString(type);
			if (loadedInfo.inconsistentHeaderSize)
				printf("\t\tError: asset type '%s' has inconsistent header sizes\n", assetTypeFourCC.c_str());
			if (loadedInfo.inconsistentVersions)
				printf("\t\tError: asset type '%s' has inconsistent asset versions\n", assetTypeFourCC.c_str());
		
			numAssetErrors += loadedInfo.inconsistentHeaderSize + loadedInfo.inconsistentVersions;
		}
	}

	const uint32_t numContainerErrors = g_assetData.m_numFailedContainerLoads + numSegmentErrors + numAssetErrors;
	printf("\tFound %i problems:\n\t%u segment padding errors, %u asset data errors, %u files failed to load\n", numContainerErrors, numSegmentErrors, numAssetErrors, g_assetData.m_numFailedContainerLoads);

	return true;
}