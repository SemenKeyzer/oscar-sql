/* hidapi's Windows backend for OSCAR
 *
 * oscar.pro defines WINVER=0x0501 for every file; hidapi's windows/hid.c needs Windows Vista's
 * declarations (WC_ERR_INVALID_CHARS), and Qt 6 runs on Windows 10 anyway. The vendored file
 * stays unmodified. */

#undef WINVER
#define WINVER 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00

#include "hidapi/windows/hid.c"
