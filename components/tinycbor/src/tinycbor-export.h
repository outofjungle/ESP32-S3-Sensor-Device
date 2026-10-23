/* Not part of the TinyCBOR source tree. Upstream generates this header at
 * configure time, from CMake's GenerateExportHeader module, for a build that
 * may produce a shared library. This project only ever builds TinyCBOR as a
 * static ESP-IDF component, so every export macro is a no-op. See
 * VENDORED.md. */
#ifndef TINYCBOR_EXPORT_H
#define TINYCBOR_EXPORT_H

#define CBOR_API
#define CBOR_PRIVATE_API

#endif
