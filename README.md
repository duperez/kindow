# Kindow

*[Versão em português](docs/README.pt-BR.md)*

Kindow turns a jailbroken Kindle into a wireless touchscreen for a Raspberry Pi, over
VNC. The Pi's desktop is rendered on the Kindle's e-ink display, and touch input is
sent back as mouse and keyboard events. Screen updates are strictly on-demand — the
server only transmits when content changes, which is both how the RFB protocol works
and what an e-ink panel requires.

<p align="center">
  <img src="docs/images/kindow-photo.jpg" alt="A Kindle on a stand, running Kindow: the Pi's Mousepad editor with 'Hello from Kindow' typed on the on-screen keyboard" width="420">
  &nbsp;&nbsp;
  <img src="docs/images/kindow-photo-files.jpg" alt="The same Kindle browsing the Pi's filesystem with the PCManFM file manager" width="420">
</p>

<p align="center">
  <img src="docs/images/session.png" alt="Framebuffer capture of a Kindow session: the Pi's desktop with Mousepad, taskbar and the on-screen keyboard" width="380">
  &nbsp;&nbsp;
  <img src="docs/images/connect.png" alt="Framebuffer capture of the Kindow connection screen: saved Pi list, add-new button, and the bottom bar" width="380">
</p>

*Top: photos of the device. Bottom: framebuffer captures, pixel-identical to what the
display shows.*

**Status**: working end to end on real hardware (Kindle KT5 + Raspberry Pi 4).

## Features

- **Interactive remote desktop** — the Pi's X session (Openbox, tint2, GTK
  applications) rendered 1:1. The remote resolution adapts automatically to the
  connecting Kindle's screen via the RFB `SetDesktopSize` extension; no scaling or
  cropping.
- **Touch input** — a tap is a left click. The keyboard's symbols page provides
  dedicated Left-click (press-and-drag, released when the finger lifts) and
  Right-click keys.
- **On-screen keyboard** — sticky Shift/Ctrl modifiers (chords such as Ctrl+C work
  without multi-touch) and a symbols page.
- **Persistent bottom bar** — scroll up/down with an adjustable step, keyboard toggle,
  and menu.
- **Menu** — remote zoom in three independent layers (application content via Xft/DPI,
  window decorations, panel), disconnect, connection status, quit.
- **Connection manager** — history of previously used servers, a form to add new ones
  (IP, port, password), classic VNC authentication, and explicit error reporting on
  failed connections.
- **Library launcher** — starts with a tap from the Kindle's library; SSH is only
  needed for installation.

## Requirements

- A **jailbroken Kindle** with scriptlet support (a tappable `.sh` in the library, the
  standard mechanism of the [current jailbreak](https://kindlemodding.org/)). Tested on
  a KT5 (1072×1448). The layout is proportional and should adapt to other resolutions,
  but no other model has been validated.
- A **Raspberry Pi** — or any Debian-like Linux with `systemd` — on the same network,
  reachable over SSH.
- To build the client: the KindleModding cross-compilation toolchain (koxtoolchain +
  KMC SDK) in a container. See "Building the client".

## Installation

### Server (Pi)

```bash
scp -r pi/ pi@<pi-ip>:/tmp/kindow-pi && ssh -t pi@<pi-ip> 'bash /tmp/kindow-pi/install.sh'
```

[`install.sh`](pi/install.sh) is idempotent. It installs the required packages
(TigerVNC, Openbox, tint2, mousepad, xsettingsd), applies the session configuration
without overwriting user customizations, enables the two services (`vnc-kindle` on port
5901, `kindow-helperd` on port 5910) and verifies that both respond.

### Client (Kindle)

With the binary cross-compiled (see below) and root SSH access to the Kindle:

```bash
./kindle/deploy.sh <kindle-ip>
```

This copies the binary and `libvncclient` to `/mnt/us/kindow/`, installs the
[`kindle/kindow.sh`](kindle/kindow.sh) scriptlet into `/mnt/us/documents/` — it appears
as a tappable "Kindow" item in the library — and launches the application.

### Building the client

The client is written in C against GTK2 (the toolkit shipped in the Kindle firmware)
and cross-compiled with Meson inside a container providing the KindleModding toolchain.
Follow the [KindleModding GTK tutorial](https://kindlemodding.org/kindle-dev/gtk-tutorial/)
to set up the container (koxtoolchain + KMC SDK), then:

```bash
# Once: cross-compile the vendored libvncclient (submodule) and install it into the
# toolchain sysroot. The complete tested recipe is in docs/findings/libvncclient-api.md.
cd vendor/libvncserver && cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<toolchain-sysroot>/usr \
  -DWITH_LIBVNCSERVER=OFF -DWITH_LIBVNCCLIENT=ON \
  -DWITH_GCRYPT=OFF -DWITH_OPENSSL=OFF -DWITH_GNUTLS=OFF -DWITH_JPEG=OFF -DWITH_PNG=OFF \
  -DBUILD_SHARED_LIBS=ON
cmake --build build && cmake --install build

# The application
cd app && meson setup build --cross-file <your-meson-crosscompile.txt> && ninja -C build
```

The pure modules have unit tests that run on any machine, without the toolchain:

```bash
cd app
cc -std=gnu11 -Wall -Wextra -Isrc src/connection_store.c tests/test_connection_store.c -o /tmp/t && /tmp/t
cc -std=gnu11 -Wall -Wextra -Isrc src/keyboard.c tests/test_keyboard.c -o /tmp/t && /tmp/t
cc -std=gnu11 -Wall -Wextra -Isrc src/pixel_convert.c tests/test_pixel_convert.c -o /tmp/t && /tmp/t
```

## Usage

1. Tap **"Kindow"** in the Kindle's library. The Home screen remains visible for about
   three seconds before the application appears; the launcher delays startup to avoid a
   race with the system's Home screen redraw.
2. On the **connection screen**, tap a previously used server to reconnect, or **"+"**
   to enter the IP, port and password of a new one. A blank password means a server
   without authentication, which is the `install.sh` default. Successful connections
   are saved to `/mnt/us/kindow/connections.txt`; the password is stored in plain text
   (see the rationale in
   [`app/src/connection_store.h`](app/src/connection_store.h)).
3. In the **session**, touch interacts directly with the Pi's desktop. The bottom bar
   toggles the keyboard and the menu; the keyboard's `?123` page holds the Left-click
   (drag) and Right-click keys.
4. To **switch servers or quit**: menu → "Desconectar do Pi" returns to the connection
   screen. With no active session, the bar's "Menu" button becomes "Sair" (quit).

## Known limitations

- **Portrait only.** Landscape rotation is planned
  ([`docs/ideias-futuras.md`](docs/ideias-futuras.md)).
- **Grayscale.** The client converts all color to 256 gray levels, of which the e-ink
  panel effectively distinguishes about 16. There is no dithering yet; smooth gradients
  may show banding.
- **Single validated model.** Only the KT5 (1072×1448) has been tested. The layout is
  proportional by design, but other models are unverified.
- **Portuguese-only UI.** Button labels are in Portuguese ("Teclado", "Sair",
  "Conectando…"); internationalization has not yet been addressed.
- **Screensaver recovery.** The application disables the Kindle's screensaver while
  running and restores it on every normal exit path. If the process is killed without
  cleanup (SIGKILL, crash), the screensaver stays disabled until
  `lipc-set-prop -i com.lab126.powerd preventScreenSaver 0` is run or the device is
  rebooted.
- **Launch delay.** About three seconds from the library tap, as described in "Usage".
- **No transport encryption.** Classic VNC authentication only, intended for a trusted
  local network. Do not expose these ports to the internet.

## Architecture

The client follows a lightweight Ports & Adapters structure; each external dependency
is isolated behind a dedicated module.

- [`app/src/main.c`](app/src/main.c) — wiring: instantiates the modules and connects
  their callbacks.
- [`app/src/session.c`](app/src/session.c) — core: connection lifecycle (connect,
  automatic reconnection, socket watch), resize policy, and input dispatch. Depends on
  GLib as the event loop; no GTK, no VNC.
- [`app/src/ui.c`](app/src/ui.c) — presentation adapter (GTK2/Cairo): window, touch
  handling, bottom bar, keyboard/menu panel, connection screens. No VNC.
- [`app/src/vnc_client.c`](app/src/vnc_client.c) — the only module that uses
  `libvncclient`.
- [`app/src/kindle_platform.c`](app/src/kindle_platform.c) — device-specific concerns:
  screensaver control via `lipc`, the window-title convention required by the Kindle's
  window manager, the data directory.
- Pure modules with unit tests (no GTK, no VNC):
  [`keyboard.c`](app/src/keyboard.c) (layout, hit-testing, sticky modifiers),
  [`connection_store.c`](app/src/connection_store.c) (connection history) and
  [`pixel_convert.c`](app/src/pixel_convert.c) (color-to-grayscale conversion).
- [`app/src/remote_control.c`](app/src/remote_control.c) — TCP client for
  `kindow-helperd`, the zoom side channel outside the RFB protocol.
- [`pi/`](pi/) — the server side: X session, services, installer.
- [`kindle/`](kindle/) — launch scriptlet and deploy script.
- [`vendor/libvncserver`](vendor/libvncserver) — git submodule, pinned at 0.9.15.

## Technical documentation

The documentation under `docs/` is written in Portuguese.

- [`docs/findings/`](docs/findings/) — technical findings, one file per problem: the
  RFB protocol and encoding choices, the libvncclient API and the bugs worked around in
  it, the VNC server selection, and the issues only revealed by hardware testing.
- [`docs/ideias-futuras.md`](docs/ideias-futuras.md) — the roadmap, with the reasoning
  recorded for each item.
- [`docs/historico-da-poc.md`](docs/historico-da-poc.md) — the chronological log of the
  proof-of-concept phase.

## License

[GPL-3.0](LICENSE). The vendored `libvncclient` is licensed GPL-2.0-or-later, so any
distributed binary is subject to GPL terms; licensing the project under GPL-3.0 keeps
the whole repository consistent with its dependency.
