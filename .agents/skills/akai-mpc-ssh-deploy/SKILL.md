---
name: akai-mpc-ssh-deploy
description: Install or update plugins (OpenMPC Plugin Manager, SynthyBoy package) on the owner's Akai MPC Key 61 / MPC XL over Wi-Fi SSH. Use whenever the user wants something put on the Akai / MPC ("rakd fel az Akaira", "telepítsd az MPC-re", plugin manager, deploy).
---

# Deploy to the Akai MPC over SSH (Wi-Fi)

The SSH access is ALREADY SET UP (developer mode on, key login, no password). Do NOT ask the user for a USB drive,
Storage mode or an IP first, and do NOT scan the network or guess passwords. Just try these.

## Devices (from commit 14411d71, ov38 deploy)
| Device | IP | Arch | Plugin root |
|---|---|---|---|
| MPC Key 61 (`mpc-key-61`) | 192.168.0.30 | armv7 (arm32) | `/sdcard/VST2 Plugins/` |
| MPC XL | 192.168.0.126 | aarch64 (arm64) | `/nvme/VST2 Plugins/` |

The IPs come from DHCP and may change. Check first:
```sh
ssh -o BatchMode=yes -o ConnectTimeout=5 root@192.168.0.30 'uname -a; ls "/sdcard/VST2 Plugins"'
```
If it does not answer: `ping`, then ask the user for the new IP (MPC: Settings -> WiFi). Never go beyond key login.

## Install the OpenMPC Plugin Manager (MPC OS 3.10.1+, https://open-mpc.github.io/install.html)
The `-mpc-sdk.zip` is universal (contains arm32 and arm64 `.so`), so it works on both devices.
```sh
mkdir -p /tmp/openmpc && cd /tmp/openmpc
curl -fsSL -O https://github.com/open-mpc/plugin-manager/releases/download/plugin-manager-g2-v1.3.7/Plugin-Manager-G2-1.3.7-mpc-sdk.zip   # check the releases page for a newer version
unzip -o -q Plugin-Manager-G2-*-mpc-sdk.zip -d pm
cd pm && tar cf - "OpenMPC - VST - Plugin Manager" | ssh -o BatchMode=yes root@192.168.0.30 'cd "/sdcard/VST2 Plugins" && tar xf -'
ssh root@192.168.0.30 'rm -f "/sdcard/VST2 Plugins/._"*'   # macOS AppleDouble junk
```
(Use `tar | ssh`: the device has BusyBox; scp/rsync may be missing. Use `/nvme/VST2 Plugins` for the XL.)

## Install the SynthyBoy package
Build: `scripts/build-mpc.sh --arch all` (see docs/MPC.md); the folder is `Safian - VST - SynthyBoy`. Copy it the same way
into `VST2 Plugins/`. Compare md5 of the `.so` after copying. The earlier ov38 deploy also restarted `acvs`.

## After copying (user does this on the MPC)
- A new plugin appears only after the MPC application is restarted (docs/MPC.md "Known host behaviour"): restart `acvs` over SSH
  or ask the user to restart the MPC.
- Preferences -> Enhancements (Beta): Enable Enhancements + External Plugin Support must be ON (already set by the user).
- Plugin track -> plugin browser -> External Plugins -> "OpenMPC - VST - Plugin Manager".
- Plugin Manager web UI: `http://<mpc-ip>:8080` on the same Wi-Fi.

## Rules
- Only the user's own devices on 192.168.0.x; key login only, never brute force.
- Touch only `VST2 Plugins/<your folder>`; do not delete other plugins ("MPC SDK Examples", "Safian - VST - SynthyBoy").
