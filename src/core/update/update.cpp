#include "pch.h"
#include "update.h"
#include <core/utils/jsonutils.h>
#include <core/version.h>

#if !defined(NO_LIBCURL)
#include <curl/curl.h>
#include <core/utils/curlutils.h>
#endif


bool SendRequest(const std::string& finalUrl, const char* request,
    std::string& outResponse, std::string& outMessage, CURLINFO& outStatus, const bool forceIPv4)
{
    CURLParams params;

    params.writeFunction = CURLWriteStringCallback;
    params.timeout = 2;
    params.verifyPeer = false;
    params.verbose = false;
    params.forceIPv4 = forceIPv4;

    curl_slist* sList = nullptr;
    CURL* curl = CURLInitRequest(finalUrl.c_str(), request, outResponse, sList, params);
    if (!curl)
    {
        return false;
    }

    CURLcode res = CURLSubmitRequest(curl, sList);
    if (!CURLHandleError(curl, res, outMessage, true))
    {
        return false;
    }

    outStatus = CURLRetrieveInfo(curl);

    return true;
}

const char* GitHubReleaseInfo_s::GetHighestDifferingVersionNumber() const
{
    constexpr int ourVersionParts[] = {
        VERSION_MAJOR,
        VERSION_MINOR,
        VERSION_PATCH,
        STRtoCHAR(VERSION_REVIS),
    };

    constexpr const char* versionPartNames[] = {
        "major",
        "minor",
        "patch",
        "bugfix"
    };

    int tagPartIdx = 0;

    std::stringstream tagStringStream(tagName);

    std::string segment;
    while (std::getline(tagStringStream, segment, '.'))
    {
        const int num = atoi(segment.c_str());

        if (num > ourVersionParts[tagPartIdx])
            return versionPartNames[tagPartIdx];

        // If we are processing the final segment of the version number, it may also contain a revision letter
        // Check if the letter is present in the tag and if it is compare it with ours.
        if (tagPartIdx == 2)
        {
            const char lastChar = segment.back();
            if (lastChar >= 'a' && lastChar <= 'z')
                return lastChar > ourVersionParts[3] ? versionPartNames[3] : nullptr; // ourVersionParts[3] may be '\0'. This comparison is still valid.
        }

        tagPartIdx++;
    }
    return nullptr;
}

// Based on r5sdk's CPylon::SendRequest
// parameter is untouched unless the update is fetched successfully
bool GetLatestGitHubReleaseInformation(GitHubReleaseInfo_s* update)
{
    rapidjson::Document responseJson;

    std::string response;
    std::string message;
    CURLINFO status;

    if (SendRequest(GITHUB_RELEASES_API_URL, nullptr, response, message, status, false))
    {
        assert(status == 200);

        responseJson.Parse(response.c_str(), response.length());

        if (responseJson.HasParseError()) [[unlikely]]
        {
            Log("%s Error: JSON parse error at position %zu: %s\n", __FUNCTION__,
                responseJson.GetErrorOffset(), rapidjson::GetParseError_En(responseJson.GetParseError()));

            return false;
        }

        if (!responseJson.IsArray()) [[unlikely]]
        {
            Log("%s Error: JSON root was not an array\n", __FUNCTION__);
            return false;
        }

        const rapidjson::GenericArray releasesArray = responseJson.GetArray();

        if (releasesArray.Size() <= 0)
        {
            Log("%s Error: JSON root was an array of invalid size\n", __FUNCTION__);
            return false;
        }

        if (update)
        {
            for (rapidjson::Value& release : releasesArray)
            {
                bool isPreRelease = false;
                if (!JSON_GetValue(release, "prerelease", isPreRelease))
                    return false;

                // Prereleases are not recommended for most users, so let's not prompt the user with them
                if (isPreRelease)
                    continue;

                if (!JSON_GetValue(release, "tag_name", update->tagName))
                    return false;

                if (!JSON_GetValue(release, "body", update->description))
                    return false;

                if (!JSON_GetValue(release, "html_url", update->htmlUrl))
                    return false;

                break;
            }
        }

        return true;
    }

	return false;
}