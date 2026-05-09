# Safe Source Release Manifest

Release purpose:

- Keep the latest working `1.05.01` runtime fixes.
- Preserve source transparency.
- Avoid distributing compiled injected binaries.
- Add clear safety, build, and verification instructions.

Included:

- C++ source code
- CMake project files
- INI configuration examples
- safety/security documentation
- local build and verification scripts

Not included:

- `version.dll`
- prebuilt `.asi`
- prebuilt `.dll`
- game executable
- installers
- packers
- updaters

Functional baseline:

- incoming/fall damage reduction
- outgoing damage scaling
- item gain scaling
- durability loss prevention
- spirit scaling
- player tracking hard guard
- resistance multiplier applied to tracked player incoming-health writes
- horse/mount diagnostics and resolver work from fix-16
- affinity crash guard

Known limitation:

- affinity multiplier is intentionally left for last and is not enabled by this safe release.
