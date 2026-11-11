# Fixed source archives, no package manager

- HarfBuzz 14.6.0, commit `c7a7457b7385f33178e8cf87615ca077a810bbe7`:
  https://codeload.github.com/harfbuzz/harfbuzz/tar.gz/c7a7457b7385f33178e8cf87615ca077a810bbe7
  SHA256 `10dcea60bd85ff219490d55525630650ac8c57044c4efdc878162f286451775b`
- FreeType 2.14.1, commit `526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b`:
  https://codeload.github.com/freetype/freetype/tar.gz/526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b
  SHA256 `d24f53b60034dc5c48e90d122d432a8f04a05b9e0a98c8d8e49b4a759207ce2e`

Archives retain upstream copyright and license documents; CMake verifies archives before extraction.
Extraction skips only the non-build CLAUDE.md symlink. Sources are expanded under ignored build/sources.
FreeType optional Zlib/BZip2/PNG/Brotli/HarfBuzz auto-hinting dependencies disabled. HarfBuzz links FreeType; experimental raster/vector/GPU, subset, CLI tools disabled.
Khronos headers come from the preceding minimal-build prototype. No fonts are included; use locally installed Windows fonts only. This does not select the product's default font policy.

# Pinned Khronos headers

Vendored source only; no package manager and no configure-time network access.
The full upstream licenses are retained in each header.

- glcorearb.h: OpenGL-Registry commit `6af574a14089ccfee87efe230ebcdd8742859813`, `api/GL/glcorearb.h`.
  Source: https://github.com/KhronosGroup/OpenGL-Registry/blob/6af574a14089ccfee87efe230ebcdd8742859813/api/GL/glcorearb.h
  SHA256: `65fae555a8b3b5709099837001e0c14228b3e45651e2d0fa2ad22649833921e1`
- KHR/khrplatform.h: EGL-Registry commit `db3425b8246136faccb5e2782b5694960bd6edf1`, `api/KHR/khrplatform.h`.
  Source: https://github.com/KhronosGroup/EGL-Registry/blob/db3425b8246136faccb5e2782b5694960bd6edf1/api/KHR/khrplatform.h
  SHA256: `7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83`

The small local GL symbol table is probe code, not a production loader dependency decision.

MVP reuse: source archives and GL headers copied unchanged from prototype branch codex/prototype-selfdraw-editing, commit 7950cab4d81f8c4b57d3368fa8e77a0faee16020. The MVP extracts and adapts the GL bootstrap, shaping and rasterization approach; it does not include prototype entry points, UIA providers or benchmark fixtures.

The generated [third-party notices](../THIRD_PARTY_NOTICES.txt) contain the shipped licenses. The original source archives remain unchanged.

# Unicode data

- VerticalOrientation-18.0.0.txt (Unicode Character Database, UAX #50 Vertical_Orientation), copied unchanged:
  https://www.unicode.org/Public/18.0.0/ucd/VerticalOrientation.txt
  SHA256 `0803e09669d7aa7137de678e55b848bd418c6632694fa1b05f0b40941103c748`
- LICENSE-UNICODE-V3.txt (Unicode License v3), copied unchanged: https://www.unicode.org/license.txt
  SHA256 `e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96`

`src/vertical_orientation_data.inc` is generated from the data file with `third_party/unicode/gen_vertical_orientation.py`
(only the non-default values are written; every other code point is R). To move to a newer Unicode version, replace the
two files, update `cmake/ThirdPartyManifest.cmake`, rerun the generator and regenerate the notices.

# CI-only tools

- mesa-dist-win 26.2.5 MSVC, used only for Windows llvmpipe CI, not distributed with LcarsUI:
  https://github.com/pal1000/mesa-dist-win/releases/download/26.2.5/mesa3d-26.2.5-release-msvc.7z
  SHA256 `997123ef4ce8896204ab912d5b54f19372c487ff9835bcefafa2fca23b8953c2`
  Version 26.2.6's MSVC package omits OpenGL because of its documented build regression.
