Building OSCAR on Linux
=======================

OSCAR 2 needs Qt 6 and a C++17 compiler. Qt 6.10 is recommended; the Qt 6.4 packaged with
Ubuntu 24.04 is enough to build the application and run the unit tests.

Packages (Ubuntu 24.04 / Debian names; see ./Prebuild_env.sh):

    build-essential qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools
    qt6-l10n-tools libqt6serialport6-dev libqt6opengl6-dev libqt6sql6-sqlite
    libglu1-mesa-dev libx11-dev zlib1g-dev libudev-dev

Optional: `qt6-connectivity-dev` adds the Qt Bluetooth module, which enables importing from
Contec oximeters over Bluetooth. Without it OSCAR builds as before, without that button
(or pass `CONFIG+=no_bluetooth` to leave it out on purpose).

The help browser is disabled in the build (`DEFINES += helpless`), so Qt Help is not needed.

The current pre-built downloads use the distribution-supplied version of Qt.

Building
--------

Shadow building is recommended to avoid cluttering up the git source code folder.
The following does not use Qt Creator and assumes the directory structure shown:

    $ mkdir OSCAR
    $ cd OSCAR
    $ git clone https://gitlab.com/CrimsonNape/OSCAR-SQL.git OSCAR-code
    $ mkdir build
    $ cd build
    $ qmake6 ../OSCAR-code/OSCAR_QT.pro
    $ make -j$(nproc)

After successful compilation, you can execute in place with ./oscar/OSCAR20,
or build an installable package with mkOSDistDeb-Qt6.sh (Debian/Ubuntu) or mkRedHat.sh.
It is recommended to create a Packages folder within OSCAR and copy the scripts and associated
files to it to avoid cluttering up the git source tree.

Build options (append to the qmake line):

- `CONFIG+=crash` — debug build
- `CONFIG+=memdebug` — AddressSanitizer
- `CONFIG+=no_bluetooth` — leave out Bluetooth oximeter import

Unit tests
----------

The tests are a separate build of the same project (target `test`, built with AddressSanitizer):

    $ mkdir build-test
    $ cd build-test
    $ qmake6 CONFIG+=test ../OSCAR-code/oscar/oscar.pro
    $ make -j$(nproc)
    $ QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 ./test

The exit code is non-zero when a test fails. LeakSanitizer reports some known leaks at exit,
which also make it non-zero; `ASAN_OPTIONS=detect_leaks=0` leaves only the test results (memory
errors still stop the run). CI runs the same steps, see `.github/workflows/build.yml`.

Loader and Daily-view tests that need sample card data read it from `./testdata/`, which is
not in the repository; without it the loader tests have nothing to compare and EventsTabTests
is skipped.

Compiler warnings are errors (`-Werror`), except deprecation warnings, which newer Qt
releases keep adding.
