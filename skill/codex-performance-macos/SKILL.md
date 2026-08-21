---
name: codex-performance-macos
description: Install, verify, repair, or uninstall event-driven User Initiated scheduling for ChatGPT, Codex CLI, Qoder IDE, Qoder CLI, and their work-session process trees on Apple silicon without granting agents root privileges.
---

# Codex Performance for macOS

Deploy three cooperating components:

- a per-user broker plus CLI/open shims that register work-session roots;
- a non-root App watcher that consumes `NSWorkspace` launch events; and
- a root LaunchDaemon that validates the configured UID and reapplies the role to registered process trees using `kqueue` lifecycle events.

Keep both agents and all workload commands non-root. The root keeper's local socket cannot execute commands and rejects callers or PIDs outside the configured login UID.

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
- Do not replace event tracking with unified-log collection. The keeper performs a 10-second safety scan only while tracked work exists and performs no process scan while idle.

## Validate changes

Run `scripts/test` after modifying the implementation. Also run the Skill Creator validator against this directory before publishing.
