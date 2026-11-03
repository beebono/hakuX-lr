# Upstreaming hakuX's Linux and libretro work

Status: draft for the owner's review, 2026-10-25. Nothing here has been sent
upstream.

Goal: the emulator work done in this fork lives upstream, and PortareOS keeps
everything it relies on. Two upstreams are in view:

- **rfandango/hakuX**, the emulator's home: the Linux platform layer, the
  libretro target, the shared core changes and their tests.
- **libretro**, RetroArch's distribution: the core info file, buildbot builds
  and documentation, built from wherever the libretro target lives.

portare-ch/hakuX would then shrink to what is PortareOS's own: release
packaging and notes, or nothing at all. PortareOS's integration already lives
in portare-ch/distribution and is not affected.

## 1. Where things stand

**The fork.** 38 commits, merges included, on upstream's `98b9353111`
(2026-10-24). 74 files changed, +14,252 −227. Most of that is new: the libretro
core (`native/libretro`), the Linux front end, tests, build tools and design
documents. Upstream's own files changed little:
- portability shims (an Android-property wrapper, glibc structure sizes, a
  copy fallback for guest RAM where Linux has no AHardwareBuffer);
- the exact vblank rate and its lock to a frontend's frame;
- a shared-device Vulkan start for RetroArch;
- two kernel setters (the video region added in #14).

None of them changes what Android does.

**Upstream since.** 13 commits for app version 1.1.0: the Android app's UI, and
GPUs beyond Adreno (`b09452a5c8`, `b9ed716823`). 61 files, +3,814 −1,174. A
trial merge conflicts in `CHANGELOG.md`, `hw/xbox/hle/vk/guest_ram.c` and
`hw/xbox/hle/vk/vkdev.c`.

**Upstream as a project.** One maintainer. Issues, discussions and the wiki are
disabled. No outside pull request has been merged. Two are open without a
reply: #7 (August) and #8 (September), both about launching from external
frontends. The README presents an Android app (arm64, tuned for Adreno). The
licence is GPL-3.0-or-later, as here.

**libretro.**
- **Info files** are submitted to libretro-super (`dist/info`) and mirrored to
  libretro-core-info.
- **Builds** come from a `.gitlab-ci.yml` in the core's own tree that includes
  libretro-infrastructure's ci-templates. Dolphin, a CMake core, builds Linux
  AArch64 with `.libretro-linux-cmake-aarch64` and Android with
  `.libretro-android-cmake-arm64-v8a`: the two targets hakuX can have.
- **Xbox cores already listed:** xemu and DirectXbox, both
  `is_experimental`, with `systemid = "xbox"` and `database = "Microsoft - Xbox"`
  (the database exists). xemu's info file was added on 2026-07-04, updated on
  2026-10-12. Its core keeps one machine per process ("Restart RetroArch
  between game sessions"), so a core that can't stop is listed today.

## 2. What goes where

| Area | Files | Destination | Notes |
| --- | --- | --- | --- |
| Portability shims | `native/platform/properties.h`, `native/state/def.h` (ABI sizes), `hw/xbox/hle/vk/guest_ram.c` (copy fallback), `native/core`, `native/cpu`, `native/kernel/fixups.c` | rfandango/hakuX | No Android change; the base of everything below |
| Linux build | `native/CMakeLists.txt` options, `native/cmake/linux-*.cmake`, `tools/native/linux-deps.*`, `Dockerfile.linux`, `linux-build.sh` | rfandango/hakuX | Pinned dependencies (`linux-deps.json`) |
| Exact vblank and phase lock | `native/dev/nv2a.c`, `hw/xbox/hle/vk/display.c` | rfandango/hakuX | Generic: any frontend whose frame is the display's |
| Shared-device Vulkan | `hw/xbox/hle/vk/vkdev.c` (`hle_vk_init_shared`, frontend queue lock) | rfandango/hakuX | Goes with the libretro target |
| Console setting APIs | `native/kernel/ex.c`, `kernel.h` | rfandango/hakuX | `nc_kernel_set_av_region`; defaults keep the app's behaviour |
| libretro core | `native/libretro/*`, `native/cmake/libretro.cmake` | rfandango/hakuX, in tree as PPSSPP does; or this fork (section 6) | Section 4 lists what it still lacks |
| Tests | `tests/native/linux`, `tests/native/libretro` | With their targets | CTest; no device needed |
| Linux diagnostics, and the app's shared front-end code | `native/run/*`, `native/dev/apu/out_null.c`, the `present.c` and `gamepad.c` changes | rfandango/hakuX, optional | The Linux standalone `hakux` was dropped in 0.4.0: untested, and PortareOS no longer shipped it |
| Release packaging | `tools/native/linux-package.sh`, `BUILD-INFO`, `linux-v*` tags | This fork, or portare-ch/distribution | What `hakux-lr` pins today |
| Design documents | `SPEC.md`, `LIBRETRO_SPEC.md` | rfandango/hakuX, without the PortareOS specifics | Their device sections stay here |
| PortareOS notes | `PORTAREOS_HANDOFF.md`, `README.md`'s PortareOS text, `CHANGELOG.md`'s `linux-v*` sections | This fork | Upstream gets a "Linux and RetroArch" section in its README; the `HANDOFF.md` edits are reverted |
| PortareOS integration | `hakux-lr`, the Xbox system entry, the RetroArch override (save directory `/storage/saves/xbox`, run-ahead off), `runemu.sh`'s VRR, the Mesa, kernel and RetroArch patches | portare-ch/distribution, as now | Unaffected by any of this |

## 3. PortareOS's features and how each survives

| Feature | Where it lives | Upstream, it becomes |
| --- | --- | --- |
| VRR, timed presents and the kernel's LFC (#8) | portare-ch/distribution | Nothing in hakuX: the core only needs its phase lock, which is generic |
| The Xbox's exact 60000/1001 Hz, one frame per `retro_run` | The core | Unchanged; it suits every RetroArch |
| The overscan crop that fills a 4:3 panel | `native/libretro/video.c` | A core option ("Crop overscan", on by default), so a TV can keep the full frame |
| The console disk and caches in `/storage/saves/xbox` | RetroArch's save directory, set by PortareOS's override | Unchanged: the core uses whatever save directory the frontend gives |
| Run-ahead off | `savestate = "false"` and the override | Unchanged |
| RetroArch ends on unload, and when RetroArch drops the Vulkan context; a relaunch starts RetroArch again (#11) | `native/libretro/core.c` | Replaced by M2's stop in process. PortareOS's build option `HAKUX_LR_END_PROCESS` keeps them behind `HAKUX_LR_STOP=0` for one release, and ends the process when a stop hangs (10 s). The relaunch's re-exec needs RetroArch's own binary with the game on its command line; elsewhere, Android included, it falls back to ending the process |
| Core options (#14) | `native/libretro/options.c` | Unchanged; PortareOS's per-game `.opt` merge in `runemu.sh` keeps working |
| A release tarball with a pinned checksum | This fork's releases | Either `hakux-lr` builds upstream's source at a pinned commit, as other PortareOS cores do, or this fork keeps cutting tarballs from upstream's tree |

## 4. What each destination still needs

### rfandango/hakuX

- **Rebase onto upstream's master:** three conflicts today, more the longer we
  wait, given upstream's pace. Merge upstream regularly from now on.
- **Pull requests a maintainer can review**, in the order of section 5, each
  building on Android. We have never built Android; macOS can run the NDK, and
  each pull request should say whether it built.
- **No PortareOS in shared code.** The libretro gate in `native/CMakeLists.txt`
  ("The libretro core is for Linux (PortareOS)") becomes Android and Linux. The
  comments that cite PortareOS, the Nova or InputPlumber in `core.c` become
  general or move to this fork's notes.
- **A way in.** With issues disabled, the first contact is a small pull request
  (the portability shims) that explains the series. Given #7 and #8, set a time
  limit; section 6 is the fallback.

### libretro

- **Stopping the core (M2).** libretro expects `retro_unload_game` to free the
  game so `retro_load_game` can run again. The core now stops in RetroArch's
  process (LIBRETRO_SPEC section 6), on Linux; not yet built or run on Android.
  Before, it ended RetroArch, which on Android closes the RetroArch app. It
  also lets PortareOS drop #11's workarounds.
- **No process-wide side effects.** `retro_load_game` sets `HAKUX_HLE_MODE`
  and `HAKUX_NATIVE_BASE` with `setenv`; these become API calls.
- **Android in the core build.** The CMake gate goes. The core then builds
  with the NDK, without SDL or JNI, on RetroArch's Vulkan device.
  - **No custom Turnip:** RetroArch owns the device, so adrenotools can't load
    one. Phones on Qualcomm's Vulkan 1.2 drivers run the core on its
    compatibility path, which can be slower (upstream's app recommends Turnip
    for them).
  - **Guest RAM:** AHardwareBuffer sharing works in any app process.
- **libretro's CI.**
  - A `.gitlab-ci.yml` with `linux-cmake-aarch64` and
    `android-cmake-arm64-v8a`.
  - FEX needs clang, and the templates' images default to GCC 14: an image or
    a `CC` override has to be found.
  - The dependencies `linux-deps.py` downloads at build time become
    submodules or vendored sources.
  - The Linux core links against an older glibc than Debian's, for the
    buildbot's users.
- **The info file.**
  - `systemid = "xbox"` instead of `microsoft_xbox`, to match libretro's
    database, playlists and thumbnails.
  - `core_options = "true"` with `core_options_version = "2.0"`: #14 left
    `"false"`.
  - `notes` for ARM64 only, Vulkan 1.2 (1.3 recommended), no BIOS or hard
    disk image.
  - Submitted to libretro-super.
- **A docs page** in libretro's docs: requirements, options, the data folder,
  what's missing.

## 5. Order of work

| # | Step | Where | Ready when |
| --- | --- | --- | --- |
| 0 | Fixes in this fork: the info file; PortareOS-only behaviour behind one build option; `setenv` replaced; overscan option; an Android build of the fork | portare-ch/hakuX | Container tests pass and the Android build works; on the Nova, PortareOS's package behaves as today |
| 1 | Merge upstream's 1.1.0 into the fork, then keep merging | portare-ch/hakuX | No conflicts left; Burnout 3 and BLACK as before on the Nova |
| 2 | Pull request: portability shims and the Linux build | rfandango/hakuX | Upstream reviews it, or the time limit passes |
| 3 | Pull request: exact vblank, shared-device Vulkan, console setting APIs | rfandango/hakuX | Android unchanged in upstream's own checks |
| 4 | Pull request: the libretro target, its tests, the Linux docs | rfandango/hakuX | CTest in upstream's tree |
| 5 | Pull request, optional: the Linux diagnostics | rfandango/hakuX | Only if upstream wants them |
| 6 | M2: stop, unload, load again, reset | Wherever the core lives | LIBRETRO_SPEC's M2 gate: 100 load/run/unload cycles in one process, nothing left behind |
| 7 | libretro submission: CI file, info file, docs page | libretro-super, libretro's docs, the core's tree | The buildbot builds Linux AArch64 and Android arm64 |
| 8 | PortareOS on the upstream source | portare-ch/distribution | `hakux-lr` builds or fetches the upstream core with the PortareOS build option, at a pinned commit |

Steps 0, 1 and 6 help PortareOS on their own and need nobody outside.

Progress, 2026-10-25: steps 1 and 0 are on the branches
`portareos/upstream-1.1.0` (the merge) and `portareos/upstream-prep` (the
fix-ups), not merged yet. Both build for Android with NDK r29. Step 6 (M2)
is on `m2-stop`. A video driver rebuild mid-game (`VIDEO_REINIT`, which
PortareOS's RetroArch has) crashed RetroArch until the core stopped letting
it cache the context.

## 6. If upstream doesn't take it

This fork becomes the libretro core's home, as many libretro cores are forks
that follow their emulator:
- merge upstream regularly, and keep the shared-code changes small and rebased;
- send upstream only what it asks for;
- the libretro CI builds from this repository, and the info file credits
  upstream's authors.

PortareOS is unchanged either way.

## 7. Decisions for the owner

1. **The libretro target's home:** upstream's tree (the aim), or this fork
   from the start.
2. **M2's priority:** needed for general RetroArch, Android especially, though
   PortareOS gains nothing from it on its own.
3. **The Linux standalone `hakux`:** dropped (2026-10-25), as it wasn't
   tested.
4. **First contact:** who approaches upstream, and how, with issues
   disabled.
5. **`systemid`:** change it to `"xbox"` now. Neither portare-ch/distribution
   nor the launcher reads it.
