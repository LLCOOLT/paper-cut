Minimal hosted-libc stand-ins for `gen_bedecode.py`'s layout probes on hosts
without glibc headers (Windows). The probes only parse headers to dump struct
layouts, which do not depend on the C library, so declarations are enough.
Used with `-nostdlibinc`; clang's own freestanding headers (stdint.h,
stddef.h, stdbool.h, ...) still apply. Never used to compile the game.
