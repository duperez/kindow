# Kindow

*[Versão em português](docs/README.pt-BR.md)*

Turn a jailbroken Kindle into a **wireless touchscreen for a Raspberry Pi**, over VNC:
the Pi's desktop shows up on the Kindle's e-ink display, and touching the screen sends
mouse and keyboard input back — with screen updates only when content actually changes
(an e-ink panel couldn't survive continuous refresh, and the RFB protocol is
on-demand by design anyway).

<p align="center">
  <img src="docs/images/kindow-photo.jpg" alt="A Kindle on a stand, running Kindow: the Pi's Mousepad editor with 'Hello from Kindow' typed on the on-screen keyboard, on real e-ink" width="420">
  &nbsp;&nbsp;
  <img src="docs/images/kindow-photo-files.jpg" alt="The same Kindle browsing the Pi's filesystem with the PCManFM file manager, taskbar showing multiple open apps" width="420">
</p>

<p align="center">
  <img src="docs/images/session.png" alt="Framebuffer capture of a Kindow session: the Pi's desktop with Mousepad, taskbar and the on-screen keyboard" width="380">
  &nbsp;&nbsp;
  <img src="docs/images/connect.png" alt="Framebuffer capture of the Kindow connection screen: saved Pi list, add-new button, and the bottom bar" width="380">
</p>

*The photo is the real device; the two smaller images are framebuffer captures — exactly
what the e-ink shows, pixel for pixel.*

**Status**: working end to end on real hardware (Kindle KT5 + Raspberry Pi). It started
as a proof of concept and still shows it in places — but the full loop (connect, see,
touch, type, drag) is validated on the physical device.

## What it does

- **Interactive remote desktop**: the Pi's X session (Openbox + tint2 + GTK apps)
  rendered 1:1 on the Kindle — the remote resolution adapts automatically to whatever
  Kindle connects (`SetDesktopSize`), no scaling, no cropping.
- **Touch = mouse**: a tap is a left click; the keyboard's symbols page has dedicated
  **Left** (sticky — arms a real press-and-drag that ends when your finger lifts) and
  **Right** click keys.
- **On-screen keyboard** with sticky Shift/Ctrl (chords like Ctrl+C work without
  multi-touch) and a symbols page.
- **Persistent bottom bar**: scroll ↑/↓ (wheel notches per tap are adjustable),
  show/hide the keyboard, and the menu.
- **Menu**: remote zoom in 3 independent layers (app content via Xft/DPI, window
  decorations, panel), disconnect, connection status, quit.
- **Connection screen**: history of previously used Pis (tap to reconnect), a form to
  add a new one (IP/port/password), classic VNC password support, and real error
  messages when a connection doesn't come up.
- **Launches with a tap** from the Kindle's library (the "Kindow" scriptlet) — no SSH
  needed to start it.

## Requirements

- A **jailbroken Kindle** with scriptlet support (a tappable `.sh` in the library — the
  standard mechanism of the [modern jailbreak](https://kindlemodding.org/)). Tested on a
  KT5 (1072×1448); the layout is proportional and should adapt to other resolutions,
  but only the KT5 has been validated.
- A **Raspberry Pi** (or any Debian-like Linux with `systemd`) on the same network,
  reachable over SSH.
- To build the client: the KindleModding cross-compilation toolchain (koxtoolchain +
  KMC SDK) in a container — see "Building" below.

## Installing

### Pi side (server)

```bash
scp -r pi/ pi@<pi-ip>:/tmp/kindow-pi && ssh -t pi@<pi-ip> 'bash /tmp/kindow-pi/install.sh'
```

[`install.sh`](pi/install.sh) is idempotent (re-running is safe): it installs the
packages (TigerVNC, Openbox, tint2, mousepad, xsettingsd), applies the session configs
without overwriting your customizations (chosen zoom levels, an edited `rc.xml`),
installs and enables both services (`vnc-kindle` on port 5901, `kindow-helperd` on
5910 — the zoom side channel) and verifies at the end that both respond.

### Kindle side (client)

With the binary already cross-compiled (see below) and root SSH on the Kindle:

```bash
./kindle/deploy.sh <kindle-ip>
```

This copies the binary + `libvncclient` to `/mnt/us/kindow/`, installs the
[`kindle/kindow.sh`](kindle/kindow.sh) scriptlet into `/mnt/us/documents/` (it becomes
the tappable "Kindow" item in the library) and relaunches the app.

### Building the client

The client is C/GTK2 (the GTK the Kindle firmware ships), cross-compiled with Meson
inside a container with the KindleModding toolchain — follow the
[KindleModding GTK tutorial](https://kindlemodding.org/kindle-dev/gtk-tutorial/) to set
up the container (koxtoolchain + KMC SDK). With it running:

```bash
# once: the vendored libvncclient (submodule), cross-compiled and installed into the
# toolchain sysroot — the full tested recipe (every flag, the sysroot install and the
# verification) is in docs/findings/libvncclient-api.md
cd vendor/libvncserver && cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=../../cmake/Toolchain-arm-kindlehf-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<toolchain-sysroot>/usr \
  -DWITH_LIBVNCSERVER=OFF -DWITH_LIBVNCCLIENT=ON \
  -DWITH_GCRYPT=OFF -DWITH_OPENSSL=OFF -DWITH_GNUTLS=OFF -DWITH_JPEG=OFF -DWITH_PNG=OFF \
  -DBUILD_SHARED_LIBS=ON
cmake --build build && cmake --install build

# the app itself
cd app && meson setup build --cross-file <your-meson-crosscompile.txt> && ninja -C build
```

The pure modules' unit tests run on any machine, no toolchain needed:

```bash
cd app
cc -std=gnu11 -Wall -Wextra -Isrc src/connection_store.c tests/test_connection_store.c -o /tmp/t && /tmp/t
cc -std=gnu11 -Wall -Wextra -Isrc src/keyboard.c tests/test_keyboard.c -o /tmp/t && /tmp/t
cc -std=gnu11 -Wall -Wextra -Isrc src/pixel_convert.c tests/test_pixel_convert.c -o /tmp/t && /tmp/t
```

## Using it

1. Tap **"Kindow"** in the Kindle's library (the Home screen shows for ~3s before the
   app appears — that's intentional, a race against the Home redraw the launcher has to
   win).
2. **Connection screen**: tap a previously used Pi to reconnect, or **"+"** to enter
   the IP/port/password of a new one (blank password = server without one, the
   `install.sh` default). Successful connections are saved to the history
   (`/mnt/us/kindow/connections.txt` — the password is stored there in plain text, a
   documented decision, see [`app/src/connection_store.h`](app/src/connection_store.h)).
3. **In the session**: touch interacts directly with the Pi's desktop. The bottom bar
   toggles keyboard/menu; the keyboard's `?123` page has the Left (drag) and Right
   click keys.
4. **Quit / switch Pi**: menu → "Desconectar do Pi" goes back to the connection screen;
   with no active session, the bar's "Menu" button becomes **"Sair"** (quit).

## Known limitations

- **Portrait only** for now — landscape rotation is on the roadmap
  ([`docs/ideias-futuras.md`](docs/ideias-futuras.md)).
- **Grayscale**: the client converts everything to 256 gray levels (and the e-ink panel
  itself only really distinguishes ~16). No dithering yet, so smooth gradients may show
  banding.
- **Only the KT5 validated** (1072×1448). The layout is proportional by design, but no
  other model has been tested.
- **UI language is Portuguese** (buttons like "Teclado", "Sair", "Conectando…") — the
  app was built by and for a Brazilian; i18n hasn't been a goal so far.
- **Screensaver can get stuck off** if the process dies without cleanup (SIGKILL /
  crash): the app disables the Kindle's screensaver while running and restores it on
  every normal exit path, but nothing can run after a SIGKILL. Recovery:
  `lipc-set-prop -i com.lab126.powerd preventScreenSaver 0` (or reboot).
- **~3s launch delay** from the library tap, explained above.
- **No VNC encryption**: classic VNC auth only, on a trusted local network. Don't
  expose these ports to the internet.

## Architecture

Lightweight Ports & Adapters — each external dependency isolated behind its own module:

- [`app/src/main.c`](app/src/main.c) — wiring only: instantiates the modules and
  connects callbacks.
- [`app/src/session.c`](app/src/session.c) — the core: connection lifecycle (connect,
  auto-reconnect, fd watch), resize policy, sending clicks/keys/scroll/drag. Knows GLib
  (event loop), not GTK or VNC.
- [`app/src/ui.c`](app/src/ui.c) — presentation adapter (GTK2/Cairo): window, touch,
  bar, keyboard/menu panel, connection screens. Knows nothing about VNC.
- [`app/src/vnc_client.c`](app/src/vnc_client.c) — the only module that talks to
  `libvncclient`.
- [`app/src/kindle_platform.c`](app/src/kindle_platform.c) — device-specific bits
  (screensaver via `lipc`, the magic window title, the data directory).
- Pure, testable modules (zero GTK/VNC): [`keyboard.c`](app/src/keyboard.c) (layout,
  hit-testing, sticky keys), [`connection_store.c`](app/src/connection_store.c)
  (connection history) and [`pixel_convert.c`](app/src/pixel_convert.c)
  (color → grayscale).
- [`app/src/remote_control.c`](app/src/remote_control.c) — TCP client for
  `kindow-helperd` (remote zoom, outside the RFB protocol).
- [`pi/`](pi/) — the complete server side (X session, services, installer).
- [`kindle/`](kindle/) — launch scriptlet and deploy script.
- [`vendor/libvncserver`](vendor/libvncserver) — git submodule, pinned at 0.9.15.

## Technical documentation

Heads-up: everything under `docs/` is written in Portuguese — it's the project's
working log, kept in its original language.

- [`docs/findings/`](docs/findings/) — technical findings, one file per
  problem/solution: the RFB protocol and encodings, the libvncclient API (and the real
  bugs in it we worked around), the VNC server choice, and everything only real
  hardware testing revealed.
- [`docs/ideias-futuras.md`](docs/ideias-futuras.md) — the roadmap, with the reasoning
  recorded before each implementation.
- [`docs/historico-da-poc.md`](docs/historico-da-poc.md) — the chronological diary of
  the proof of concept, preserved as a faithful record of how the project got here.

## License

[GPL-3.0](LICENSE). The choice follows the dependency: the vendored `libvncclient` is
GPL-2.0-or-later, so any distributed binary would inherit GPL terms anyway — licensing
the whole project as GPL is the coherent option.
