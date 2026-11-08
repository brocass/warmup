# Warmup

![Warmup preparing songs for Sing](screenshot.png)

Warmup prepares [Narjo Sing](https://github.com/ayumpo/narjo-sing-helper)
stems ahead of time. Drop songs, folders, or playlists onto the window and
your self-hosted helper separates each song into vocals and instrumental in
the background — so the next time you tap the microphone in Narjo, the song
starts in Sing within a second instead of separating for an hour.

## What you need

- A running narjo-sing-helper on your network (see its README to set one up).
- The helper's **address** (`http://YOUR-SERVER-IP:8765`) and **access key**
  (saved by the helper in `stems/access-key.txt` next to its compose file).
- The local path to **the same music folder** the helper watches as `/music`,
  so dropped songs can be matched to the helper's library.

On first launch Warmup asks for these three things; they live in
`~/.config/warmup/config.json`. The quality menu maps to the helper's
`quality` setting: **Best** (the default) is what you want for preparing
karaoke tracks in advance.

## Using it

- Drag songs, folders, or `.m3u`/`.m3u8`/`.pls` playlists anywhere onto the
  window. Warmup reads each file's tags with `ffprobe` and submits the batch
  to the helper at `batch` priority.
- The list mirrors the helper's queue with live progress, and below it the
  songs that recently became ready.
- Songs outside your music folder are matched by file name and tags; unique
  file names work best.

### Shortcuts

| Key   | Action                        |
| ----- | ----------------------------- |
| Space | Pause or resume the queue     |
| X     | Cancel the selected song      |
| ,     | Open settings                 |
| ?     | Show or hide the shortcut help |
| Q     | Quit                          |

## Development

```sh
bin/build    # qmake6 + make into build/warmup
bin/test     # offscreen Qt Test suite
bin/install  # makepkg -fsi: installs binary, launcher entry, and icon
```

C++ owns the helper API, settings, and metadata (`src/backend.cpp`,
`src/settings.cpp`, `src/metadata.cpp`); QML owns layout and keys
(`src/Main.qml` and the overlays). The window follows the Omarchy accent and
Hyprland's window rounding live, and every visible string goes through
`qsTr()`/`tr()` with JSON catalogs under `src/i18n/`.

## License

MIT. Theme and translation plumbing adapted from Omacut and Monologue (MIT,
David Heinemeier Hansson) — see LICENSE. Warmup talks to the
narjo-sing-helper over its local HTTP API and is not affiliated with Narjo.
