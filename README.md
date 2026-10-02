# cutefish-dm

The Cutefish display manager. It runs the login screen and starts the Wayland
session the user picks.

## Dependencies

Qt 6.8 (Core, DBus, Network) and PAM; `cutefish-compositor` at runtime.

## Build

```shell
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

A source install needs the login screen's user once:

```shell
sudo adduser --system --group --home /var/lib/cutefish-greeter cutefish-greeter
```

## Enable

```shell
sudo systemctl disable sddm
sudo systemctl enable cutefish-dm
```

## Tests

```shell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
