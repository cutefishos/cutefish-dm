# cutefish-dm

The Cutefish display manager. It runs the login screen
(`cutefish-compositor --login`) and the Wayland session the user picks, Cutefish
or any other `wayland-sessions` entry such as GNOME or Plasma, with no black
frame or console in between.

The login screen runs on VT1 and the session on VT2, so every handover is a switch
between two VTs that are already drawing. On a single VT the console would flash
whenever a compositor exits.

- `cutefish-dm` is the root daemon. The login screen talks to it over
  `GREETD_SOCK` with greetd's protocol; `start_session` may name a
  `wayland-sessions` id instead of a command.
- `cutefish-dm-helper` runs PAM and the session for one login, like greetd's
  session worker and sddm-helper.
- `cutefish-session` calls `com.cutefish.DisplayManager.SwitchToGreeter` before
  it logs out, so the login screen is already up when the session ends.

## Dependencies

Qt 6.8 (Core, DBus, Network) and PAM; `cutefish-compositor` at runtime.

## Build

```shell
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

The Debian package creates the login screen's user; a source install needs it once:

```shell
sudo adduser --system --group --home /var/lib/cutefish-greeter cutefish-greeter
```

## Enable

```shell
sudo systemctl disable sddm
sudo systemctl enable cutefish-dm
```

Optional settings go in `/etc/cutefish-dm.conf`; see `daemon/config.cpp`.

## Tests

```shell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
