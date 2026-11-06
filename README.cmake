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
  * The version the library reports from btbb_get_release(). In a git checkout
    it defaults to the nearest release tag and how many commits the checkout
    is past it (`2020-12-R1+22`, with a trailing `*` for uncommitted changes).
    In a source archive it defaults to the release the archive was made from,
    which git wrote into the RELEASE-VERSION file as the archive was made, and
    it is `unknown` where neither says. It is not the library's version or
    soname, which are in README.md under "What a program links".
 * BTBB_COMMIT
  * The commit the library records and reports from btbb_get_commit(). It
    defaults to the checkout's HEAD, and is `unknown` when the source is not
    a git checkout.

CMake 3.5 or newer is required, and CMake 4 builds this project.

The release a build reports
---------------------------
`RELEASE_STRING` comes from one of three places, in this order.

A git checkout asks git: the nearest release tag and how many commits the
checkout is past it.

A source archive reads the `RELEASE-VERSION` file at the top of the tree.
`.gitattributes` marks that file `export-subst`, so `git archive` -- which is
how a release tarball and GitHub's "Download ZIP" are made -- fills the release
into it from the commit it archived. Nothing is kept by hand, and nothing has
to be edited before a release is tagged. Filling it in needs git 2.32 or newer
*where the archive is made*; the git that unpacks or builds it does not matter,
and an archive made by an older git reports its release as `unknown`.

Where neither says -- a checkout whose history reaches no release tag, which a
shallow clone or one fetched without tags often is, or a partial copy of these
sources with no `RELEASE-VERSION` in it -- the release is `unknown` and
`btbb_get_commit()` is what identifies the build. `PACKAGE_VERSION`, the PEP 440
version the Python tools carry, is then `0+unknown`, which sorts below every
release: a wheel built from such a checkout will not upgrade an installed one.

The three release tag patterns are written twice, once as CMake globs in
`cmake/set_release.cmake` and once inside the placeholder in
`RELEASE-VERSION`. A new kind of release tag has to be added in both, or
archives lose their release; the `release_version` test compares the two.
