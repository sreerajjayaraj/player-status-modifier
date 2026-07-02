#include "logger.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

namespace {

std::mutex g_log_mutex;
std::filesystem::path g_log_path;
HANDLE g_log_handle = INVALID_HANDLE_VALUE;
bool g_log_enabled = false;
bool g_log_verbose = false;
DWORD g_log_max_lines = 2000;
uint64_t g_log_line_count = 0;

void CloseLogHandle() {
    if (g_log_handle == INVALID_HANDLE_VALUE) {
        return;
    }

    FlushFileBuffers(g_log_handle);
    CloseHandle(g_log_handle);
    g_log_handle = INVALID_HANDLE_VALUE;
    g_log_line_count = 0;
}

DWORD SanitizeMaxLogLines(const DWORD max_log_lines) {
    if (max_log_lines == 0) {
        return 2000;
    }

    if (max_log_lines > 1000000) {
        return 1000000;
    }

    return max_log_lines;
}

void RefreshLogLineCountLocked() {
    if (g_log_handle == INVALID_HANDLE_VALUE) {
        g_log_line_count = 0;
        return;
    }

    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(g_log_handle, zero, nullptr, FILE_BEGIN)) {
        g_log_line_count = 0;
        return;
    }

    char buffer[4096]{};
    uint64_t line_count = 0;
    DWORD bytes_read = 0;
    while (ReadFile(g_log_handle, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
        for (DWORD i = 0; i < bytes_read; ++i) {
            if (buffer[i] == '\n') {
                ++line_count;
            }
        }
    }

    zero.QuadPart = 0;
    SetFilePointerEx(g_log_handle, zero, nullptr, FILE_END);
    g_log_line_count = line_count;
}

bool OpenLogHandle(const DWORD creation_disposition) {
    if (g_log_path.empty()) {
        return false;
    }

    CloseLogHandle();

#if defined(PLAYER_STATUS_MODIFIER_ANALYSIS_LOG_BACKUP) && PLAYER_STATUS_MODIFIER_ANALYSIS_LOG_BACKUP
    if (creation_disposition == CREATE_ALWAYS) {
        std::error_code ec;
        const bool has_previous_log = std::filesystem::is_regular_file(g_log_path, ec);
        ec.clear();
        const auto previous_log_size = has_previous_log ? std::filesystem::file_size(g_log_path, ec) : 0;
        if (has_previous_log && !ec && previous_log_size > 0) {
            SYSTEMTIME local_time{};
            GetLocalTime(&local_time);

            wchar_t suffix[96]{};
            swprintf_s(suffix,
                       L".prelaunch.%04u%02u%02u-%02u%02u%02u.pid%lu.log",
                       local_time.wYear,
                       local_time.wMonth,
                       local_time.wDay,
                       local_time.wHour,
                       local_time.wMinute,
                       local_time.wSecond,
                       static_cast<unsigned long>(GetCurrentProcessId()));

            const std::filesystem::path backup_dir =
                L"C:\\Users\\sreer\\Documents\\Codex\\2026-05-11\\files-mentioned-by-the-user-crimsondesert\\player-status-log-backups";
            std::filesystem::create_directories(backup_dir, ec);
            ec.clear();
            const std::filesystem::path backup_path =
                backup_dir / (g_log_path.stem().wstring() + suffix);
            std::filesystem::copy_file(g_log_path,
                                       backup_path,
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec);
        }
    }
#endif

    g_log_handle = CreateFileW(g_log_path.c_str(),
                               FILE_APPEND_DATA | GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr,
                               creation_disposition,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (g_log_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (creation_disposition == CREATE_ALWAYS) {
        g_log_line_count = 0;
    } else {
        RefreshLogLineCountLocked();
    }

    return true;
}

}  // namespace

bool InitializeLogger(const std::wstring& path,
                      const bool enabled,
                      const bool verbose,
                      const DWORD max_log_lines) {
    std::lock_guard lock(g_log_mutex);

    g_log_path = std::filesystem::path(path);
    g_log_enabled = enabled;
    g_log_verbose = verbose;
    g_log_max_lines = SanitizeMaxLogLines(max_log_lines);
    if (!g_log_enabled) {
        CloseLogHandle();
        return true;
    }

    return OpenLogHandle(CREATE_ALWAYS);
}

void UpdateLoggerConfig(const bool enabled, const bool verbose, const DWORD max_log_lines) {
    std::lock_guard lock(g_log_mutex);

    g_log_verbose = verbose;
    g_log_max_lines = SanitizeMaxLogLines(max_log_lines);

    if (!enabled) {
        CloseLogHandle();
        g_log_enabled = false;
        return;
    }

    if (g_log_handle == INVALID_HANDLE_VALUE && !OpenLogHandle(OPEN_ALWAYS)) {
        g_log_enabled = false;
        return;
    }

    RefreshLogLineCountLocked();
    g_log_enabled = true;
}

void ShutdownLogger() {
    std::lock_guard lock(g_log_mutex);

    CloseLogHandle();
    g_log_enabled = false;
}

void Log(const char* format, ...) {
    std::lock_guard lock(g_log_mutex);
    if (!g_log_enabled || g_log_handle == INVALID_HANDLE_VALUE) {
        return;
    }

    char payload[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(payload, sizeof(payload), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    const DWORD process_id = GetCurrentProcessId();
    const DWORD thread_id = GetCurrentThreadId();

    char prefix[96]{};
    _snprintf_s(prefix,
                sizeof(prefix),
                _TRUNCATE,
                "[%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu] ",
                local_time.wYear,
                local_time.wMonth,
                local_time.wDay,
                local_time.wHour,
                local_time.wMinute,
                local_time.wSecond,
                local_time.wMilliseconds,
                static_cast<unsigned long>(process_id),
                static_cast<unsigned long>(thread_id));

    char line[1152]{};
    const int written = _snprintf_s(line,
                                    sizeof(line),
                                    _TRUNCATE,
                                    "%s%s\r\n",
                                    prefix,
                                    payload);
    if (written <= 0) {
        return;
    }

    if (!g_log_verbose) {
        const uint64_t max_lines = static_cast<uint64_t>(g_log_max_lines);
        if (g_log_line_count >= max_lines) {
            return;
        }
    }

    DWORD bytes_written = 0;
    if (!WriteFile(g_log_handle,
                   line,
                   static_cast<DWORD>(written),
                   &bytes_written,
                   nullptr)) {
        return;
    }

    ++g_log_line_count;

    FlushFileBuffers(g_log_handle);
}
