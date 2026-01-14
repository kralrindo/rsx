#include <pch.h>

#include <d3d11.h>
#include <game/rtech/utils/utils.h>

#include <core/render/dx.h>
#include <core/render/dxutils.h>

extern CDXParentHandler* g_dxHandler;

bool CreateD3DBuffer(
    ID3D11Device* device, ID3D11Buffer** pBuffer,
    UINT byteWidth, D3D11_USAGE usage,
    D3D11_BIND_FLAG bindFlags, int cpuAccessFlags,
    UINT miscFlags, UINT structureByteStride, void* initialData)
{
    assert(pBuffer);

    if (!device)
        return false;

    D3D11_BUFFER_DESC desc = {};

    desc.Usage = usage;
    desc.ByteWidth = bindFlags & D3D11_BIND_CONSTANT_BUFFER ? IALIGN(byteWidth, 16) : byteWidth;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = cpuAccessFlags;
    desc.MiscFlags = miscFlags;
    desc.StructureByteStride = structureByteStride;

    HRESULT hr;
    if (initialData)
    {
        D3D11_SUBRESOURCE_DATA resource = { initialData };
        hr = device->CreateBuffer(&desc, &resource, pBuffer);
    }
    else
    {
        hr = device->CreateBuffer(&desc, NULL, pBuffer);
    }

    assert(SUCCEEDED(hr));

    return SUCCEEDED(hr);
}

CPreviewDrawData::~CPreviewDrawData()
{
    FreeDrawData();
}

inline void CPreviewDrawData::FreeDrawData()
{
    FreeAllocVar(drawData);
    drawData = nullptr;
}

// check if the monitor changed, and update accordingly
bool CPreviewDrawData::CheckForMonitorChange()
{
    const uint32_t dxHandlerActiveMonitor = g_dxHandler->GetActiveMonitor();
    if (activeMonitor == dxHandlerActiveMonitor)
        return false;

    if (g_dxHandler->MonitorHasSameAdapter(dxHandlerActiveMonitor, activeMonitor))
    {
        activeMonitor = dxHandlerActiveMonitor;
        return false;
    }

    activeMonitor = dxHandlerActiveMonitor;
    FreeDrawData();

    return true;
}