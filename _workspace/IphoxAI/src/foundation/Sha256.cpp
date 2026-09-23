#include "iphox/foundation/Sha256.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace iphox::foundation {
namespace {

bool NtSuccess(NTSTATUS status) noexcept {
    return status >= 0;
}

} // namespace

std::optional<Sha256Digest> Sha256(
    std::span<const std::byte> bytes) noexcept {

    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};

    if (!NtSuccess(BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0))) {
        return std::nullopt;
    }

    DWORD objectBytes{};
    DWORD copied{};

    if (!NtSuccess(BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes),
            sizeof(objectBytes),
            &copied,
            0))) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }

    DWORD hashBytes{};

    if (!NtSuccess(BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashBytes),
            sizeof(hashBytes),
            &copied,
            0)) ||
        hashBytes != Sha256Digest{}.size()) {

        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }

    std::vector<UCHAR> object(objectBytes);

    if (!NtSuccess(BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            static_cast<ULONG>(object.size()),
            nullptr,
            0,
            0))) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }

    bool ok = true;

    if (!bytes.empty()) {
        if (bytes.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<ULONG>::max)())) {
            ok = false;
        } else if (!NtSuccess(BCryptHashData(
                       hash,
                       reinterpret_cast<PUCHAR>(
                           const_cast<std::byte*>(
                               bytes.data())),
                       static_cast<ULONG>(
                           bytes.size()),
                       0))) {
            ok = false;
        }
    }

    Sha256Digest digest{};

    if (ok &&
        !NtSuccess(BCryptFinishHash(
            hash,
            reinterpret_cast<PUCHAR>(
                digest.data()),
            static_cast<ULONG>(
                digest.size()),
            0))) {
        ok = false;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    if (!ok) {
        return std::nullopt;
    }

    return digest;
}

std::string Hex(
    const Sha256Digest& digest) {

    std::ostringstream out;
    out << std::hex << std::setfill('0');

    for (const auto byte : digest) {
        out << std::setw(2)
            << std::to_integer<unsigned int>(
                   byte);
    }

    return out.str();
}

} // namespace iphox::foundation
