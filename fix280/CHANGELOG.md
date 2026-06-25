# Changelog

## 1.05.01-fix69

- Fixed DMM scan warning path by stripping an empty PE Thread Storage Directory from the release ASI.
- Added a reproducible post-build tool: `tools/strip-empty-tls-directory.js`.
- Verified the TLS callback table was empty before stripping the PE directory.
- Kept version metadata present in the ASI.

## 1.05.01-fix68

- Replaced remaining `std::thread` usage with Win32 worker threads.
- Removed the standard-library thread implementation path that was pulling in runtime TLS metadata.
- Preserved config watcher, mount resolver, and position-control listener behavior.

## 1.05.01-fix67

- Removed static `GetAsyncKeyState` import by resolving the API dynamically only when position hotkeys are active.
- Replaced explicit `thread_local` scratch/RNG storage with non-TLS storage.
- Added Windows `VS_VERSION_INFO` metadata.

## 1.05.01-fix66

- Fixed boss and large enemy health being incorrectly locked as mount health.
- Mount health lock now applies only to exact confirmed tracked mount health entries.
- Removed unsafe large-health fallback that could restore boss health.

## 1.05.01-fix65

- Added rideable-proxy mount tracking for current game mount layouts.
- Confirmed rideable mount stamina relock on current game version.
