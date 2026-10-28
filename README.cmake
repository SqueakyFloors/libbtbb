CMake Settings
==============
The following are flags that may be of use when configuing this project.

 * ENABLE_PYTHON
  * A boolean flag for building and installing btaptap python tool.
  * The pcapdump module part of it is built into a wheel, which needs
    python3 -m pip. If setuptools is already installed the wheel is built
    from it, otherwise pip fetches a build backend. If pip is missing, the
    module is skipped and the rest of the build carries on.
  * The module is installed into the install prefix's site directory, which
    the interpreter searches only when that prefix is the one it was built
    for. btaptap looks for the module in the prefix it is installed in - the
    directory above its own - so the prefix does not have to be the
    interpreter's, and an install staged under DESTDIR works once it is
    moved into place. Nothing above that prefix is looked in, so an
    INSTALL_DEFAULT_BINDIR deeper than one directory does not find the
    module this way.
 * INSTALL_DEFAULT_BINDIR
  * The directory the tools are installed in, under the install prefix.
    It defaults to bin.
 * RELEASE_STRING
  * The version the library reports. It defaults to the current git commit,
    or to the latest release when the source is not a git checkout.

CMake 3.5 or newer is required.
