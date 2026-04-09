#include <pch.h>
#include <core/render/dx.h>
#include <imgui.h>
#include <game/rtech/utils/utils.h>
#include <misc/imgui_utility.h>
#include <core/render/preview/preview.h>

void Preview_Texture(CDXDrawData* drawData, float dt)
{
    UNUSED(dt);
    static float textureZoom = 1.0f;

    const float aspectRatio = !drawData->previewTexture ? 1.f : static_cast<float>(drawData->previewTexture->GetWidth()) / drawData->previewTexture->GetHeight();
    float imageHeight = !drawData->previewTexture ? 0.f : std::max(std::clamp(static_cast<float>(drawData->previewTexture->GetHeight()), 0.f, std::max(ImGui::GetContentRegionAvail().y, 1.f)) - 2.5f, 4.f);
    float imageWidth = imageHeight * aspectRatio;

    const float originalHeight = !drawData->previewTexture ? 0.f : drawData->previewTexture->GetHeight();
    const float originalWidth = !drawData->previewTexture ? 0.f : drawData->previewTexture->GetWidth();

    imageWidth *= textureZoom;
    imageHeight *= textureZoom;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);

    ImGui::Text("Scale: %.f%%", textureZoom * 100.f);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 0.1f));
    ImGui::Text(" | ");
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::Text("%.f x %.f", originalWidth, originalHeight);

    ImGui::SameLine();
    ImGui::NextColumn();

    constexpr const char* const zoomHelpText = "Hold CTRL and scroll to zoom";
    IMGUI_RIGHT_ALIGN_FOR_TEXT(zoomHelpText);
    ImGui::TextUnformatted(zoomHelpText);
    if (ImGui::BeginChild("Texture Preview", ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar)) // [rika]: todo smaller screens will not have the most ideal viewing experience do to the image being squashed
    {
        const bool previewHovering = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (!drawData->previewTexture)
        {
            const ImVec2 avail_size = ImGui::GetContentRegionAvail();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.y += ImCeil(ImMax(0.0f, avail_size.y * 0.5f));
            ImGui::SetCursorScreenPos(pos);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.48f, 0.48f, 1.f));
            ImGuiExt::TextCentered("Failed to preview this texture.");
            ImGuiExt::TextCentered("If this is a streamed texture, please check that all required .STARPAK files are present");
            ImGui::PopStyleColor();
        }
        else
        {
            // Ensure that the preview image is always centered in the screen (while still allowing scrolling)
            // This code is slightly duplicated above, but oh well
            const ImVec2 avail_size = ImGui::GetContentRegionAvail();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.x += ImCeil(ImMax(0.0f, (avail_size.x - imageWidth) * 0.5f));
            pos.y += ImCeil(ImMax(0.0f, (avail_size.y - imageHeight) * 0.5f));
            ImGui::SetCursorScreenPos(pos);

            ImGui::Image(drawData->previewTexture->GetSRV(), ImVec2(imageWidth, imageHeight));
            if (previewHovering && ImGui::GetIO().KeyCtrl)
            {
                const float wheel = ImGui::GetIO().MouseWheel;
                const float scrollZoomFactor = ImGui::GetIO().KeyAlt ? (1.f / 20.f) : (1.f / 10.f);

                if (wheel != 0.0f)
                    textureZoom += (wheel * scrollZoomFactor);

                textureZoom = std::clamp(textureZoom, 0.1f, 5.0f);
            }

            static bool resetPos = true;
            static ImVec2 posPrev;
            if (previewHovering && ImGui::GetIO().MouseDown[2] && !ImGui::GetIO().KeyCtrl) // middle mouse
            {
                ImVec2 posCur = ImGui::GetIO().MousePos;

                if (resetPos)
                    posPrev = posCur;

                ImVec2 delta(posCur.x - posPrev.x, posCur.y - posPrev.y);
                ImVec2 scroll(0.0f, 0.0f);

                scroll.x = std::clamp(ImGui::GetScrollX() + delta.x, 0.0f, ImGui::GetScrollMaxX());
                scroll.y = std::clamp(ImGui::GetScrollY() + delta.y, 0.0f, ImGui::GetScrollMaxY());

                ImGui::SetScrollX(scroll.x);
                ImGui::SetScrollY(scroll.y);

                posPrev = posCur;
                resetPos = false;
            }
            else
                resetPos = true;
        }

    }
    ImGui::EndChild();
}