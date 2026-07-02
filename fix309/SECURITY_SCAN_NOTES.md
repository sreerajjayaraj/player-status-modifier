# Security Scan Notes

This is an unsigned hobby ASI plugin. A missing digital signature can still be reported by scanners or mod managers.

Fix69 reduces avoidable scanner triggers:

- Static `GetAsyncKeyState` import removed.
- Windows version metadata added.
- Empty PE Thread Storage Directory stripped from the release ASI.
- TLS stripping is guarded by `tools/strip-empty-tls-directory.js`, which refuses to modify a file if the TLS callback table is non-empty.

Expected remaining informational warning:

- Unsigned binary.

No file overlay is intentionally used.
