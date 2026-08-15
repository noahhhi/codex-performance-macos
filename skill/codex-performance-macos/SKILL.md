---
name: codex-performance-macos
description: Install, verify, repair, or uninstall the Codex macOS performance broker and narrowly scoped QoS keeper on Apple silicon. Use when ChatGPT or Codex work saturates efficiency cores while performance cores remain underused, when local Codex commands inherit Utility QoS, or when reproducing this scheduling setup on another Mac without granting Codex root privileges.
---

# Codex Performance for macOS

Deploy two separate components:

- a per-user broker that launches local commands with an application role and User Initiated Darwin role; and
- a root LaunchDaemon that can only reapply that role to an exact allowlist of executables inside `/Applications/ChatGPT.app`.

Keep Codex and all workload commands non-root. The root keeper exposes no command or IPC interface.

## Choose an action

- Install from a repository checkout: ask the user to run `./install.sh` in Terminal. Do not pipe the installer through a shell or attempt interactive `sudo` through the Codex broker.
- Verify an installation: run `scripts/status` as the regular user.
- Repair an installation: inspect `scripts/status`, then ask the user to rerun `./install.sh` in Terminal.
- Remove it: ask the user to run `./uninstall.sh` in Terminal.

Administrator authorization is expected only for installing or removing the fixed keeper binary and LaunchDaemon. Never request or persist an administrator password, sudo ticket, credential, or broad sudoers rule.

## Preserve scheduling boundaries

- Use `~/.codex/bin/codex-performance-exec` for local commands after installation.
- Default sustained parallel work to `hw.perflevel0.physicalcpu`; foreground UI remains more important.
- Do not claim hard performance-core affinity. macOS ultimately chooses physical cores.
- Treat the Darwin process-role and spawn interfaces as unsupported implementation details that may require repair after a macOS update.

## Validate changes

Run `scripts/test` after modifying the implementation. Also run the Skill Creator validator against this directory before publishing.
