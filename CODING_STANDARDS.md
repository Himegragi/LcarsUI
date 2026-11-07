# Coding standards for review

- A property asserted through more than one test seam uses one shared predicate.
- A real-platform test (IME, DPI, clipboard) has a core-seam twin wherever the property is observable without the platform.
- Production changes to layout or the desktop host list which real-platform runs they require.
- Tests should exercise observable behavior through established public or host seams, with independent expected results.
- Preserve user data and make platform failures distinguishable from unsupported or absent functionality.
- Keep evidence, limits and reproducible verification steps clear and current.
