# hakuX-lr

Play original Xbox games in RetroArch on **ARM64 (aarch64)** and
**AMD64 (x86-64) Linux**. This fork of
[hakuX](https://github.com/rfandango/hakuX) is included in
[PortareOS](https://github.com/portare-ch/portareos) for the Retroid Pocket Nova.

You need your own game images. **No BIOS, MCPX ROM or hard disk image is
required**; hakuX creates its storage on first launch.

## Install

<a id="play-on-portareos"></a>

### PortareOS

1. [Install PortareOS](https://github.com/portare-ch/portareos/blob/next/documentation/INSTALLATION.md),
   or update through **Settings → About → Update** and restart.
2. Copy an Xbox `.iso` or DVD `.chd` to `/storage/roms/xbox/`.
   Extract ZIP or 7z archives first.
3. Open **Xbox** in the launcher and select a game. If it is missing,
   press **Home** to return to the console list, then reopen Xbox.

To transfer games over Wi-Fi, enable **Settings → SSH** and connect with
an SFTP client. Use the IP address and password in **Settings → About**,
with username `root`.

<details>
<summary>Manually update the bundled core</summary>

Download `hakux-<version>-aarch64.tar.gz` from the
[Linux releases](https://github.com/portare-ch/hakuX-lr/releases) and extract
it. Copy `hakux_libretro.so` and `hakux_libretro.info` from `lib/libretro/`
to `/storage/cores/` on the device, creating the directory if needed, then
restart.

These files override the bundled core after OS updates too. Remove both
and restart to restore the bundled version. Back up your
[saves](#saves) before changing versions.

</details>

<a id="play-on-other-aarch64-linux-systems"></a>

### RetroArch on other Linux systems

Install a 64-bit RetroArch through your distribution. Release builds need
**Vulkan 1.3**, glibc 2.38 or newer, libstdc++ from GCC 13 or newer, and zlib.
Choose the archive for your CPU:

| CPU | Release archive | Additional requirements |
| --- | --- | --- |
| ARM64 | `hakux-<version>-aarch64.tar.gz` | 64-bit ARM Linux |
| AMD64 | `hakux-<version>-x86_64.tar.gz` | Linux 5.16 or newer and [KVM setup](#play-on-amd64-x86-64-linux-systems) |

1. Download and extract the archive from the
   [Linux releases](https://github.com/portare-ch/hakuX-lr/releases).
2. In **Settings → Directory**, find RetroArch's **Cores** and **Core Info**
   directories. Copy `lib/libretro/hakux_libretro.so` into Cores and
   `lib/libretro/hakux_libretro.info` into Core Info.
3. Set **Settings → Drivers → Video** to **vulkan**. Choose
   **Main Menu → Configuration File → Save Current Configuration**, then
   restart RetroArch.
4. Choose **Load Core → Microsoft - Xbox (hakuX)**, then **Load Content**
   and select your `.iso` or `.chd`.

Set a writable **Settings → Directory → Save Files** location. Leave
**save states, rewind and run-ahead off**; they are unsupported.

<details>
<summary>Set up a test machine with one script</summary>

`tools/linux/setup-hakux.sh` installs RetroArch with the distribution's
package manager (pacman, apt, dnf or zypper), downloads the newest release
for the CPU, checks KVM on AMD64, and gives hakuX its own RetroArch
configuration, so an existing one is left alone. It writes
`~/.local/bin/start-hakux.sh`, which starts a game by path or by part of its
name. On a desktop RetroArch opens a window; without a display server it
draws directly through KMS, as on PortareOS.

```sh
curl -fsSLO https://raw.githubusercontent.com/portare-ch/hakuX-lr/master/tools/linux/setup-hakux.sh
sh setup-hakux.sh
start-hakux.sh "burnout 3"
```

Games go in `~/roms/xbox`. Run the script again to update the core;
`--version`, `--tarball` and `--core` install a specific release or a local
build (`sh setup-hakux.sh --help`).

</details>

<a id="play-on-amd64-x86-64-linux-systems"></a>

#### AMD64: set up KVM first

The AMD64 core runs Xbox code through Linux KVM. It needs **TSC scaling**
and **UMIP** support (AMD Zen 2 or newer; Intel KVM can provide UMIP).

1. Enable **Intel VT-x** or **AMD SVM / AMD-V** in BIOS/UEFI.
2. Load the driver: `sudo modprobe kvm_intel` or `sudo modprobe kvm_amd`.
3. Check `ls -l /dev/kvm`. If its group is `kvm`, give your user access:

   ```sh
   sudo usermod -aG kvm "$USER"
   ```

   Log out and back in, then confirm this prints `KVM access OK`:

   ```sh
   test -r /dev/kvm && test -w /dev/kvm && echo 'KVM access OK'
   ```

Sandboxed RetroArch installations also need access to `/dev/kvm`. The core
logs missing CPU or KVM features when a game starts. Once KVM is ready,
follow the [RetroArch installation steps above](#play-on-other-aarch64-linux-systems).

## Game formats

Use an Xbox **XISO** or **full disc dump (Redump)** with the `.iso`
extension. DVD **CHD** images are also supported. To compress an ISO:

```sh
chdman createdvd -i "Game.iso" -o "Game.chd"
```

hakuX reads the CHD directly. Use `createdvd`; CD CHDs (`createcd`) and
CHDs that require a parent image are unsupported.

<a id="how-hakux-compares-with-xemu"></a>

## How this differs from upstream hakuX and xemu

This fork adds Linux builds, RetroArch integration and PortareOS packaging
to [upstream hakuX](https://github.com/rfandango/hakuX), an ARM64 Android
emulator. Both use hakuX's native core; the AMD64 KVM backend is an addition
in this fork. For Android, use [upstream's releases](https://github.com/rfandango/hakuX/releases).

This compares xemu's default configuration with this fork's Linux libretro
builds. ARM64 and AMD64 refer to the host CPU.

| | xemu | hakuX-lr, ARM64 (including Nova) | hakuX-lr, AMD64 |
| --- | --- | --- | --- |
| **Built on QEMU** | Yes, a QEMU fork | No; uses hakuX's native core | Same as ARM64 |
| **Machine model** | QEMU's framework extended with Xbox hardware | hakuX's memory and device model ([native/core](native/core/), [native/dev](native/dev/)) | Same as ARM64 |
| **Runs the game's x86 code with** | QEMU's TCG: software translation | [FEXCore fork](native/cpu/cpu.cpp): JIT translation from x86 to ARM64 | [KVM](native/cpu/cpu_kvm.c): execution on the host's x86 CPU |
| **Hardware virtualization** | Not used by default (TCG) | No; x86 code is translated in software | Yes; Linux KVM with Intel VT-x or AMD-V |
| **Xbox kernel** | Xbox kernel booted from a compatible BIOS | hakuX's [reimplemented Xbox kernel](native/kernel/) | Same as ARM64 |
| **BIOS / MCPX ROM / HDD image** | Required; BIOS must be debug or modified retail | Not required; storage is created automatically | Same as ARM64 |
| **Device emulation** | QEMU infrastructure plus Xbox-specific device models | Devices implemented or adapted for hakuX's native core; accesses handled in software | Same devices; KVM exits send device accesses to hakuX |
| **Graphics** | Low-level NV2A emulation rendered with Vulkan or OpenGL | NV2A push-buffer commands translated to Vulkan through hakuX's graphics HLE | Same as ARM64 |

The native core reuses components adapted from xemu, including the
[Xbox audio device](native/dev/apu/). The AMD64 build is experimental;
see its [KVM requirements](#play-on-amd64-x86-64-linux-systems).

For xemu's architecture and requirements, see its
[project overview](https://xemu.app/docs/about/),
[default CPU accelerator selection](https://github.com/xemu-project/xemu/blob/master/system/vl.c#L2299),
[required files](https://xemu.app/docs/required-files/) and
[graphics backends](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/meson.build).

## Controls

RetroArch ports 1–4 correspond to Xbox players 1–4. Face buttons follow
their Xbox positions; **L1 = White**, **R1 = Black**, and **L2/R2 = triggers**.
Remap buttons in **Quick Menu → Controls**. Enable vibration with
**Quick Menu → Core Options → Rumble**; it is off by default.

On PortareOS, **Home + Start** returns to the launcher.
**Home + the top face button** opens RetroArch's Quick Menu.

## Picture and audio

Open **Quick Menu → Core Options**. Close and reopen the game after
changing options; **Crop Overscan** and **Rumble** apply immediately.

| Setting | When to change it |
| --- | --- |
| **Render Scale** | Defaults to **2x**. Try **1x** for better performance; higher values need more GPU power. |
| **Screen Format** | Choose **Widescreen** for games that support it. Set RetroArch's aspect ratio to **Core provided**. |
| **Crop Overscan** | On by default to hide TV borders. Turn it off to show the full frame. |
| **Audio Processing** | ARM64 defaults to **Full DSP (experimental)** for Xbox audio effects. Try **Fast** if audio crackles or the game runs slowly. AMD64 defaults to Fast; its DSP JIT is unavailable. |
| **Audio Resampler** | ARM64 defaults to **Sinc** for higher quality. Try **Linear** to reduce CPU use. AMD64 always uses Linear. |

## Saves

Save inside the game, as on an Xbox. Saves are stored in `hdd/` under
RetroArch's save directory: **`/storage/saves/xbox/hdd/` on PortareOS**.
Back up that folder before moving devices or reinstalling. RetroArch save
states are unavailable.

## Multiplayer

Set **Quick Menu → Core Options → Network**, then close and reopen the game.

- **LAN (system link):** enable it on every device, then use the game's LAN
  mode. Discovery uses UDP port 3075. For another subnet or a VPN, add peers
  to `lan-peers.txt` in RetroArch's save directory
  (`/storage/saves/xbox/` on PortareOS), one `IP` or `IP:PORT` per line.
- **Xbox Live (Insignia):** register the console with
  [Insignia](https://insignia.live) using its Setup Assistant, then select this
  network mode. Account setup needs your own Xbox dashboard files;
  see the [dashboard setup](native/README.linux.md#the-libretro-core) and
  [NAT / port forwarding guide](native/README.linux.md#insignia-nat).
  Dashboard navigation and end-to-end Insignia setup still need device
  validation.

XLink Kai is unavailable in RetroArch.

## Troubleshooting

| Problem | What to try |
| --- | --- |
| Game missing from the PortareOS launcher | Check `/storage/roms/xbox/`, extract archives, and return to the console list with Home. Update PortareOS if CHDs are missing. |
| Core missing in RetroArch | Check the Cores and Core Info directories, copy both release files, then restart. |
| Slow game or black picture | Select the **vulkan** video driver and restart RetroArch. Try **Render Scale: 1x** and **Audio Processing: Fast**. |
| AMD64 game will not start | Check [KVM setup](#play-on-amd64-x86-64-linux-systems) and the log for missing TSC scaling or UMIP. |
| Game asks for a controller | Set **Quick Menu → Controls → Port 1 Controls → Device Type** to **Xbox Controller**. None disconnects the port. |
| Game fails to start | Try the latest core and an XISO. For CHD, check that it was made with `createdvd`. |

[Report an issue](https://github.com/portare-ch/hakuX-lr/issues) with the game
and region, core version, device, OS, and what happened. On PortareOS,
collect `/var/log/exec.log` and `/var/log/retroarch/retroarch.log` **before
starting another game**. See [collecting logs](TESTING.md#logs) for details.

## Development and license

[Build for Linux](native/README.linux.md) · [Testing](TESTING.md) ·
[Changelog](CHANGELOG.md)

The upstream hakuX authors made the emulator core and Android app.
This fork maintains the Linux and libretro integration.

GNU GPL v3 or later ([COPYING](COPYING)). Third-party licenses are listed
in [LICENSE](LICENSE) and [licenses/](licenses/).
