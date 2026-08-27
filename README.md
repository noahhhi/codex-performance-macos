# Codex Performance for macOS

Provides a non-root, direct launcher for Codex CLI and Qoder CLI development
work on Apple silicon. V2 intentionally does not observe or rewrite existing
process trees.

This is an unofficial community workaround and is not supported or endorsed by
OpenAI or Apple.

## Why V2

The original implementation used a privileged Keeper, `EVFILT_PROC` lifecycle
events, process enumeration, and private Darwin role changes. A macOS 27
WindowServer watchdog report captured that Keeper, `runningboardd`, and an
executing shell blocked on the same kernel wait object. An older watchdog report
showed the underlying macOS failure without the tool, but V1 could enter and
amplify the same vulnerable path. V2 removes that entire architecture.

## What it does

- launches `codex` and `qoder` through Apple's `/usr/sbin/taskpolicy -a`;
- requests User Initiated QoS in the short-lived launcher before direct execution;
- lets child commands inherit application/default resource policy;
- installs no daemon, LaunchAgent, watcher, socket, broker, or root component;
- performs no PID enumeration, process inspection, periodic scan, or GUI launch interception.

## Install

Requirements: Apple silicon, macOS 11 or newer, and Xcode Command Line Tools.

```sh
git clone https://github.com/noahhhi/codex-performance-macos.git
cd codex-performance-macos
./install.sh
```

A clean V2 install needs no administrator authorization. Migrating an existing
V1 installation asks once only to remove the obsolete root-owned helper and
LaunchDaemon.

Open a new shell after installation, then verify:

```sh
skill/codex-performance-macos/scripts/status
```

Uninstall:

```sh
./uninstall.sh
```

## Limitations

macOS has no supported public API for an external utility to force arbitrary
third-party Apps and all descendants to User Initiated or to performance cores.
V2 therefore leaves ChatGPT, Qoder IDE, DingTalk, and other GUI Apps under normal
macOS/RunningBoard management. It does not provide CPU affinity.

For sustained builds, use at most `sysctl -n hw.perflevel0.physicalcpu` heavy
jobs and reduce the count if the foreground UI becomes less responsive.

## Development

```sh
skill/codex-performance-macos/scripts/test
```

See [LICENSE](LICENSE) for terms.
