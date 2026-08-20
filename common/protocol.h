#pragma once

// Wire protocol shared between the arrayplot client header and the viewer.
// One message on the pipe = [MessageHeader][name bytes][data bytes].

#include <cstdint>

namespace aplot {

constexpr uint32_t kProtocolMagic = 0x504C4F54; // "PLOT"
constexpr const char* kPipeName = R"(\\.\pipe\arrayplot)";   // Windows named pipe
constexpr const char* kSocketPath = "/tmp/arrayplot.sock";   // Unix domain socket (Linux/macOS)

// Sanity cap so a corrupted/foreign message can't make the viewer try to
// allocate an absurd amount of memory.
constexpr uint64_t kMaxMessageBytes = 256ull * 1024 * 1024;

enum class DType : uint32_t {
    Float64 = 0,
    Float32 = 1,
};

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic;      // kProtocolMagic
    uint32_t nameLen;    // bytes of the name that follow the header
    DType    dtype;
    uint32_t rows;
    uint32_t cols;       // 1 for 1D arrays/vectors
    uint8_t  rowMajor;   // 1 = row-major, 0 = column-major (meaningful when rows>1 && cols>1)
    uint64_t dataBytes;  // size in bytes of the data payload that follows the name
};
#pragma pack(pop)

} // namespace aplot
