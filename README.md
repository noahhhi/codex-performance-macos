# Codex Performance for macOS

Deploys a per-user command broker and a narrowly scoped QoS keeper so local work started by ChatGPT/Codex is eligible for Apple silicon performance cores instead of remaining trapped in a Utility-QoS process tree.

This is an unofficial community workaround and is not supported or endorsed by OpenAI or Apple.

## Requirements

- Apple silicon Mac running macOS 11 or newer
- ChatGPT installed at `/Applications/ChatGPT.app`
- Xcode Command Line Tools (`xcode-select --install`)
- an administrator account for one installation prompt

## Install

Run this in Terminal as your normal login user:

```sh
git clone https://github.com/noahhhi/codex-performance-macos.git
cd codex-performance-macos
./install.sh
```

The installer compiles binaries locally, installs the reusable Skill under `~/.codex/skills`, adds a managed performance block to `~/.codex/AGENTS.md`, and requests administrator authorization only for the fixed keeper and LaunchDaemon. Restart ChatGPT after installation.

Verify at any time:

```sh
~/.codex/skills/codex-performance-macos/scripts/status
```

Uninstall from the checkout or installed Skill:

```sh
./uninstall.sh
```

## Security model

- Codex, the broker, and all workload commands remain the login user; they never run as root.
- The root keeper has no command execution or IPC interface. Every two seconds it enumerates one configured UID and changes the Darwin role only for an exact allowlist of executables inside `/Applications/ChatGPT.app`.
- The broker socket is mode `0600` and rejects peers whose UID differs from its owner.
- No password, sudo ticket, local account name, UID, home path, token, log, or machine-generated artifact is committed.

## Important limitations

This does not provide hard CPU affinity. macOS still selects physical cores. The implementation uses Darwin process-role and spawn interfaces that are visible in Apple open-source headers but are not supported public application APIs; a future macOS update may require repair.

The installer manages only the block between `<!-- BEGIN codex-performance-macos -->` and `<!-- END codex-performance-macos -->` in `~/.codex/AGENTS.md`. Existing instructions outside that block are preserved.

## Development

```sh
skill/codex-performance-macos/scripts/test
```

See [LICENSE](LICENSE) for terms.
