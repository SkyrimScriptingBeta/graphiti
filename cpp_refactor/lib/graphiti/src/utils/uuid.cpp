#include "uuid.h"

#include <array>
#include <format>
#include <random>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace graphiti::uuid {

static void fill_random_bytes(uint8_t* buf, size_t len) {
#ifdef _WIN32
    BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    // /dev/urandom via std::random_device (implementation-defined but good on Linux/macOS)
    std::random_device rd;
    for (size_t i = 0; i < len; i += 4) {
        auto val = rd();
        auto bytes_to_copy = std::min(len - i, size_t{4});
        std::memcpy(buf + i, &val, bytes_to_copy);
    }
#endif
}

std::string generate() {
    std::array<uint8_t, 16> bytes{};
    fill_random_bytes(bytes.data(), bytes.size());

    // Set version (4) and variant (RFC 4122)
    bytes[6] = (bytes[6] & 0x0f) | 0x40;  // Version 4
    bytes[8] = (bytes[8] & 0x3f) | 0x80;  // Variant 1

    return std::format(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
        bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]
    );
}

} // namespace graphiti::uuid
