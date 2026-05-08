# Runtime Fix 12B - build fix

Fixes MSVC redefinition errors caused by `kMinimumPointerAddress` being defined in both:

- `src/runtime/runtime_state.h`
- `src/hooks/hooks_internal.h`

`hooks_internal.h` now includes `runtime/runtime_state.h` and uses the shared constant from there.

No runtime behavior was intentionally changed from fix-12.
Build as x64-Release.
