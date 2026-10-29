# Halo CE PS3: PS Move and stereoscopic 3D patch

Adds PS Move motion aiming, Navigation-controller support and stereoscopic 3D
to [Mayo1970/halo-ce-ps3](https://github.com/Mayo1970/halo-ce-ps3).
**The latest Fix 7 build is confirmed working by the user on PS3.** The earlier
campaign-loading issue is no longer reported for that build. This confirmation
does not establish that every stereo mode or PS Eye setup has been tested.

[Download the latest PKG](https://github.com/kevums/halo-ce-ps3-stereo-move-patch/releases/latest).
This repository contains the patch, changed/new source and Windows source-patch
manager. It does not include game maps, disc images or saves.

## Install and enable

1. Install the latest PKG with the PS3 Package Manager. It uses title ID
   HALOCE000 and replaces the existing Halo executable. Keep a backup of your
   previous executable.
2. Provide your own Xbox Halo maps in `/dev_hdd0/data/HaloCE/maps/` or
   `HaloCE/maps/` on USB, following upstream installation instructions.
3. Hold **Select + Start** to open extension settings. Use the D-pad to select
   and adjust; X runs actions; Circle saves and closes.
4. Set `move_enable=1`, save, and restart Halo. Pair Move and Navigation by USB,
   then press PS. Gyro is the default (`move_camera=0`); a PS Eye is optional.
5. For 3D, set `r_stereo=1`, choose `r_stereo_mode`, save, and restart. Select
   the matching format on a compatible 3D display.

Settings persist in `/dev_hdd0/data/HaloCE/extensions.cfg`. Installing the PKG
does not replace external maps, profiles, saves or configuration files.

## How motion control works

The Move's gyroscope supplies look/aim rotation. Sensitivity, drift deadzone,
vertical inversion and optional gestures are adjustable. Navigation supplies
movement and buttons; `move_nav_port=0` selects automatically, while1-7 select
an explicit pad port. Check `move_status` and `move_sensor` for diagnostics.

Independent aim (`move_freeaim=1`) moves the reticle within an adjustable range
and uses a configurable camera-turn/clutch action. Turn gain, button selection
and hold/release behavior are adjustable. The HUD hook converts Halo's16-bit
coordinates through float locals to avoid the earlier incorrect-width write.

Default Move buttons: T fires; Move switches grenades; Square reloads/uses;
Cross jumps; Circle melees; Triangle switches weapons. Bindings are adjustable.
`move_blue_orb=1` requests a blue orb while in use. `move_camera=1` requests PS
Eye tracking and a compatible trackable hue; gyro mode needs no camera.
Optional shake and flick-turn settings are also included.

## How stereo works

The renderer draws separate left/right eye views and combines them into the
selected output format:

| r_stereo_mode | Output |
|---|---|
|1|Side-by-side|
|2|Over/under|
|3|HDMI720p frame packing|
|4|Frame sequential|

`r_stereo_depth` controls eye separation (default0.5, range0-3),
`r_stereo_convergence` sets zero-parallax distance in game world units
(default10), and `r_stereo_swap=1` swaps the eyes. Stereo enable and mode require
a restart. Stereo doubles rendering work, uses the PPE Direct3D path and disables
dynamic resolution; performance depends on the scene and display format.

## Patch source / future upstream versions

The current source patch targets upstream commit
`f105a8e5a328cbe5e0768d6e73601ddaf0442c31`:

```
git apply --check /path/to/halo-stereo-move.patch
git apply /path/to/halo-stereo-move.patch
```

On Windows, run Launch Extension Manager.cmd with Python/Tk and Git installed.
It checks source compatibility before applying; it patches source, not a PKG.
Later upstream revisions may need a refreshed patch. Installed extension files
document compiled features; they are not runtime-loadable plugins.

`source/` mirrors changed/new files only; a full upstream checkout is required
to build. The release also supplies the full source archive corresponding to its
PKG. The release PKG was built against the earlier e76d50c3 base; this repository's
patch additionally accommodates newer upstream source. Full NSPS3 parity remains
incomplete. See the extension README/NOTICE and root LICENSE for provenance.
