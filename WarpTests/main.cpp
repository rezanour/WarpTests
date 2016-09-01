//==============================================================================
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <stdint.h>
#include <assert.h>
#include <wincodec.h>

#include "SceneVS.h"
#include "ScenePS.h"
#include "RotationalWarpVS.h"
#include "RotationalWarpPS.h"

#include <DirectXMath.h>
using namespace DirectX;

#include <wrl.h>
using namespace Microsoft::WRL;

//==============================================================================
// Constants
//==============================================================================
static const uint32_t NumVertsWidth = 65;
static const uint32_t NumVertsHeight = 65;

//==============================================================================
// Structures
//==============================================================================
struct SceneVertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Color;
};

struct SceneVSConstants
{
    XMFLOAT4X4 WorldViewProj;
};

struct RotationWarpVertex
{
    XMFLOAT2 TexCoord;
};

struct RotationWarpVSConstants
{
    XMFLOAT4X4 InvTWMatrix;
};

struct PipelineState
{
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;
    ComPtr<ID3D11InputLayout> InputLayout;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11Buffer> VSConstantBuffer;
    ComPtr<ID3D11Buffer> PSConstantBuffer;
    uint32_t Stride;
    uint32_t Offset;
    uint32_t NumIndices;
};

enum class PipelineStateIndex
{
    SceneRender,
    RotationalTimewarp,
    Count
};

//==============================================================================
// Global variables
//==============================================================================
static ComPtr<ID3D11Device> Device;
static ComPtr<ID3D11DeviceContext> Context;
static ComPtr<IDXGISwapChain> SwapChain;
static ComPtr<ID3D11RenderTargetView> BackBufferRTV;
static ComPtr<ID3D11RenderTargetView> AppFrameRTV;
static ComPtr<ID3D11ShaderResourceView> AppFrameSRV;
static ComPtr<ID3D11SamplerState> Sampler;
static PipelineState Pipelines[(uint32_t)PipelineStateIndex::Count];
static float RotationX = 0.f;
static float RotationY = 0.f;

static XMFLOAT4X4 ViewProj;

//==============================================================================
// Functions
//==============================================================================
static HWND WindowInit(HINSTANCE instance, const wchar_t* class_name, uint32_t width, uint32_t height);
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool GraphicsInit(HWND hwnd);
static void GraphicsDestroy();

static bool GraphicsCreateScene();
static bool GraphicsCreateRotationalTimewarp();

static bool GraphicsLoadImage(const wchar_t* filename, ID3D11ShaderResourceView** srv);

static void GraphicsDoFrame();

static void GraphicsDrawPipeline(const PipelineState& pipeline);

static inline PipelineState& GetPipeline(PipelineStateIndex index)
{
    return Pipelines[(uint32_t)index];
}

//==============================================================================
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    // Needed for WIC (to load images)
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr))
    {
        assert(false);
        return -1;
    }

    HWND window = WindowInit(instance, L"WarpTests", 1280, 720);
    if (!window)
    {
        CoUninitialize();
        return -2;
    }

    if (!GraphicsInit(window))
    {
        DestroyWindow(window);
        CoUninitialize();
        return -3;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            GraphicsDoFrame();
        }
    }

    GraphicsDestroy();
    DestroyWindow(window);

    CoUninitialize();
    return 0;
}

//==============================================================================
HWND WindowInit(HINSTANCE instance, const wchar_t* class_name, uint32_t width, uint32_t height)
{
    WNDCLASSEX wcx{};
    wcx.cbSize = sizeof(wcx);
    wcx.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcx.hInstance = instance;
    wcx.lpfnWndProc = WindowProc;
    wcx.lpszClassName = class_name;
    if (RegisterClassEx(&wcx) == INVALID_ATOM)
    {
        assert(false);
        return nullptr;
    }

    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);

    RECT rc{};
    rc.right = width;
    rc.bottom = height;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindow(wcx.lpszClassName, wcx.lpszClassName, style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);
    if (!hwnd)
    {
        assert(false);
        return nullptr;
    }

    return hwnd;
}

//==============================================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
        }
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

//==============================================================================
bool GraphicsInit(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = rc.right - rc.left;
    scd.BufferDesc.Height = rc.bottom - rc.top;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT flags = 0;

#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, &featureLevel, 1, D3D11_SDK_VERSION, &scd, &SwapChain, &Device, nullptr, &Context);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    hr = SwapChain->GetBuffer(0, IID_PPV_ARGS(&texture));
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    hr = Device->CreateRenderTargetView(texture.Get(), nullptr, &BackBufferRTV);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }
    Context->OMSetRenderTargets(1, BackBufferRTV.GetAddressOf(), nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width = (float)scd.BufferDesc.Width;
    vp.Height = (float)scd.BufferDesc.Height;
    vp.MaxDepth = 1.f;
    Context->RSSetViewports(1, &vp);

    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    hr = Device->CreateTexture2D(&td, nullptr, texture.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    hr = Device->CreateRenderTargetView(texture.Get(), nullptr, &AppFrameRTV);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    hr = Device->CreateShaderResourceView(texture.Get(), nullptr, &AppFrameSRV);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = Device->CreateSamplerState(&sd, &Sampler);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }
    Context->PSSetSamplers(0, 1, Sampler.GetAddressOf());

    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (!GraphicsCreateScene())
    {
        assert(false);
        return false;
    }

    if (!GraphicsCreateRotationalTimewarp())
    {
        assert(false);
        return false;
    }

    return true;
}

//==============================================================================
void GraphicsDestroy()
{
    for (uint32_t i = 0; i < _countof(Pipelines); ++i)
    {
        Pipelines[i].InputLayout = nullptr;
        Pipelines[i].PixelShader = nullptr;
        Pipelines[i].PSConstantBuffer = nullptr;
        Pipelines[i].VertexShader = nullptr;
        Pipelines[i].VSConstantBuffer = nullptr;
        Pipelines[i].IndexBuffer = nullptr;
        Pipelines[i].VertexBuffer = nullptr;
    }

    AppFrameSRV = nullptr;
    AppFrameRTV = nullptr;
    BackBufferRTV = nullptr;
    SwapChain = nullptr;
    Context = nullptr;
    Device = nullptr;
}

//==============================================================================
bool GraphicsCreateScene()
{
    auto& pipeline = GetPipeline(PipelineStateIndex::SceneRender);

    SceneVertex vertices[] = {
        { { -1.f, -1.f, -1.f },{ 1.f, 0.f, 0.f } },
        { { -1.f, 1.f, -1.f },{ 0.f, 1.f, 0.f } },
        { { 1.f, 1.f, -1.f },{ 0.f, 0.f, 1.f } },
        { { 1.f, -1.f, -1.f },{ 0.f, 1.f, 1.f } },
        { { 1.f, -1.f, 1.f },{ 1.f, 0.f, 0.f } },
        { { 1.f, 1.f, 1.f },{ 0.f, 1.f, 0.f } },
        { { -1.f, 1.f, 1.f },{ 0.f, 0.f, 1.f } },
        { { -1.f, -1.f, 1.f },{ 1.f, 1.f, 0.f } },
    };

    uint32_t indices[] = {
        0, 1, 2, 0, 2, 3, // front
        4, 5, 6, 4, 6, 7, // back
        7, 6, 1, 7, 1, 0, // left
        3, 2, 5, 3, 5, 4, // right
        1, 6, 5, 1, 5, 2, // top
        7, 0, 3, 7, 3, 4, // bottom
    };

    D3D11_BUFFER_DESC bd{};
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(vertices);
    bd.StructureByteStride = sizeof(SceneVertex);

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = vertices;
    init.SysMemPitch = bd.ByteWidth;
    init.SysMemSlicePitch = init.SysMemPitch;

    HRESULT hr = Device->CreateBuffer(&bd, &init, &pipeline.VertexBuffer);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    pipeline.Stride = bd.StructureByteStride;
    pipeline.Offset = 0;

    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = sizeof(indices);
    bd.StructureByteStride = sizeof(uint32_t);

    init.pSysMem = indices;
    init.SysMemPitch = bd.ByteWidth;
    init.SysMemSlicePitch = init.SysMemPitch;

    hr = Device->CreateBuffer(&bd, &init, &pipeline.IndexBuffer);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    pipeline.NumIndices = _countof(indices);

    hr = Device->CreateVertexShader(SceneVS, sizeof(SceneVS), nullptr, &pipeline.VertexShader);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    hr = Device->CreatePixelShader(ScenePS, sizeof(ScenePS), nullptr, &pipeline.PixelShader);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC elems[2]{};
    elems[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[0].SemanticName = "POSITION";
    elems[1].AlignedByteOffset = sizeof(XMFLOAT3);
    elems[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[1].SemanticName = "COLOR";

    hr = Device->CreateInputLayout(elems, _countof(elems), SceneVS, sizeof(SceneVS), &pipeline.InputLayout);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(SceneVSConstants);
    bd.StructureByteStride = bd.ByteWidth;
    hr = Device->CreateBuffer(&bd, nullptr, &pipeline.VSConstantBuffer);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    return true;
}

//==============================================================================
bool GraphicsCreateRotationalTimewarp()
{
    auto& pipeline = GetPipeline(PipelineStateIndex::RotationalTimewarp);

    RotationWarpVertex vertices[NumVertsWidth * NumVertsHeight]{};

    for (int y = 0; y < NumVertsHeight; ++y)
    {
        for (int x = 0; x < NumVertsWidth; ++x)
        {
            vertices[y * NumVertsWidth + x].TexCoord =
                XMFLOAT2(x / (float)(NumVertsWidth - 1), y / (float)(NumVertsHeight - 1));
        }
    }

    uint32_t indices[(NumVertsWidth - 1) * (NumVertsWidth - 1) * 6]{};
    for (int y = 0; y < NumVertsHeight - 1; ++y)
    {
        for (int x = 0; x < NumVertsWidth - 1; ++x)
        {
            indices[(y * (NumVertsWidth - 1) + x) * 6 + 0] = y * NumVertsWidth + x;
            indices[(y * (NumVertsWidth - 1) + x) * 6 + 1] = y * NumVertsWidth + x + 1;
            indices[(y * (NumVertsWidth - 1) + x) * 6 + 2] = (y + 1) * NumVertsWidth + x;
            indices[(y * (NumVertsWidth - 1) + x) * 6 + 3] = (y + 1) * NumVertsWidth + x;
            indices[(y * (NumVertsWidth - 1) + x) * 6 + 4] = y * NumVertsWidth + x + 1;
            indices[(y * (NumVertsWidth - 1) + x) * 6 + 5] = (y + 1) * NumVertsWidth + x + 1;
        }
    }

    D3D11_BUFFER_DESC bd{};
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(vertices);
    bd.StructureByteStride = sizeof(RotationWarpVertex);

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = vertices;
    init.SysMemPitch = bd.ByteWidth;
    init.SysMemSlicePitch = init.SysMemPitch;

    HRESULT hr = Device->CreateBuffer(&bd, &init, &pipeline.VertexBuffer);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    pipeline.Stride = bd.StructureByteStride;
    pipeline.Offset = 0;

    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = sizeof(indices);
    bd.StructureByteStride = sizeof(uint32_t);

    init.pSysMem = indices;
    init.SysMemPitch = bd.ByteWidth;
    init.SysMemSlicePitch = init.SysMemPitch;

    hr = Device->CreateBuffer(&bd, &init, &pipeline.IndexBuffer);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    pipeline.NumIndices = _countof(indices);

    hr = Device->CreateVertexShader(RotationalWarpVS, sizeof(RotationalWarpVS), nullptr, &pipeline.VertexShader);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    hr = Device->CreatePixelShader(RotationalWarpPS, sizeof(RotationalWarpPS), nullptr, &pipeline.PixelShader);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC elems[1]{};
    elems[0].Format = DXGI_FORMAT_R32G32_FLOAT;
    elems[0].SemanticName = "TEXCOORD";
    hr = Device->CreateInputLayout(elems, _countof(elems), RotationalWarpVS, sizeof(RotationalWarpVS), &pipeline.InputLayout);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(RotationWarpVSConstants);
    bd.StructureByteStride = bd.ByteWidth;
    hr = Device->CreateBuffer(&bd, nullptr, &pipeline.VSConstantBuffer);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    return true;
}

//==============================================================================
bool GraphicsLoadImage(const wchar_t* filename, ID3D11ShaderResourceView** srv)
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeOptions::WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherType::WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteType::WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    uint32_t width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.ArraySize = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;

    uint32_t* pixels = new uint32_t[width * height];
    hr = converter->CopyPixels(nullptr, sizeof(uint32_t) * width, width * height * sizeof(uint32_t), (BYTE*)pixels);
    if (FAILED(hr))
    {
        assert(false);
        delete[] pixels;
        return false;
    }

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels;
    init.SysMemPitch = sizeof(uint32_t) * width;
    init.SysMemSlicePitch = init.SysMemPitch * height;

    ComPtr<ID3D11Texture2D> texture;
    hr = Device->CreateTexture2D(&td, &init, &texture);
    if (FAILED(hr))
    {
        assert(false);
        delete[] pixels;
        return false;
    }

    delete[] pixels;

    hr = Device->CreateShaderResourceView(texture.Get(), nullptr, srv);
    if (FAILED(hr))
    {
        assert(false);
        return false;
    }

    return true;
}

//==============================================================================
void GraphicsDrawPipeline(const PipelineState& pipeline)
{
    Context->IASetVertexBuffers(0, 1, pipeline.VertexBuffer.GetAddressOf(), &pipeline.Stride, &pipeline.Offset);
    Context->IASetIndexBuffer(pipeline.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    Context->IASetInputLayout(pipeline.InputLayout.Get());
    Context->VSSetShader(pipeline.VertexShader.Get(), nullptr, 0);
    Context->VSSetConstantBuffers(0, 1, pipeline.VSConstantBuffer.GetAddressOf());
    Context->PSSetShader(pipeline.PixelShader.Get(), nullptr, 0);
    Context->PSSetConstantBuffers(0, 1, pipeline.PSConstantBuffer.GetAddressOf());
    Context->DrawIndexed(pipeline.NumIndices, 0, 0);
}

//#define DRAW_NATIVE

//==============================================================================
void GraphicsDoFrame()
{
    static const float clearColor[] = { 0.f, 0.f, 0.f, 1 };
    Context->ClearRenderTargetView(AppFrameRTV.Get(), clearColor);
    Context->ClearRenderTargetView(BackBufferRTV.Get(), clearColor);

    // Update rotational warp params
    static POINT lastMouse{};
    POINT newMouse{};

    GetCursorPos(&newMouse);
    if (lastMouse.x != 0 || lastMouse.y != 0)
    {
        RotationX += (newMouse.x - lastMouse.x) * 0.001f;
        RotationY += (newMouse.y - lastMouse.y) * 0.001f;
    }
    lastMouse = newMouse;

    XMMATRIX rot = XMMatrixMultiply(XMMatrixRotationY(RotationX), XMMatrixRotationX(RotationY));
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), 1280.f / 720.f, 0.1f, 1000.f);

#ifdef DRAW_NATIVE
    XMMATRIX view = XMMatrixLookToLH(XMVectorSet(0, 1, -8, 1), XMVector3Transform(XMVectorSet(0, 0, 1, 0), rot), XMVectorSet(0, 1, 0, 0));

#else // Draw warped

    XMMATRIX view = XMMatrixLookToLH(XMVectorSet(0, 1, -8, 1), XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 1, 0, 0));
    XMMATRIX view2 = XMMatrixLookToLH(XMVectorSet(0, 1, -8, 1), XMVector3Transform(XMVectorSet(0, 0, 1, 0), rot), XMVectorSet(0, 1, 0, 0));

    XMVECTOR det;
    XMMATRIX warp = XMMatrixMultiply(XMMatrixInverse(&det, view), view2);
#endif

    // Draw scene
    auto& scenePipeline = GetPipeline(PipelineStateIndex::SceneRender);

    SceneVSConstants sceneVSConst{};
    XMStoreFloat4x4(&sceneVSConst.WorldViewProj, XMMatrixMultiply(view, proj));
    Context->UpdateSubresource(scenePipeline.VSConstantBuffer.Get(), 0, nullptr, &sceneVSConst, sizeof(sceneVSConst), 0);
    
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    Context->PSSetShaderResources(0, _countof(nullSRV), nullSRV);
    Context->OMSetRenderTargets(1, AppFrameRTV.GetAddressOf(), nullptr);
    GraphicsDrawPipeline(scenePipeline);

    // Rotational warp
    auto& rotationalPipeline = GetPipeline(PipelineStateIndex::RotationalTimewarp);
    
    RotationWarpVSConstants rotationVSConst{};
#ifdef DRAW_NATIVE
    XMStoreFloat4x4(&rotationVSConst.InvTWMatrix, XMMatrixIdentity());
#else // draw warped
    XMStoreFloat4x4(&rotationVSConst.InvTWMatrix, XMMatrixInverse(&det, warp));
#endif

    Context->UpdateSubresource(rotationalPipeline.VSConstantBuffer.Get(), 0, nullptr, &rotationVSConst, sizeof(rotationVSConst), 0);

    Context->OMSetRenderTargets(1, BackBufferRTV.GetAddressOf(), nullptr);
    Context->PSSetShaderResources(0, 1, AppFrameSRV.GetAddressOf());
    GraphicsDrawPipeline(rotationalPipeline);

    SwapChain->Present(1, 0);
}
