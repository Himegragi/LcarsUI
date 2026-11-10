# Draw-list backend mapping review

Ticket #37 reviews the Vulkan mapping in spec #32. Coordinates remain physical pixels with a top-left origin, and commands retain their specified ordering and semantics.

| Draw-list or backend concept | OpenGL implementation | Vulkan counterpart and constraint |
| --- | --- | --- |
| Coordinates | Vertex shader converts to NDC and flips y | Use top-left pixel coordinates and viewport conversion; no backend convention enters the list. |
| Clear | Clear framebuffer | Render-pass clear or explicit clear; first command clears. |
| Clip | Scissor converts to bottom-left coordinates | Dynamic scissor; integer, axis-aligned only. |
| Fill and rounded fill | Quad generated from vertex ID; fragment shader computes quantized corner coverage | Quad and equivalent shader compiled to SPIR-V. No extra drawing primitives are implied. |
| Glyphs | R8 atlas texture with nearest texel access | R8 image, nearest sampler, descriptor set; integer glyph positions. |
| Atlas upload | Preflight resolves and uploads complete pages before drawing; if a frame overflows the atlas, upload and draw borrowed regions one at a time | Staging copies and barriers before each execution segment; end and resume the render pass for overflow segments. |
| Blending | Non-sRGB source-alpha blending, with separate alpha factors | Non-sRGB color attachment with equivalent blend factors. |
| Groups | Preserved in the draw list; debug labels are emitted when supported | Debug-utils labels when supported. |
| Presentation | SwapBuffers | Queue present; an out-of-date swapchain is not device loss. |
| Frame errors, reset, presentation errors | Structured renderer failure and optional robustness detection | Check VkResult and map device loss to the shared failure state. |
| Readback | RGBA8 framebuffer; rows converted in the backend | Copy image to buffer and expose top-down pixels. |
| Rebuild | Explicit context resource reconstruction | Recreate device resources from CPU atlas data. |
| Diagnostics | Debug context, labels and queued callbacks | Debug-utils callback copies messages to a bounded queue. |

The spec's instanced-buffer batching was an engineering proposal. This slice uses one generated quad per command, uploads complete atlas pages once for a fitting frame, and segments overflow one glyph at a time. Command order and pixel semantics are unchanged. Performance work remains for S7.

Host changes require idle-desktop clipboard, IME and DPI checks using `verify-idle.ps1`; the coordinator schedules those runs.
