#pragma once

#include <core/render/dxshader.h>

struct GridVertex_t
{
	float x, y, z;
	uint32_t color;

	constexpr GridVertex_t() {};
	constexpr GridVertex_t(float x, float y, float z, uint32_t color) : x(x), y(y), z(z), color(color) {};
};
class CShader;

template<int N, int SZ=1>
struct PreviewGrid_t
{
	constexpr PreviewGrid_t() : numVertices(4*(N+1))
	{
		constexpr float minCoord = (-(N / 2)) * SZ;
		constexpr float maxCoord = (N / 2) * SZ;
		constexpr uint16_t horizontalVertsStart = (2 * (N + 1));

		uint16_t numVertsWritten = 0;
		for (float i = minCoord; i <= maxCoord; i += SZ)
		{
			constexpr uint32_t lineColour = 0x919191FF;
			vertices[numVertsWritten] = { i, 0, maxCoord, lineColour };
			vertices[numVertsWritten+1] = { i, 0, minCoord, lineColour };

			vertices[numVertsWritten + horizontalVertsStart] = { minCoord, 0, i, lineColour };
			vertices[numVertsWritten + horizontalVertsStart + 1] = { maxCoord, 0, i, lineColour };

			numVertsWritten += 2;
		}
	};

	GridVertex_t vertices[4*(N+1)];

	UINT numVertices;
	UINT vertexStride;

	ID3D11Buffer* vertexBuffer;

	CShader* vertexShader;
	CShader* pixelShader;

	void CreateBuffers(ID3D11Device* device)
	{
		if (!vertexBuffer)
		{
			constexpr UINT vertStride = sizeof(GridVertex_t);

			D3D11_BUFFER_DESC desc = {};

			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.ByteWidth = static_cast<UINT>(vertStride * numVertices);
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;

			D3D11_SUBRESOURCE_DATA srd{ vertices };

			if (FAILED(device->CreateBuffer(&desc, &srd, &vertexBuffer)))
				return;

			vertexStride = vertStride;
		}
	}

	void Draw(ID3D11DeviceContext* ctx)
	{
		UINT offset = 0u;

		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		ctx->IASetVertexBuffers(0u, 1u, &vertexBuffer, &vertexStride, &offset);

		ctx->IASetInputLayout(vertexShader->GetInputLayout());
		ctx->VSSetShader(vertexShader->Get<ID3D11VertexShader>(), nullptr, 0u);
		ctx->PSSetShader(pixelShader->Get<ID3D11PixelShader>(), nullptr, 0u);

		ctx->Draw(numVertices, 0);
	}
};