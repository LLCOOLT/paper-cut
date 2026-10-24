/* Memory card. Saves will live in files under the user's config directory;
 * until then the card stubs report no card in the slot. */
#include <dolphin.h>
#include <dolphin/card.h>

s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
    if (callback != NULL) {
        callback(chan, CARD_RESULT_NOCARD);
    }
    return CARD_RESULT_NOCARD;
}
