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
