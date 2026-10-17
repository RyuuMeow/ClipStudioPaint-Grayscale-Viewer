# Grayscale Viewer icons

The user-supplied `grayscale_viewer_svg_pack.zip` was moved here from Downloads. Its SVG sources and PNG exports are preserved unchanged.

- `grayscale-viewer.ico`: application and installer icon, using the full background tile.
- `grayscale-viewer-tray.ico`: notification-area icon, using the transparent symbol.

The ICO files contain 16, 20, 24, 32, 40, 48, 64, 128 and 256 px frames. Regenerate them from the supplied PNG exports with `scripts/build-icons.ps1` on Windows. They are embedded at build time, so the executable needs no external image files or SVG renderer.
