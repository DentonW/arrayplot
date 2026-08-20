// arrayplot_viewer (Linux): Dear ImGui + ImPlot over GLFW + OpenGL3. Runs a
// Unix-domain-socket server (/tmp/arrayplot.sock, see common/protocol.h) in
// a background thread and renders whatever arrays/matrices arrive, keyed by
// name, live. Shared data-store/parsing/draw logic lives in plot_store.h.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "plot_store.h"
#include "protocol.h"

using namespace aplot;

static void GlfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "arrayplot_viewer: GLFW error %d: %s\n", error, description);
}

// ---------------- socket server ----------------
static void HandleClient(int fd) {
    aplot_viewer::HandleClientGeneric([fd](void* buf, size_t size) -> bool {
        uint8_t* p = static_cast<uint8_t*>(buf);
        size_t remaining = size;
        while (remaining > 0) {
            ssize_t n = ::recv(fd, p, remaining, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) return false; // peer closed
            p += static_cast<size_t>(n);
            remaining -= static_cast<size_t>(n);
        }
        return true;
    });
    ::close(fd);
}

static void SocketServerLoop() {
    ::unlink(kSocketPath);

    int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd < 0) {
        std::perror("arrayplot_viewer: socket");
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);

    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("arrayplot_viewer: bind");
        ::close(listenFd);
        return;
    }
    if (::listen(listenFd, 16) != 0) {
        std::perror("arrayplot_viewer: listen");
        ::close(listenFd);
        return;
    }

    while (aplot_viewer::g_running) {
        int clientFd = ::accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        std::thread(HandleClient, clientFd).detach();
    }
    ::close(listenFd);
    ::unlink(kSocketPath);
}

// ---------------- main ----------------
int main(int, char**) {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "arrayplot_viewer: glfwInit failed\n");
        return 1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "arrayplot viewer", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "arrayplot_viewer: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    aplot_viewer::RegisterMonoColormap();

    std::thread serverThread(SocketServerLoop);
    serverThread.detach(); // parked in accept(); OS reclaims it on process exit

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        aplot_viewer::DrawFrame(kSocketPath);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    aplot_viewer::g_running = false;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
