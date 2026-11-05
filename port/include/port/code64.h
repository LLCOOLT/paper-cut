#ifndef PORT_CODE64_H
#define PORT_CODE64_H

/* The text codes players share (Master Quest's sets, enemy variants): bytes
 * in URL-safe base64 (A-Z a-z 0-9 - _), no padding (port/src/common/code64_pc.c). */

#ifdef __cplusplus
extern "C" {
#endif

/* in's n bytes into out (size with its NUL): the length, or -1 if it's too small */
int portCode64Encode(const unsigned char* in, int n, char* out, int size);
/* in into out (size with a NUL after): whitespace skipped, it ends at any other
 * character not in the alphabet; the length, or -1 if out is too small */
int portCode64Decode(const char* in, unsigned char* out, int size);

#ifdef __cplusplus
}
#endif

#endif
