CMake Settings
==============
The following are flags that may be of use when configuing this project.

 * ENABLE_PYTHON
  * A boolean flag for building and installing btaptap python tool.
  * The pcapdump module part of it is built into a wheel, which needs
    python3 -m pip. If setuptools is already installed the wheel is built
    from it, otherwise pip fetches a build backend. If pip is missing, the
    module is skipped and the rest of the build carries on.
 * RELEASE_STRING
  * The version the library reports. It defaults to the current git commit,
    or to the latest release when the source is not a git checkout.

CMake 3.5 or newer is required.
