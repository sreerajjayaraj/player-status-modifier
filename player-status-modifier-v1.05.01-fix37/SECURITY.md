# Security Notes

This package is a source-first release of `player-status-modifier` for Crimson Desert `1.05.01`.

## What this mod does

The compiled output is a native Windows DLL with an `.asi` extension. It is loaded into the game process by Ultimate ASI Loader and modifies runtime game values through validated hooks.

The mod can read and write process memory inside the game process for these features:

- player incoming health write reduction
- outgoing damage scaling against non-player health writes
- item gain quantity scaling
- durability loss reduction
- spirit/stamina/mount stat handling
- resistance multiplier on tracked player incoming-health writes
- optional position and dragon-limit hooks, where available

This behavior is why ASI mods and trainers can be flagged by antivirus products even when the source is benign.

## What this package intentionally does not include

This source release does **not** include:

- `version.dll`
- compiled `.asi`, `.dll`, `.exe`, `.pdb`, `.lib`, `.obj`, or installer files
- the game executable
- packed, encrypted, or obfuscated binaries
- auto-updaters
- network code
- telemetry
- persistence/startup registration

Build the ASI locally from source using Visual Studio.

## Network behavior

The source is not intended to use networking. The project should not import or call WinINet, WinHTTP, Winsock, HTTP, socket, downloader, updater, or telemetry APIs.

The included verification script performs a simple post-build string/import sanity check for common debug CRT and network-related names. This check is not a formal security audit, but it catches common mistakes.

## Loader

Ultimate ASI Loader is external and is not bundled here. Use your own trusted loader file if you choose to use one.

Recommended game folder layout after local build:

```text
CrimsonDesert\bin64\
  version.dll
  player-status-modifier.asi
  player-status-modifier.ini
```

Keep only one `player-status-modifier.asi` in the folder. Remove old backup `.asi` files so the loader cannot load the wrong build.

## Antivirus notes

Changing `.asi` to `.dll`, `.bin`, or another extension does not make native injected code safer. Trust comes from:

- readable source
- local Release builds
- no obfuscation or packing
- no network behavior
- reproducible hashes
- clear documentation of hooks and memory writes
