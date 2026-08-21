# Codex Performance for macOS

Deploys an event-driven process-tree scheduler for ChatGPT, Codex CLI, Qoder IDE, and Qoder CLI. Workloads such as CMake, compilers, scripts, and GUI apps opened by an agent are registered as session roots and kept at User Initiated scheduling without running the agents or workloads as root.

This is an unofficial community workaround and is not supported or endorsed by OpenAI or Apple.

## Requirements

- Apple silicon Mac running macOS 11 or newer
- ChatGPT and/or Qoder IDE installed in `/Applications`
- Xcode Command Line Tools (`xcode-select --install`)
- an administrator account for one installation prompt

## Install

Run this in Terminal as your normal login user:

```sh
git clone https://github.com/noahhhi/codex-performance-macos.git
cd codex-performance-macos
./install.sh
```

The installer compiles binaries locally, installs the reusable Skill under `~/.codex/skills`, and requests administrator authorization only for the fixed keeper and LaunchDaemon. It also installs a non-root App watcher plus `codex`, `qoder`, and `open` shell shims, with managed PATH blocks in `~/.zshenv` and `~/.zprofile`. Open a new shell after installation; no Qoder Skill or Qoder settings change is required.

Verify at any time:

```sh
~/.codex/skills/codex-performance-macos/scripts/status
```

Uninstall from the checkout or installed Skill:

```sh
./uninstall.sh
```

## Security model

- ChatGPT, Qoder, both CLIs, the broker, and all workload commands remain the login user; they never run as root.
- A non-root `NSWorkspace` observer registers only ChatGPT and Qoder. The `open` shim resolves the requested target App and registers only that PID; it does not inspect another process's arguments or environment.
- The root keeper's mode-`0600` socket accepts only the configured login UID, validates that each PID belongs to that UID, and can only apply a Darwin scheduling role. It cannot execute commands.
- Registered roots receive User Initiated before they launch work, so fork/exec descendants inherit the role without waiting for a scan. `kqueue` fork/exec/exit events trigger a 50 ms coalesced process-tree reconciliation for bookkeeping; a 10-second safety reconciliation runs only while tracked work exists. With no tracked App, CLI, or child process, the keeper blocks without scanning.
- No password, sudo ticket, local account name, UID, home path, token, log, or machine-generated artifact is committed.

## Important limitations

This does not provide hard CPU affinity. macOS still selects physical cores. The implementation uses Darwin process-role and spawn interfaces that are visible in Apple open-source headers but are not supported public application APIs; a future macOS update may require repair.

The `open` shim can register named or bundle-ID launches such as `open -a DingTalk`. Apps activated through an unrelated automation API without starting or registering a process retain macOS's normal foreground scheduling.

The installer manages only the block between `<!-- BEGIN codex-performance-macos -->` and `<!-- END codex-performance-macos -->` in `~/.codex/AGENTS.md`. Existing instructions outside that block are preserved.

## Development

```sh
skill/codex-performance-macos/scripts/test
```

See [LICENSE](LICENSE) for terms.
