# Local kernel build on this Mac (no homebrew, no GitHub Actions)

The kernel builds with Apple's Xcode 15.4 + KDK 13.5 via
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) (pinned
SHA in `tools/xnu-build-local.sh`), the same recipe upstream's CI uses. This
machine is an x86_64 Hackintosh on Sequoia 15.3.2 with Command Line Tools only
and a broken homebrew, so everything non-Apple lives in `research/` (gitignored):

| Path | What | Why not standard |
| --- | --- | --- |
| `research/bin/` | `bash` 5.3 (built from GNU source), `ninja` 1.13.2, `gum` 2.0.2, `cmake` 3.31.8 (+ `research/cmake/CMake.app`), `xcodes` stub | brew is broken; upstream's `xnu-build-local.sh` already puts `research/bin` first on PATH |
| `research/xcode15/Xcode.app` | Xcode 15.4, expanded from your xip | Xcode 16+ `iig` breaks xnu-8796 (IORPC ABI); selected via `DEVELOPER_DIR`, never `xcode-select`, never `/Applications` |
| `research/kdk/KDK_13.5_22G74.kdk` | Kernel Debug Kit 13.5, unpacked with `pkgutil --expand-full` (no admin prompt) | KDK must match the macOS 13.5 kernel being built |

`tools/local-toolchain.sh` sets all of that up idempotently.

## The two Apple downloads (your one manual step)

Both need your Apple ID at <https://developer.apple.com/download/all/>:

1. **Xcode 15.4** (`Xcode_15.4.xip`, ~7.7 GB) — search "Xcode 15.4"
2. **Kernel Debug Kit 13.5 build 22G74** (`KDK_13.5_22G74.dmg`) — search "KDK 13.5"

Drop both in `research/incoming/` **or** `/Volumes/HARD/ios-sailfish-incoming/`
(HARD recommended: the xip is 7.7 GB and ADATA has ~23 GB free; the *expanded*
Xcode (~13 GB) goes to ADATA either way, so delete the xip after expansion).

Then:

```sh
tools/local-toolchain.sh            # stage + verify everything
tools/xnu-build-local.sh xnu/out-local-0001                # first kernel build
```

The first build also clones xnu-8796 + dependency repos into
`research/xnu-build/` and compiles the mig/ctf bootstrap tools — expect 30–60
min on this machine; after that, incremental rebuilds after patch changes are
much faster. Kernel lands in `xnu/out-local-0001/kernel.development.sweet*`.

## Pixel 7 note

`PIXEL7=1 tools/xnu-build-local.sh <out>` targets the gs201 CPU cluster
(Cortex-X1/A78/A55, ARMv8.2+LSE) with `-march=armv8.2-a+lse` instead of the
Pixel 1's `-mcpu=kryo`. Until the Pixel 7 bring-up patches exist, both configs
still produce the upstream (Pixel 1) kernel — the flag only sets the CPU
instruction target.

## Troubleshooting

- `xip` expansion fails on disk space: free ADATA first (Xcode + build tree ≈
  17 GB steady-state).
- A patch refuses to apply after you edit `xnu/patches/`: the script rebuilds
  from pristine xnu each run, so just re-run; it refuses to ship a kernel when
  any patch was skipped (grep for `Skipping patch` in the log).
- Single-file rebuilds without a full run: `tools/xnu-cc.py` replays compile
  commands from the last CI/local log.
