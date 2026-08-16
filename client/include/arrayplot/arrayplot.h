#pragma once

// Header-only client for arrayplot. Include this, then call aplot::plot1d /
// aplot::plot2d (or aplot::plot(...) from arrayplot_eigen.h for Eigen types).
//
// Calls silently no-op if the arrayplot_viewer process isn't running -- this
// is designed to be called from the Immediate Window mid-debug, or from a
// conditional breakpoint (condition: `aplot::plot1d("x", arr, n), false`),
// so it must never throw or block for long.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <type_traits>

#include "protocol.h"

namespace aplot {
namespace detail {

template <typename T> struct dtype_of;
template <> struct dtype_of<double> { static constexpr DType value = DType::Float64; };
template <> struct dtype_of<float>  { static constexpr DType value = DType::Float32; };

// One shared connection per process (Meyer's singleton -- safe even though
// this header-only function is defined in every translation unit that
// includes it, because inline function-local statics are merged by the
// linker into a single instance).
class PipeConnection {
public:
    bool Send(const MessageHeader& header, const char* name, const void* data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureConnected()) return false;

        bool ok = WriteAll(&header, sizeof(header)) &&
                  WriteAll(name, header.nameLen) &&
                  WriteAll(data, static_cast<size_t>(header.dataBytes));
        if (!ok) Close();
        return ok;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::mutex mutex_;

    bool EnsureConnected() {
        if (handle_ != INVALID_HANDLE_VALUE) return true;

        handle_ = ::CreateFileA(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ != INVALID_HANDLE_VALUE) return true;

        if (::GetLastError() == ERROR_PIPE_BUSY && ::WaitNamedPipeA(kPipeName, 200)) {
            handle_ = ::CreateFileA(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return true;
        }
        // Viewer isn't running (or gave up busy-waiting) -- caller no-ops.
        return false;
    }

    bool WriteAll(const void* data, size_t size) {
        if (size == 0) return true;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        size_t remaining = size;
        while (remaining > 0) {
            DWORD written = 0;
            if (!::WriteFile(handle_, p, static_cast<DWORD>(remaining), &written, nullptr) || written == 0)
                return false;
            p += written;
            remaining -= written;
        }
        return true;
    }

    void Close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }
};

inline PipeConnection& Connection() {
    static PipeConnection conn;
    return conn;
}

} // namespace detail

// `data` must be contiguous. `rowMajor` is ignored when rows<=1 or cols<=1.
template <typename T>
inline void plot2d(const char* name, const T* data, size_t rows, size_t cols, bool rowMajor = true) {
    static_assert(std::is_same<T, double>::value || std::is_same<T, float>::value,
                  "arrayplot: only float and double are supported");

    MessageHeader header{};
    header.magic = kProtocolMagic;
    header.nameLen = static_cast<uint32_t>(std::strlen(name));
    header.dtype = detail::dtype_of<T>::value;
    header.rows = static_cast<uint32_t>(rows);
    header.cols = static_cast<uint32_t>(cols);
    header.rowMajor = rowMajor ? 1 : 0;
    header.dataBytes = static_cast<uint64_t>(rows * cols * sizeof(T));

    detail::Connection().Send(header, name, data);
}

template <typename T>
inline void plot1d(const char* name, const T* data, size_t n) {
    plot2d(name, data, n, static_cast<size_t>(1), true);
}

} // namespace aplot
