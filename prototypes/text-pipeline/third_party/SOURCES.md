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
