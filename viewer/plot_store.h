#pragma once

// Platform-agnostic core of arrayplot_viewer: the received-data store,
// message parsing, colormap setup, and the per-frame ImGui/ImPlot draw
// routine. Shared by main_win32.cpp (Win32 + DirectX11 + named pipe) and
// main_linux.cpp (GLFW + OpenGL3 + Unix domain socket), which each supply
// only the windowing/graphics backend and the platform-specific transport
// accept loop.

#include "imgui.h"
#include "implot.h"

#include "protocol.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace aplot_viewer {

using namespace aplot;

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

inline PlotStore g_store;
inline std::atomic<bool> g_running{true};
inline std::atomic<int> g_connectionCount{0};

// Single-hue sequential colormap (dark -> light blue) so heatmap intensity
// reads as shade, not hue -- registered once, after ImPlot::CreateContext().
inline ImPlotColormap g_monoColormap = -1;

inline void RegisterMonoColormap() {
    static const ImVec4 monoColors[] = {
        ImVec4(0.03f, 0.19f, 0.42f, 1.0f),
        ImVec4(0.13f, 0.44f, 0.71f, 1.0f),
        ImVec4(0.42f, 0.68f, 0.84f, 1.0f),
        ImVec4(0.78f, 0.86f, 0.94f, 1.0f),
        ImVec4(0.97f, 0.98f, 1.00f, 1.0f),
    };
    g_monoColormap = ImPlot::AddColormap("arrayplot_mono", monoColors, IM_ARRAYSIZE(monoColors), false);
}

inline std::vector<double> ConvertAndNormalize(const MessageHeader& header, const std::vector<uint8_t>& raw) {
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

// Reads and applies messages from one connection until it closes or sends
// something malformed. `read_exact(buf, size)` should block until `size`
// bytes have been read into `buf`, returning false on error/EOF/closed --
// each platform's accept loop supplies this wrapping ReadFile or recv().
// Connection bookkeeping (g_connectionCount) is handled here so callers
// don't have to duplicate it.
template <typename ReadExactFn>
inline void HandleClientGeneric(ReadExactFn&& read_exact) {
    ++g_connectionCount;
    for (;;) {
        MessageHeader header{};
        if (!read_exact(&header, sizeof(header))) break;
        if (header.magic != kProtocolMagic) break;
        if (header.dataBytes > kMaxMessageBytes) break;

        std::string name(header.nameLen, '\0');
        if (header.nameLen > 0 && !read_exact(name.data(), header.nameLen)) break;

        std::vector<uint8_t> raw(static_cast<size_t>(header.dataBytes));
        if (header.dataBytes > 0 && !read_exact(raw.data(), raw.size())) break;

        std::vector<double> normalized = ConvertAndNormalize(header, raw);
        g_store.Update(name, header.rows, header.cols, std::move(normalized));
    }
    --g_connectionCount;
}

// Draws the status panel and one window per received array/matrix. Call
// between ImGui::NewFrame() and ImGui::Render(). `transportDescription` is
// shown in the status panel (e.g. the pipe name or socket path).
inline void DrawFrame(const char* transportDescription) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 110), ImGuiCond_FirstUseEver);
    ImGui::Begin("arrayplot");
    ImGui::Text("Listening on %s", transportDescription);
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
}

} // namespace aplot_viewer
