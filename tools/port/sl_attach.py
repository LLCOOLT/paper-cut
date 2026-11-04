"""Cards of our own added to a model the port makes from the disc
(docs/LUIGI_ANIMS.md, "Super Luigi's models": variants): Luigi in a bride's
veil, Luigi in his kart. The base is c_luigi_pc (the disc's c_luigi with
Luigi's own pack, port/src/aurora/luigi_model_aurora.c); the variant is the
base with more groups (textured cards and locators, hung from the base's
groups or from each other), more drawings after the base's textures, and
animations of our own, under a name of its own. Every pose of the base plays
on the variant as it is, the new cards following the groups they hang from.

What ships has only our own work: a pack (luigi_pack's LUPK: records
appended to the base's tables, group fields to set, animations) and a TPL of
our drawings, both entries of port/assets/super_luigi/sl_models.bin, named
"<variant>@<base>" and "<variant>-@<base>-". The port merges them into the
base it made at startup (port/src/aurora/sl_models_aurora.c). The records
point into the base's tables by count, so a base whose tables grew (a new
part in Luigi's pack) makes the port skip the variant: it checks the base's
position and texture counts against the pack's.

A card: dict(name, parent (a base group's name or an earlier card's), images
(RGBA rows: the rest drawing, then the others the "tex" channel picks), rect
(x0, y0, x1, y1) in the base's model units, z, pivot (x, y[, z]), visible).
A card without images is a locator.

A texture swap ({base image index: RGBA rows}, a variant module's SWAPS): the
drawing goes after the base's like a card's, and every texture bind of the
base that shows that image shows ours instead (luigi_pack.table_patch), so
one NPC can wear other colours without a texture pack turning every one of
its kind (Petunia's pink petals on c_pansy).
"""
import struct

import luigi_pack as LP
import ttyd_anim as T


def _u32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def _i32(v):
    return struct.pack(">i", v)


def _tpl_count(tpl):
    return struct.unpack_from(">I", tpl, 4)[0]


def card_records(base, cards, tex_base):
    """(records {table: [bytes]}, group patches [(group, offset, value)], images)
    for the cards added to `base` (ttyd_anim.PoseFile); the base's root stays the
    last group, the cards' groups go just before it"""
    t = base.tables
    out = {name: [] for name, _s, _c in T.TABLES}
    n_groups = len(t["groups"])
    first = n_groups - 1  # the cards' groups take the root's old place onwards
    base_names = {g.name.lstrip("|"): i for i, g in enumerate(base.groups())}
    order = list(cards)
    index = {c["name"]: first + k for k, c in enumerate(order)}
    images = []
    counts = {k: len(t[k]) for k in ("shapes", "drawCalls", "pos", "nrm", "clr", "texVtx", "texData", "texBinds",
                                      "draws", "vis", "node", "posIdx", "nrmIdx", "clrIdx", "texIdx0")}
    groups = []
    for c in order:
        shape = -1
        imgs = c.get("images") or []
        if imgs:
            shape = counts["shapes"] + len(out["shapes"])
            pos_i = counts["pos"] + len(out["pos"])
            tex_i = counts["texVtx"] + len(out["texVtx"])
            nrm_i = counts["nrm"] + len(out["nrm"])
            clr_i = counts["clr"] + len(out["clr"])  # (a base's colours needn't number its normals)
            # each index table on its own: a disc model's needn't be as long as its
            # positions' (c_heavy's untextured planes have no texture indices)
            pid_i = counts["posIdx"] + len(out["posIdx"])
            nid_i = counts["nrmIdx"] + len(out["nrmIdx"])
            cid_i = counts["clrIdx"] + len(out["clrIdx"])
            tid_i = counts["texIdx0"] + len(out["texIdx0"])
            call_i = counts["drawCalls"] + len(out["drawCalls"])
            draw_i = counts["draws"] + len(out["draws"])
            slot_i = counts["texData"] + len(out["texData"])
            bind_i = counts["texBinds"] + len(out["texBinds"])
            sh = bytearray(0xA8)
            sh[0:64] = T._pad_name(c["name"] + "Shape", 64)
            struct.pack_into(">26i", sh, 0x40, *([pos_i, 4, 0, 0, 0, 0, tex_i, 4] + [tex_i + 4, 0] * 7 +
                                                 [draw_i, 1, 0, c.get("cull", 3)]))
            out["shapes"].append(bytes(sh))
            out["drawCalls"].append(_i32(0) + _i32(4))
            x0, y0, x1, y1 = c["rect"]
            for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
                out["pos"].append(struct.pack(">3f", x, y, c["z"]))
                out["nrm"].append(struct.pack(">3f", 0.0, 0.0, 1.0))
                out["clr"].append(b"\xff\xff\xff\xff")
            for uv in ((0.0, 1.0), (1.0, 1.0), (0.0, 0.0), (1.0, 0.0)):
                out["texVtx"].append(struct.pack(">2f", *uv))
            for k in (0, 1, 3, 2):
                out["posIdx"].append(_u32(k))
                out["texIdx0"].append(_u32(k))
            for k in range(4):
                out["nrmIdx"].append(_u32(nrm_i + k))
                out["clrIdx"].append(_u32(clr_i + k))
            out["texAnim"].append(struct.pack(">B3x5f", 0, 0.0, 0.0, 1.0, 1.0, 0.0))
            out["texData"].append(_i32(bind_i) + _i32(0))
            for img in imgs:
                b = bytearray(0x40)
                struct.pack_into(">Ii", b, 4, tex_base + len(images), 0)
                out["texBinds"].append(bytes(b))
                images.append(img)
            dr = bytearray(0x6C)
            struct.pack_into(">ii", dr, 0, 1, 1)
            struct.pack_into(">8i", dr, 0x10, slot_i, -1, -1, -1, -1, -1, -1, -1)
            dr[0x30:0x38] = bytes([0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
            struct.pack_into(">2i11i", dr, 0x38, call_i, 1, pid_i, nid_i, cid_i, tid_i, 0, 0, 0, 0, 0, 0, 0)
            out["draws"].append(bytes(dr))
        vis_i = counts["vis"] + len(out["vis"])
        out["vis"].append(b"\x01" if c.get("visible", True) else b"\x00")
        node_i = counts["node"] + len(out["node"])
        piv = tuple(c["pivot"]) + (c.get("z", 0.0),) * (3 - len(c["pivot"]))
        node = [0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0] + list(piv) * 2 + [0.0] * 6
        out["node"] += [struct.pack(">f", v) for v in node]
        g = bytearray(0x58)
        g[0:64] = T._pad_name(c["name"], 64)
        struct.pack_into(">6i", g, 0x40, -1, -1, shape, vis_i, node_i, 0)
        groups.append(g)
    # links: each card the first child of its parent, the parent's old first child its brother
    patches = []
    child_of = {}  # a base group's first child as it stands (patched)
    base_groups = base.groups()
    for k, c in enumerate(order):
        gi = index[c["name"]]
        p = c["parent"]
        if p in index:
            pg = groups[index[p] - first]
            old = struct.unpack_from(">i", pg, 0x44)[0]
            struct.pack_into(">i", groups[k], 0x40, old)
            struct.pack_into(">i", pg, 0x44, gi)
        else:
            bi = base_names[p]
            old = child_of.get(bi, base_groups[bi].child)
            struct.pack_into(">i", groups[k], 0x40, old)
            child_of[bi] = gi
    for bi, gi in child_of.items():
        patches.append((bi, 0x44, gi))
    out["groups"] = [bytes(g) for g in groups]
    return out, patches, images


def swap_patches(base, swaps, tex_base):
    """(patches, images): each base image of `swaps` ({index: RGBA rows}) shown
    as ours, the drawings numbered from tex_base on"""
    patches, images = [], []
    for k, img in sorted(swaps.items()):
        hit = [i for i, b in enumerate(base.tables["texBinds"]) if struct.unpack_from(">I", b, 4)[0] == k]
        if not hit:
            raise ValueError("no texture bind shows image %d" % k)
        patches += [LP.table_patch("texBinds", i, 4, tex_base + len(images)) for i in hit]
        images.append(img)
    return patches, images


def variant(base_model, base_tpl, cards, anims=(), swaps=None):
    """(pack, tpl of our drawings) for a variant of the base model file: the
    swaps' drawings first, then the cards'"""
    base = T.PoseFile.parse(base_model)
    sw_patches, sw_images = swap_patches(base, swaps or {}, _tpl_count(base_tpl))
    recs, patches, images = card_records(base, cards, _tpl_count(base_tpl) + len(sw_images))
    import paper_model as PM
    pack = LP.pack_bytes(list(anims), base, recs, patches + sw_patches)
    return pack, PM.make_tpl(sw_images + images)


def merge(base_model, base_tpl, pack, tpl, name):
    """the variant's (model, textures), as the port makes them; None if the base
    isn't the one the pack was made for (its position and texture counts)"""
    pk = LP.read_pack(pack)
    if not fits(base_model, base_tpl, pk):
        return None
    pk = dict(pk, size=len(base_model), crc=__import__("zlib").crc32(base_model) & 0xFFFFFFFF)
    model = LP.merge_model(base_model, pk, name)
    ents = LP._tpl_entries(base_tpl) + LP._tpl_entries(tpl)
    return model, LP._pad32(LP.layout_tpl(ents))


def fits(base_model, base_tpl, pk):
    p = T.PoseFile.parse(base_model)
    sh = pk["tables"]["shapes"]
    if sh and struct.unpack_from(">i", sh[0], 0x40)[0] != len(p.tables["pos"]):
        return False
    # our first drawing (a swap's, else a card's) is just after the base's
    ours = [v for g, off, v in pk["patches"] if g >> 24 == 20 and off == 4]
    ours += [struct.unpack_from(">I", b, 4)[0] for b in pk["tables"]["texBinds"]]
    if ours and min(ours) != _tpl_count(base_tpl):
        return False
    return True
