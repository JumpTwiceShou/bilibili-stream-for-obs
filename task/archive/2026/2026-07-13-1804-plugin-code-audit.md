# OBS Bilibili Plugin Code Audit

## Scope

- Perform a read-only audit of the native C++/Qt OBS plugin.
- Review core plugin lifecycle, Bilibili API and stream-control paths, configuration, UI/state handling, and build configuration.
- Run only focused checks that are available in the current Windows workspace.
- Record actionable findings with file and line evidence.

## Exclusions

- No plugin source changes, dependency upgrades, deployment, push, or public release.
- `plugin2` and outer-workspace code are out of scope.
- No live Bilibili account/API mutation or OBS production-stream test.

## Ordered Work

- [x] Read shared rules, project instructions, and local integration notes.
- [x] Confirm branch, HEAD, upstream, remotes, and clean starting worktree.
- [x] Inspect repository structure and build/test instructions.
- [x] Audit core source paths and identify evidence-backed findings.
- [x] Run focused static/build validation where locally available.
- [x] Review the scoped diff and staged files.
- [x] Record final result and archive this task.

## Acceptance Criteria

- Findings are ranked by severity and include precise code locations.
- Each finding explains user impact and a concrete remediation direction.
- Validation evidence and any unverified residual risk are explicit.
- The final response distinguishes correctness issues from optional optimizations.

## Evidence

- Start branch: `main`.
- Start HEAD: `a28f58a9716ccbf3c4c00bf677c9f7a02b409600`.
- Upstream: `origin/main`.
- Starting worktree: clean.
- Live remote output differs from the repository note: `origin` currently points to the private development repository and no `sync` remote is configured. No remote mutation is in scope.
- `CMakePresets.json` parses successfully: 7 configure presets and 6 build presets.
- Static inventory found 13 synchronous HTTP call sites and no consumers of the implemented async HTTP methods.
- Static inventory found 13 full response-body access/log sites in the API layer and 2 unexpanded CMake template tokens in a compiled source file.
- No test files or tracked `.github/workflows` directory exist in this checkout.
- `git diff --check` passed.
- CMake, MSVC (`cl`/`msbuild`), clang-format, and zsh are unavailable on this VM, so compilation and formatter execution were not possible.
- Official OBS module documentation confirms that `obs_module_file` resolves a module data file while `obs_module_config_path` resolves per-module configuration storage.
- Official CMake documentation confirms that an absolute install `DESTINATION` is not relocated by `cmake --install --prefix`.

### Findings

1. High: authentication and streaming secrets are written to OBS logs. QR-login logs the successful response body containing the credential-bearing callback URL; start-live logs the response containing the RTMP stream code; an invalid start configuration logs and displays the CSRF token.
2. High: configuration uses `obs_module_file("config.json")`, so mutable per-user state is read from and written to the installed resource tree. Save failures are ignored, and cookies, CSRF material, and the RTMP stream code are persisted there in plaintext.
3. High: all API work is synchronous on plugin construction, Qt timer callbacks, menu slots, and the OBS streaming-stopped callback. With the 10-second default timeout, startup can block for roughly 30 seconds and native stop synchronization for roughly 20 seconds; QR/face polling can repeatedly freeze the UI.
4. Medium: the native stop callback stops any live Bilibili room associated with the account after any OBS streaming-stopped event, even when the plugin did not start that Bilibili session or OBS was streaming to another service. This matches the current integration note but needs an explicit ownership/product decision.
5. Medium: form bodies are built by raw string concatenation. In particular, a room title containing `&`, `=`, `+`, `%`, or other reserved bytes is parsed as different form fields or corrupted.
6. Medium: partition IDs from a remote JSON response are passed to `std::stoi` without type checks or exception handling, allowing a malformed or changed API response to unwind through Qt and potentially terminate OBS.
7. Medium: plugin lifecycle/build hygiene has several defects: the HTTP worker starts before the main-window null check and is not cleaned on that failure path; the menu is owned by OBS's menu bar and is not removed on plugin unload; compiled `plugin_utils.cpp` leaves `@CMAKE_PROJECT_NAME@` and `@CMAKE_PROJECT_VERSION@` literal; the top-level CMake adds an absolute duplicate install rule and repeats `plugin-main.cpp`.
8. Low: async HTTP infrastructure is initialized and maintained but unused, locale resources are effectively unused because UI strings are hard-coded, and no automated regression coverage exists for response parsing, state transitions, redaction, or form encoding.

## Decisions

- Treat the user request as a formal read-only code audit.
- Do not exercise live account, streaming, or stop-live operations.
- Rank log/config secret exposure and UI-thread blocking ahead of structural refactoring.
- Treat auto-stop ownership as a product-safety decision because the current local integration note explicitly requests live-status-based stopping.

## Blockers

- Local compile/format validation is blocked by missing CMake, MSVC, clang-format, and zsh toolchains.
- Live Bilibili API behavior was intentionally not exercised; API compatibility remains an external residual risk.

## Commits And Device Sync

- No commit, push, release, deployment, or device sync performed.

## Final Result

- Audit completed with 3 high-priority, 4 medium-priority, and 1 low-priority finding group.
- Recommended remediation order: redact secrets; move/protect configuration and stop persisting the stream key; make network operations asynchronous with UI-thread marshalling and cancellation; add ownership gating to native stop; centralize form encoding and JSON validation; then clean lifecycle/build/test debt.
- Source code was not changed. Only this required audit task record was created and archived.
