# Contributing

This is a fork of [Great Scott Gadgets' libbtbb](https://github.com/greatscottgadgets/libbtbb).
Changes for upstream belong there; this repository takes changes to the fork.

Before you start:

- [README.md](README.md) has the build and install commands, and
  [README.cmake](README.cmake) covers the CMake options.
- [TESTING.md](TESTING.md) says how to run the test suite and the fuzz
  targets, and what each test covers. A change is expected to come with the
  test that would have caught the bug it fixes.

## License notices

[LICENSE](LICENSE) licenses the project. A source file does not repeat it: a
new non-test source file starts with one line naming the license, and nothing
else about it.

- C and headers: `/* SPDX-License-Identifier: GPL-2.0-or-later */` as line 1.
- Python, shell, and CMake: `# SPDX-License-Identifier: GPL-2.0-or-later` as
  line 1, or line 2 where a `#!` line comes first.
- Test files -- anything under a `tests/` directory, anything named `test_*`,
  and the fuzz targets -- carry no license notice at all.

Notices that came from upstream are kept exactly as they are, and so is any
notice naming a holder other than the fork. That includes a file upstream
wrote that was later renamed or moved here: the name changed, the notice does
not.
