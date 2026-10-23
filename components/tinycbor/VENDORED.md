# Vendored library: TinyCBOR

This directory holds a partial copy of TinyCBOR. It encodes and decodes
Concise Binary Object Representation (CBOR) data. See `docs/06-cbor-encoding.md`.

| Field | Value |
|---|---|
| Upstream project | https://github.com/intel/tinycbor |
| Version | v7.0 |
| Commit | 6442e749ca811e24afad1551338a45c75e09808f |
| License | MIT. See `LICENSE` in this directory. |

Only the encoder and parser source files are copied here: `cbor.h`, `cborencoder.c`,
`cborencoder_close_container_checked.c`, `cborencoder_float.c`, `cborerrorstrings.c`,
`cborinternal_p.h`, `cborinternalmacros_p.h`, `cborparser.c`, `cborparser_float.c`, and
`compilersupport_p.h`. The pretty-printer, JSON conversion, string-duplication helper, and
build-system files from the upstream project are not needed by this firmware and are not
included.

## Two generated headers, written by hand

Upstream TinyCBOR is built with CMake, which generates two small headers at configure time:
`tinycbor-export.h` (from CMake's `GenerateExportHeader` module, for a build that may produce a
shared library) and `tinycbor-version.h` (from `src/tinycbor-version.h.in`, filled in with the
version CMake's `project()` call declares). Since this project never runs that CMake build, both
headers are written by hand in this directory, with the same values that build would produce for
v7.0 built as a static library: every export macro (`CBOR_API`, `CBOR_PRIVATE_API`) empty, and
`TINYCBOR_VERSION_MAJOR`/`MINOR`/`PATCH` set to 7/0/0. `cbor_cfg.h`, a third file CMake can
generate, is never included unless `CBOR_EXTERNAL_CFG` is defined, which this project does not
define, so it is not needed here.
