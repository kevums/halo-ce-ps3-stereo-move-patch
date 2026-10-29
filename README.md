# Halo CE PS3: experimental PS Move and stereoscopic 3D patch

This repository contains only our patch, its changed/new source files, and the
Windows source-patch/settings manager. It is not a fork or a full game checkout.
No maps, ISO, saves, logs, or compiled game packages are included.

**Known blocker: campaign loading with Move enabled still freezes on the test PS3.**
The reticle coordinate type mismatch is repaired in source, but the reported
freeze persists and installed-binary identity still needs verification. This is
unfinished development work, not a confirmed working release.

## Apply

Obtain Mayo1970/halo-ce-ps3 at commit
`f105a8e5a328cbe5e0768d6e73601ddaf0442c31`, then run in that checkout:

```
git apply --check /path/to/halo-stereo-move.patch
git apply /path/to/halo-stereo-move.patch
```

On Windows, Launch Extension Manager.cmd opens the Python/Tk source patch and
settings manager. Python and Git must be installed. It checks compatibility
before applying and refuses conflicting source. This patches source, not a PKG.

`source/` mirrors only files changed or added by the patch; use a complete
upstream checkout to build. See source/port/ps3/extensions/README.md for settings,
build instructions, diagnostics and current limitations. Full NSPS3 parity and
physical validation of stereo modes remain incomplete. License: root LICENSE;
reference-derived files and attribution are described in the extension NOTICE.
