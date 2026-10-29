Wireshark Plugins
-----------------

This directory holds two sets of Wireshark plugins:

- `plugins`: the btbb and btbredr dissectors, for Wireshark 2.0 and later.
- `plugins-legacy`: the same two for Wireshark 1.x, and the btatt, btle and
  btsm dissectors, which Wireshark has carried itself since 1.12. On
  Wireshark 1.12 or later only btbb and btbredr are needed.

To see which version you have, run `wireshark -v`.

Each plugin builds on its own, with its own CMake project; the libbtbb build
does not build them. The README beside each one has its build and install
instructions.
