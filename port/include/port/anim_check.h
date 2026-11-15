#ifndef PORT_ANIM_CHECK_H
#define PORT_ANIM_CHECK_H

/* A model file's animations checked before the game plays them
 * (port/src/common/anim_check_pc.c, docs/LUIGI_ANIMS.md "Joints and vertex
 * morphs"). The game's animation code (src/driver/animdrv.c, animPoseMain)
 * trusts the file: each keyframe's node, vertex, normal, visibility and
 * texture deltas step an index through the pose's buffers and write there,
 * with no bounds. The discs' files are right; one we make at startup (Play
 * as's models and their packs: joints and vertex morphs in new animations
 * play through the same code as the discs') or one a player installs is
 * checked first, so a wrong one is left out rather than writing past a
 * buffer. tools/port/ttyd_anim.py check_model is the same check. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* strict(name, user): this animation is new (ours or a player's), so also
 * what the discs' don't always keep to: tangents inside the game's table and
 * a first key at or before the start (NULL: none is) */
typedef int (*PortAnimStrictFn)(const char* name, void* user);

/* file (size bytes, big-endian as on the disc): 1 if every table lies in
 * the file, the group tree's indices are in range and every animation stays
 * inside the model's buffers; else 0 and why (NUL-terminated, whyLen bytes) */
int portAnimCheckModel(const uint8_t* file, uint32_t size, PortAnimStrictFn strict, void* user, char* why,
                       size_t whyLen);

#ifdef __cplusplus
}
#endif

#endif
