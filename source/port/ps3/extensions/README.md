# Experimental stereo and PS Move extension

Work in progress based on Mayo1970/halo-ce-ps3 commit
e76d50c304c425a912c49a973cfd908d9ecbab85.

**Known blocker: campaign loading with Move enabled freezes on the test PS3.**
The latest hardware capture stops at HUD marker63500 around the Move reticle
call. A signed16-bit HUD coordinate / float-pointer mismatch has been repaired,
but that build has not resolved the reported freeze. The installed executable's
identity still needs verification. Do not treat the mock tests as hardware proof.
Stereo enabled with Move disabled was reported to load the same campaign.
Full NSPS3 feature parity is incomplete.

## Settings

Hold Select+Start for the extension menu. Use the D-pad to adjust settings,
X for actions, and Circle to save and close. Settings are stored in
`/dev_hdd0/data/HaloCE/extensions.cfg`; the installed settings.json documents
compiled settings rather than loading code plugins.

`move_enable=1` enables Move after restarting. Default is0. `move_camera=0`
selects gyro;1 requests optional PS Eye optical tracking. Pair Move/Nav by USB
and press PS. `move_blue_orb=1` requests blue while in use, using a compatible
trackable hue for camera mode. Check move_status, move_sensor and move_orb_status.
The removed move_enabled alias is supported only for migrating old configs.

`r_stereo=1` enables stereo after restarting. `r_stereo_mode` selects:

1. Side-by-side
2. Over/under
3. HDMI720p frame packing
4. Frame sequential

The schema lists separation, convergence, eye swap and other adjustments.
These modes require further physical display validation. Stereo renders twice,
forces the PPE Direct3D path and disables dynamic resolution.

## Build and install

Use the upstream PSL1GHT build environment. Build/sign sequentially:

```
./build.sh -j12 BUILD=build/extensions pkg check
./build.sh BUILD=build/extensions self
```

The package uses title ID HALOCE000 and replaces the installed game executable.
Keep backups of the previous executable. Supply your own game maps as described
in the root README; no ISO, maps, saves or profiles are included here.

For UDP diagnostics, put the receiving PC's IPv4 address in
`/dev_hdd0/data/HaloCE/log_ip.txt`, then restart. UDP port18194 is used. The
public-source default is loopback; configure a destination before collecting.
HUD markers633xx/634xx indicate screen/HUD progress;63500/63501 surround the
reticle hook. Heartbeat wait addresses record LAST waits, not active stacks.

## Validation and provenance

Build/link checks, extracted-package comparison, source-patch compatibility,
and host sanitizer regressions passed for local preview builds. Hardware freeze
resolution remains unconfirmed. Tests are tools/ps3_extensions_math_test.c,
tools/ps3_extensions_settings_test.py and tools/ps3_move_loading_test.py.

Camera, gyro and gesture implementations derive from NSPS3's PS3 platform code,
reference commit09af768d29da1de50958d5ae4b2ebf6abbde6775 (with local working-tree
changes). Integration follows this repository's license. See NOTICE.txt and the
root LICENSE. No claim of upstream endorsement or complete compatibility.
