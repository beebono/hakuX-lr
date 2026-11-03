# Testing the hakuX core

The RetroArch core, `hakux_libretro.so`, is tested on the Retroid Pocket
Nova with PortareOS (Snapdragon 8 Gen 2, Adreno 740). The ROCKNIX community
has also tested the build on other devices.

## Before you start

- **Games:** use your own XISO or full disc dump (Redump), as `.iso` or DVD
  CHD. Convert with `chdman createdvd -i game.iso -o game.chd`. CD CHDs
  and CHDs that need a parent image are unsupported.
- **No BIOS** or other Xbox files are needed.
- **Back up game saves** before changing versions: the `hdd/` folder in
  RetroArch's save directory
  (`/storage/saves/xbox/hdd` on PortareOS).
- **Record the core version:** **Main Menu → Information → Core Information**.

## On PortareOS

### Playing

Put games in `/storage/roms/xbox`. The launcher's Xbox system starts them in
RetroArch with hakuX.

### Update the core manually

To try a release before PortareOS ships it:

1. Download `hakux-<version>-aarch64.tar.gz` from the
   [releases](https://github.com/portare-ch/hakuX-lr/releases), and check it
   against its `.sha256` file.
2. Copy `lib/libretro/hakux_libretro.so` and `lib/libretro/hakux_libretro.info`
   from it into `/storage/cores`.
3. Restart. These files override the bundled core, including after OS updates.

To restore the bundled core, remove both files from `/storage/cores` and
restart. Removing files from `/tmp/cores` also hides the bundled core.

### Logs

Copy both logs after the problem, before starting another game overwrites them:

- `/var/log/exec.log`: core output, with lines prefixed `hakuX-`.
- `/var/log/retroarch/retroarch.log`: RetroArch output.

For frame rates and core timings, launch over SSH with statistics enabled.
`exec.log` reports them every 10 seconds (`stats: 59.9 new frames/s`,
`retro_run ... ms each`):

```sh
systemctl stop portarelauncher
HAKUX_DISP_STATS=1 HAKUX_LR_STATS=1 runemu.sh "/storage/roms/xbox/<game>.iso" \
  -Pxbox --core=hakux --emulator=retroarch --controllers=
systemctl start portarelauncher
```

## On other ARM64 Linux systems

### Requirements

- **System:** ARM64 (aarch64) Linux with glibc 2.38 or newer, libstdc++ from
  GCC 13 or newer, and zlib.
- **GPU driver:** Vulkan 1.3. Tested with Mesa Turnip on Adreno 740.
  Vulkan 1.2 drivers use a compatibility path, which can be slower:
  the core's log then says `compat: Vulkan below 1.3`, or names the missing
  feature if the driver can't run the core.
- **Vulkan loader:** `libvulkan.so.1` 1.3 or newer, even with a 1.2 driver.
  The core asks for a Vulkan 1.3 instance, and RetroArch refuses it on an
  older loader: `Core requests apiVersion 1.3, but it is not supported by
  loader`.
- **RetroArch:** with its `vulkan` video driver. 1.22.2 as PortareOS builds it
  is tested. Other video drivers copy frames through the CPU and are intended
  for diagnostics.

### Installing

1. From the release tarball, copy `lib/libretro/hakux_libretro.so` into
   RetroArch's cores directory (Settings → Directory → Cores).
2. Copy `hakux_libretro.info` into its core info directory (Settings →
   Directory → Core Info).

Or load it directly:

```sh
retroarch -v -L /path/to/hakux_libretro.so "/path/to/game.iso" 2>&1 | tee hakux.log
```

The core stores game saves in `hdd/` and shader/pipeline caches in `cache/`
under RetroArch's save directory. Save states, rewind and run-ahead are
unsupported; leave them off.

Release builds exit RetroArch if shutdown takes over 10 seconds.
`HAKUX_LR_STOP=0` restores the pre-0.4.0 behavior: closing a game always
exits RetroArch.

### Logs

Start RetroArch from a terminal, as above. The core's lines start with
`hakuX-`. `HAKUX_DISP_STATS=1` and `HAKUX_LR_STATS=1` in the environment add
the frame rate and the core's time per frame.

## What to check

1. **Boot and play:** reach the menus and play with sound and controller 1.
2. **Controllers:** test players 1–4. In **Quick Menu → Controls → Port N
   Controls**, set **Device Type** to **None** to disconnect a controller,
   then reconnect it. Most games pause when player 1 disconnects.
3. **Speed:** check smooth play and frame statistics: about 59.9 new frames/s
   for a 60 fps game, or 30 for a 30 fps game.
4. **Restart:** **Quick Menu → Restart** should restart from the beginning
   with sound. Direct RetroArch confirmation is still needed; report the result.
5. **Close Content:** check that it closes without hanging. PortareOS should
   return to the launcher. Elsewhere, load another game in the same session.
6. **Core options:** Render Scale, Screen Format, Language, Video Region,
   Network and Audio Resampler apply after restarting the game. Crop
   Overscan and Rumble update immediately.
7. **Game saves:** save in-game, close it, then reopen and load the save.
8. **Game executable changes:** compilations and demo discs should continue
   when launching another program, without returning to the frontend.
9. **Video driver restarts:** changing a setting that rebuilds RetroArch's
   video driver should restart the game and show a message.
10. **System link** (two devices): set **Network** to **LAN (system link)** in
    the core options on both, start a game with system link on both, and open
    its system link menu. Each should see the other's game. Record the network
    setup and any `lan-peers.txt` entries.
11. **Audio:** compare **Sinc** and **Linear** in the same scene, restarting
    after each change. Check stereo placement, quiet or distorted sounds,
    short effects and crackling. Record the game and scene.

## Reporting

Open an [issue](https://github.com/portare-ch/hakuX-lr/issues) with:

- hakuX and RetroArch versions;
- device, GPU, driver/Mesa version, and OS version;
- game, region, and image type (XISO/full dump, ISO/CHD);
- steps to reproduce, actual result and expected result;
- logs.

Don't attach games or other copyrighted files.

## For developers

After the [Linux build](native/README.linux.md#build-and-check), run the
network regression test with:

```sh
ctest --test-dir build/linux -R '^nat-smoke$' --output-on-failure
```

`nat-smoke` builds console Ethernet frames and uses a fake UPnP router on
127.0.0.1, leaving the real router untouched. It checks outgoing and
unsolicited incoming UDP 3074, busy ports, UPnP disabled, relative control
URLs and preserved queries, permanent leases, renewal failures and retries,
and shutdown during delayed replies. Cleanup checks cover another console's
mapping and failed ownership queries.

For tests without a device, see
[Build and check](native/README.linux.md#build-and-check). The libretro
harness loads the core as RetroArch does and can play a real game headless:

```text
libretro-harness [-c <config>] [-s <save directory>] [--menu] (<core.so> | -L <core.so>) [<disc image or -> <frames> <save directory>]
```

`-c/--config` is accepted but not read; `-s/--save` overrides the save
directory. `--menu` is accepted but ignored. `-` loads no content (Start Core).
To simulate **Load Core → Start Core**, omit the core argument and set
`HARNESS_MENU_CORE=<core.so>`. With that variable or `-L` and no content
arguments, set `HARNESS_FRAMES` and supply a save directory via `-s` or
`HARNESS_SAVES` (used only when `-s` is absent).

Other variables are described at the top of
[harness.c](tests/native/libretro/harness.c): `HARNESS_VULKAN`,
`HARNESS_CYCLES`, `HARNESS_RESET_AT`, `HARNESS_REINIT_AT` and others.

Dashboard relaunches with `HAKUX_LR_STOP=0` still need checking in real
RetroArch on a device: starts with `-s <directory>`, **Load Core → Start Core**,
and `-L <core> --menu`. Confirm that restarts keep the save directory.

### Drivers below Vulkan 1.3

A Vulkan 1.3 GPU can run the renderer's compatibility path, which Vulkan 1.2
drivers use. It replaces dynamic rendering with render
passes and synchronization2 with Vulkan 1.0's barriers and submits
([compat.c](hw/xbox/hle/vk/compat.c)). There are two ways to test it:

- **In the core:** `HAKUX_HLE_CAPS=-vk13,-dynrender,-sync2,-eds1,-eds2`
  turns those features off. `libretro-smoke` checks a reset and a video
  driver rebuild this way.
- **For the whole process:** the Khronos Profiles layer
  (`VK_LAYER_KHRONOS_profiles`, in the `vulkan-profiles` package on Arch)
  makes the GPU report Vulkan 1.2 to RetroArch and the core, and hides the
  1.3 extensions:

  ```sh
  export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_profiles
  export VK_KHRONOS_PROFILES_PROFILE_DIRS="$PWD/tools/native/vk-profiles"
  export VK_KHRONOS_PROFILES_PROFILE_NAME=VP_HAKUX_vk12
  export VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES=SIMULATE_API_VERSION_BIT
  export VK_KHRONOS_PROFILES_EMULATE_PORTABILITY=false
  export VK_KHRONOS_PROFILES_EXCLUDE_DEVICE_EXTENSIONS=VK_KHR_dynamic_rendering,VK_KHR_synchronization2,VK_EXT_extended_dynamic_state,VK_EXT_extended_dynamic_state2,VK_EXT_extended_dynamic_state3,VK_KHR_dynamic_rendering_local_read
  ```

The core then logs `vk: device "..." api 1.2.0` and `compat: Vulkan below
1.3, render passes, 1.0 barriers and submits`. Add
`VK_LAYER_KHRONOS_validation` in front of the profiles layer, with
synchronization validation on, to check the path. The layer only changes
what the GPU reports: it doesn't reproduce another driver's bugs or speed.

### Is it FEX? The same game under KVM

Use an x86-64 Linux machine to compare the same game under KVM and under
FEX on the Nova. KVM runs the game's code on the host CPU; the kernel,
devices and renderer stay the same.

**Requirements:** x86-64 Linux with `/dev/kvm`, TSC scaling, UMIP (AMD
Zen 2 or newer; on Intel, KVM provides UMIP itself), and a Vulkan 1.3 GPU.
Apple's `container` on a Mac can build it but cannot run KVM.

**Setting up KVM:**

1. **Turn virtualization on in the firmware.** In the BIOS/UEFI setup,
   enable **Intel Virtualization Technology** (VT-x) on Intel or
   **SVM Mode** (AMD-V) on AMD.
2. **Load the KVM module:** `sudo modprobe kvm_intel` on Intel or
   `sudo modprobe kvm_amd` on AMD.
3. **Give your user access to `/dev/kvm`.** Check its group with
   `ls -l /dev/kvm`, usually `kvm`. Run `sudo usermod -aG kvm $USER`,
   then log out and back in.
4. **Install the core** from the `x86_64` release archive using the
   [AMD64 installation guide](README.md#play-on-amd64-x86-64-linux-systems).
   To build, test and package a clean checkout instead, run
   `sh tools/native/linux-ci-x86_64.sh 0.17.0`. Run
   `build/linux-x86_64/kvm-smoke`; it should end with
   `PASS: 0 failed`. The core is `build/linux-x86_64/hakux_libretro.so`.

**Compare like with like:**

1. Use the same disc image, core options and RetroArch settings on both.
   For sound problems, set **Audio Resampler** to **Linear** and
   **Audio Processing** to **Fast** on the Nova, and check the problem
   still occurs before comparing.
2. Transfer progress using in-game saves: copy the `hdd/` folder from
   RetroArch's save directory (`/storage/saves/xbox/hdd` on PortareOS) to
   the other machine's. Savestates don't cross between FEX and KVM.
3. Play the same scene on both and note what differs.

**Reading a difference:**

- **Works under KVM, fails under FEX:** rule out audio settings and GPU
  driver differences first. Then try FEX's switches on the Nova, set in
  RetroArch's environment: `NC_FEX_X87=80`, `NC_FEX_PC=0`, `NC_FEX_AFP=0`.
- **Fails on both:** it's the kernel, a device or the renderer.
- **Fails under KVM only:** a KVM backend bug.
