// arrayplot_viewer: standalone Dear ImGui + ImPlot app. Runs a named-pipe
// server (\\.\pipe\arrayplot) in a background thread and renders whatever
// arrays/matrices arrive, keyed by name, live.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "implot.h"

#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol.h"

using namespace aplot;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------- D3D device globals ----------------
static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static bool                    g_SwapChainOccluded = false;
static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------- plot data store ----------------
struct PlotEntry {
    uint32_t rows = 0, cols = 0;
    std::vector<double> data; // always normalized to row-major
};

class PlotStore {
public:
    void Update(const std::string& name, uint32_t rows, uint32_t cols, std::vector<double> rowMajorData) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[name] = PlotEntry{rows, cols, std::move(rowMajorData)};
    }

    std::map<std::string, PlotEntry> Snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }

private:
    std::mutex mutex_;
    std::map<std::string, PlotEntry> entries_;
};

static PlotStore g_store;
static std::atomic<bool> g_running{true};
static std::atomic<int> g_connectionCount{0};

// Single-hue sequential colormap (dark -> light blue) so heatmap intensity
// reads as shade, not hue -- registered once, after ImPlot::CreateContext().
static ImPlotColormap g_monoColormap = -1;

static void RegisterMonoColormap() {
    static const ImVec4 monoColors[] = {
        ImVec4(0.03f, 0.19f, 0.42f, 1.0f),
        ImVec4(0.13f, 0.44f, 0.71f, 1.0f),
        ImVec4(0.42f, 0.68f, 0.84f, 1.0f),
        ImVec4(0.78f, 0.86f, 0.94f, 1.0f),
        ImVec4(0.97f, 0.98f, 1.00f, 1.0f),
    };
    g_monoColormap = ImPlot::AddColormap("arrayplot_mono", monoColors, IM_ARRAYSIZE(monoColors), false);
}

// ---------------- pipe server ----------------
static bool ReadExact(HANDLE pipe, void* buf, size_t size) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        DWORD readBytes = 0;
        if (!ReadFile(pipe, p, static_cast<DWORD>(remaining), &readBytes, nullptr) || readBytes == 0)
            return false;
        p += readBytes;
        remaining -= readBytes;
    }
    return true;
}

static std::vector<double> ConvertAndNormalize(const MessageHeader& header, const std::vector<uint8_t>& raw) {
    std::vector<double> values;
    if (header.dtype == DType::Float64) {
        size_t n = raw.size() / sizeof(double);
        values.resize(n);
        std::memcpy(values.data(), raw.data(), n * sizeof(double));
    } else {
        size_t n = raw.size() / sizeof(float);
        values.resize(n);
        const float* f = reinterpret_cast<const float*>(raw.data());
        for (size_t i = 0; i < n; ++i) values[i] = static_cast<double>(f[i]);
    }

    // Eigen defaults to column-major; normalize to row-major so the render
    // code below never has to think about storage order.
    if (!header.rowMajor && header.rows > 1 && header.cols > 1) {
        std::vector<double> rowMajor(values.size());
        for (uint32_t r = 0; r < header.rows; ++r)
            for (uint32_t c = 0; c < header.cols; ++c)
                rowMajor[static_cast<size_t>(r) * header.cols + c] = values[static_cast<size_t>(c) * header.rows + r];
        return rowMajor;
    }
    return values;
}

static void HandleClient(HANDLE pipe) {
    ++g_connectionCount;
    for (;;) {
        MessageHeader header{};
        if (!ReadExact(pipe, &header, sizeof(header))) break;
        if (header.magic != kProtocolMagic) break;
        if (header.dataBytes > kMaxMessageBytes) break;

        std::string name(header.nameLen, '\0');
        if (header.nameLen > 0 && !ReadExact(pipe, name.data(), header.nameLen)) break;

        std::vector<uint8_t> raw(static_cast<size_t>(header.dataBytes));
        if (header.dataBytes > 0 && !ReadExact(pipe, raw.data(), raw.size())) break;

        std::vector<double> normalized = ConvertAndNormalize(header, raw);
        g_store.Update(name, header.rows, header.cols, std::move(normalized));
    }
    --g_connectionCount;
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

static void PipeServerLoop() {
    while (g_running) {
        HANDLE pipe = CreateNamedPipeA(
            kPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            0, 1 << 20, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "arrayplot_viewer: CreateNamedPipeA failed (%lu)\n", GetLastError());
            break;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            std::thread(HandleClient, pipe).detach();
        } else {
            CloseHandle(pipe);
        }
    }
}

// ---------------- main ----------------
int main(int, char**) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandleW(nullptr),
                        nullptr, nullptr, nullptr, nullptr, L"arrayplot_viewer", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"arrayplot viewer", WS_OVERLAPPEDWINDOW,
                                 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        std::fprintf(stderr, "arrayplot_viewer: failed to create D3D11 device\n");
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    RegisterMonoColormap();

    std::thread serverThread(PipeServerLoop);
    serverThread.detach(); // parked in ConnectNamedPipe; OS reclaims it on process exit

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 110), ImGuiCond_FirstUseEver);
        ImGui::Begin("arrayplot");
        ImGui::Text("Listening on %s", kPipeName);
        ImGui::Text("Active connections: %d", g_connectionCount.load());
        ImGui::End();

        auto snapshot = g_store.Snapshot();
        for (auto& [name, entry] : snapshot) {
            ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
            ImGui::Begin(name.c_str());
            if (entry.rows <= 1 || entry.cols <= 1) {
                if (ImPlot::BeginPlot(name.c_str(), ImVec2(-1, -1))) {
                    ImPlot::PlotLine(name.c_str(), entry.data.data(), static_cast<int>(entry.data.size()));
                    ImPlot::EndPlot();
                }
            } else {
                double dataMin = *std::min_element(entry.data.begin(), entry.data.end());
                double dataMax = *std::max_element(entry.data.begin(), entry.data.end());
                if (dataMin == dataMax) dataMax = dataMin + 1.0; // avoid a degenerate scale range

                ImPlot::PushColormap(g_monoColormap);
                if (ImPlot::BeginPlot(name.c_str(), ImVec2(-80, -1), ImPlotFlags_NoLegend)) {
                    // Y is inverted so row 0 reads "0" at the top (image order) instead of
                    // labeling the top edge with the row count.
                    ImPlot::SetupAxes("col", "row", ImPlotAxisFlags_None, ImPlotAxisFlags_Invert);
                    ImPlot::SetupAxisFormat(ImAxis_X1, "%.0f");
                    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.0f");
                    ImPlot::PlotHeatmap(name.c_str(), entry.data.data(),
                                         static_cast<int>(entry.rows), static_cast<int>(entry.cols),
                                         dataMin, dataMax, "%.1f",
                                         ImPlotPoint(0.0, 0.0),
                                         ImPlotPoint(static_cast<double>(entry.cols), static_cast<double>(entry.rows)));
                    ImPlot::EndPlot();
                }
                ImGui::SameLine();
                ImPlot::ColormapScale("##scale", dataMin, dataMax, ImVec2(60, 0));
                ImPlot::PopColormap();
            }
            ImGui::End();
        }

        ImGui::Render();
        const float clearColor[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    g_running = false;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
                                                 featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                                 &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
                                             featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                             &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = static_cast<UINT>(LOWORD(lParam));
        g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
