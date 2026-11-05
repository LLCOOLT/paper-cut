/* Paper looks' drawing on Aurora (port/include/port/paper.h): its paper patch
 * (aurora/paper.h, port/patches/aurora), through the GX command stream, so
 * each change lands between the draws around it. */
#include <aurora/paper.h>
#include "port/paper.h"

void portPaperGxShadow(int on) {
    aurora_set_paper_shadow(on);
}

void portPaperGxEdges(int on) {
    aurora_set_paper_edges(on);
}
