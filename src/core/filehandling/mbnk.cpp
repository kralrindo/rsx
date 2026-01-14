#include "pch.h"

#include <thirdparty/imgui/misc/imgui_utility.h>

#include <core/filehandling/load.h>
#include <core/filehandling/export.h>

#include <game/audio/miles.h>

void HandleMBNKLoad(std::vector<std::string> filePaths)
{
	std::atomic<uint32_t> bankLoadingProgress = 0;
	const ProgressBarEvent_t* const bankLoadProgressBar = g_pImGuiHandler->AddProgressBarEvent("Loading Audio Banks..", static_cast<uint32_t>(filePaths.size()), &bankLoadingProgress, true);

	Log("MBNK: Started loading %lld files\n", filePaths.size());

	for (const std::string& path : filePaths)
	{
		if (CMilesAudioBank* const bank = new CMilesAudioBank(); bank->ParseFile(path))
		{
			g_assetData.v_assetContainers.emplace_back(bank);
		}
		else
		{
			Log("MBNK: Bank '%s' failed to load\n", path.c_str());
			delete bank;
		}

		++bankLoadingProgress;
	}

	g_pImGuiHandler->FinishProgressBarEvent(bankLoadProgressBar);
}