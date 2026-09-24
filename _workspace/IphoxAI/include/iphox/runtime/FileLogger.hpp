#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace iphox::runtime {

enum class LogLevel {
    Info,
    Warning,
    Error
};

class FileLogger final {
public:
    explicit FileLogger(
        std::filesystem::path path,
        std::size_t maxBytes = 2u * 1024u * 1024u);

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    [[nodiscard]] bool Enabled() const noexcept;

    void Write(
        LogLevel level,
        std::string_view message);

    [[nodiscard]] const std::filesystem::path& Path() const noexcept {
        return path_;
    }

private:
    [[nodiscard]] bool OpenAppend();
    [[nodiscard]] bool Rotate();
    [[nodiscard]] static std::string TimestampUtc();
    [[nodiscard]] static const char* LevelName(LogLevel level) noexcept;
    [[nodiscard]] static std::string Sanitize(std::string_view value);

    std::filesystem::path path_;
    std::size_t maxBytes_;
    std::size_t bytesWritten_{};

    mutable std::mutex mutex_;
    std::ofstream stream_;
};

} // namespace iphox::runtime
