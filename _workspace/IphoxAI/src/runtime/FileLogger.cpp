#include "iphox/runtime/FileLogger.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iomanip>
#include <sstream>
#include <system_error>

namespace iphox::runtime {

FileLogger::FileLogger(
    std::filesystem::path path,
    std::size_t maxBytes)
    : path_(std::move(path)),
      maxBytes_(maxBytes) {

    if (maxBytes_ == 0 ||
        path_.empty()) {
        return;
    }

    std::error_code ec;

    const auto parent =
        path_.parent_path();

    if (!parent.empty()) {
        std::filesystem::create_directories(
            parent,
            ec);

        if (ec) {
            return;
        }
    }

    if (std::filesystem::exists(path_, ec) &&
        !ec) {

        const auto size =
            std::filesystem::file_size(
                path_,
                ec);

        if (!ec) {
            bytesWritten_ =
                static_cast<std::size_t>(
                    size);
        }
    }

    if (bytesWritten_ >= maxBytes_) {
        (void)Rotate();
    } else {
        (void)OpenAppend();
    }
}

bool FileLogger::Enabled() const noexcept {
    std::scoped_lock lock{mutex_};
    return stream_.is_open();
}

void FileLogger::Write(
    LogLevel level,
    std::string_view message) {

    std::scoped_lock lock{mutex_};

    if (!stream_.is_open()) {
        return;
    }

    std::string line;
    line.reserve(
        message.size() + 64);

    line += TimestampUtc();
    line += " [";
    line += LevelName(level);
    line += "] ";
    line += Sanitize(message);
    line += "\n";

    if (line.size() > maxBytes_) {
        line.resize(maxBytes_);
        if (!line.empty()) {
            line.back() = '\n';
        }
    }

    if (bytesWritten_ + line.size() >
        maxBytes_) {

        if (!Rotate()) {
            return;
        }
    }

    stream_.write(
        line.data(),
        static_cast<std::streamsize>(
            line.size()));

    stream_.flush();

    if (stream_) {
        bytesWritten_ +=
            line.size();
    }
}

bool FileLogger::OpenAppend() {
    stream_.open(
        path_,
        std::ios::binary |
            std::ios::app);

    return stream_.is_open();
}

bool FileLogger::Rotate() {
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }

    std::error_code ec;

    auto backup = path_;
    backup += L".1";

    std::filesystem::remove(
        backup,
        ec);

    ec.clear();

    if (std::filesystem::exists(
            path_,
            ec) &&
        !ec) {

        std::filesystem::rename(
            path_,
            backup,
            ec);

        if (ec) {
            return false;
        }
    }

    bytesWritten_ = 0;
    return OpenAppend();
}

std::string FileLogger::TimestampUtc() {
    SYSTEMTIME time{};
    GetSystemTime(&time);

    std::ostringstream out;

    out
        << std::setfill('0')
        << std::setw(4)
        << time.wYear
        << "-"
        << std::setw(2)
        << time.wMonth
        << "-"
        << std::setw(2)
        << time.wDay
        << "T"
        << std::setw(2)
        << time.wHour
        << ":"
        << std::setw(2)
        << time.wMinute
        << ":"
        << std::setw(2)
        << time.wSecond
        << "."
        << std::setw(3)
        << time.wMilliseconds
        << "Z";

    return out.str();
}

const char* FileLogger::LevelName(
    LogLevel level) noexcept {

    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }

    return "INFO";
}

std::string FileLogger::Sanitize(
    std::string_view value) {

    std::string out;
    out.reserve(value.size());

    for (const char ch : value) {
        if (ch == '\r' ||
            ch == '\n' ||
            ch == '\0') {
            out.push_back(' ');
        } else {
            out.push_back(ch);
        }
    }

    return out;
}

} // namespace iphox::runtime
