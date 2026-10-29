CMake Settings
==============
The following are flags that may be of use when configuring this project.
Where the library and the header are installed is in README.md, under
"Where it installs".

 * ENABLE_PYTHON
  * A boolean flag for building and installing btaptap python tool.
  * The pcapdump module part of it is built into a wheel, which needs
    python3 -m pip. If setuptools is already installed the wheel is built
    from it, otherwise pip fetches a build backend. If pip is missing, the
    module is skipped and the rest of the build carries on.
  * The module is installed into the install prefix's site directory, which
    the interpreter searches only when that prefix is the one it was built
    for. btaptap looks for the module in the prefix it is installed in, so
    the prefix does not have to be the interpreter's, and an install staged
    under DESTDIR works once it is moved into place. The copy that is
    installed carries the depth of INSTALL_DEFAULT_BINDIR, so that it finds
    its prefix and never looks above it.
 * BUILD_SHARED_LIB, BUILD_STATIC_LIB
  * Which library to build. The shared one is on by default and the static
    one off; both can be on. A configuration with neither is refused while
    configuring, so a static-only build is
    -DBUILD_SHARED_LIB=OFF -DBUILD_STATIC_LIB=ON.
 * INSTALL_DEFAULT_BINDIR
  * The directory the tools are installed in, under the install prefix.
    It defaults to bin.
 * BUILD_TESTING
  * Whether to build the test suite, on by default. Run it with ctest; see
    TESTING.md.
 * RELEASE_STRING
  * The version the library reports from btbb_get_release(). It defaults to
    the current git commit, or to the latest release when the source is not a
    git checkout. It is not the library's version or soname, which are in
    README.md under "What a program links".

CMake 3.5 or newer is required, and CMake 4 builds this project.
