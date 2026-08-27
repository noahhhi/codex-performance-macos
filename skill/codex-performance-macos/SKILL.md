---
name: codex-performance-macos
description: Install, verify, repair, or remove a non-root direct launcher that gives Codex and Qoder CLI work application/default scheduling plus a User Initiated thread hint on Apple silicon, without scanning or modifying existing process trees.
---

# Codex Performance for macOS

Use the V2 direct launcher for command-line development work. It combines the
system `/usr/sbin/taskpolicy -a` interface with an in-process User Initiated QoS request,
then immediately executes the requested command.

## Safety boundary

- Keep the launcher and every workload non-root.
- Do not install a Keeper, App watcher, broker, socket, PID scanner, or periodic task.
- Do not use `PRIO_DARWIN_ROLE`, `EVFILT_PROC`, process-tree enumeration, private
  spawn roles, affinity, or negative niceness.
- Do not promise performance-core placement. macOS selects physical cores.
- Do not externally promote ChatGPT, Qoder IDE, or GUI Apps. Public macOS APIs do
  not provide a supported way to force arbitrary third-party process trees to
  User Initiated.
- Keep sustained heavy-job concurrency at or below `hw.perflevel0.physicalcpu`;
  reduce it when foreground responsiveness suffers.

## Operations

- Install from a checkout with `./install.sh` as the login user. A new V2 install
  requires no administrator access; migration prompts only to remove obsolete V1
  privileged files.
- Verify with `skill/codex-performance-macos/scripts/status`.
- Repair by rerunning `./install.sh`.
- Remove with `./uninstall.sh`.

Run `scripts/test` and the Skill Creator validator after implementation changes.
