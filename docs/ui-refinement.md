# UI refinements

## Bar and history

- Clipboard images now appear **inside history entries**, not in a separate
  preview pane. Visible delegates request bounded, memory-only thumbnails;
  copying still decodes the original bytes afresh. Closing releases the cache.
- Clipboard is enabled and placed on the bar by default. Existing explicit
  module lists and `enabled=false` choices are retained; move Clipboard into a
  panel slot in settings to place and enable it.
- Album covers use aspect-fit, showing the entire cover without cropping.
- Workspace numbers and their underline share the same horizontal center.
  Three-zone layouts center on the actual bar viewport, not an oversized scroll
  canvas; side groups scroll independently when too long.
- Module dropdowns have no header X. Escape, source-button toggle and native
  outside dismissal still apply. Action/navigation glyphs use SVG assets, not
  font symbols. Application-supplied tray/notification images remain supported.

New TOML fields (all apply on reload):

```toml
[modules.clipboard.behavior]
inline_image_height = 160 # integer 48..512 logical pixels
inline_text_lines = 8     # integer 2..64, expanded selected text
preview_cache_items = 24  # integer 1..128, maximum memory thumbnails

[modules.media.style]
opacity = 1.0 # number 0..1; available independently on EVERY module
```

`style.opacity` affects that module's content and dropdown as a whole; theme
background opacity remains a separate setting. Each `[[panels]]` also accepts
`window_gap = 0` (integer -256..256). It adjusts the layer-shell reservation,
not the dropdown margin. Negative values can compensate Niri's own layout gaps
without modifying Niri configuration; extreme negative values can overlap tiled
windows. `exclusive_zone=0` still explicitly means no reserved screen area.

Implementation references: Qt Quick Image PreserveAspectFit, Drag/DropArea,
https://doc.qt.io/qt-6/qml-qtquick-drag.html .

Delivery validation: compilation only, as requested; no CTest or live UI run.
