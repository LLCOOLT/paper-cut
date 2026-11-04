/* Super Luigi's new models (docs/LUIGI_ANIMS.md, "Super Luigi's models"). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* At startup, before the mods: every model of port/assets/super_luigi/sl_models.bin
 * (our own work, built in) added as a/<name>, which the game loads by name like
 * a disc model (port/src/aurora/sl_models_aurora.c). */
void portSuperLuigiModelsInit(void);
/* After the art recipes (portArtPrepare), before the mods: each recipe image at
 * art/models/<model>/<n>.png becomes image n of a/<model>- (art/RECIPES.md). */
void portSuperLuigiModelArt(void);
/* $TTYD_SL_GALLERY (tests): models stood beside the player, each frame in the field */
void portSlGalleryFrame(int inField);
/* $TTYD_SL_GALLERY2D (tests): images of the art on the 2D camera */
void portSlGallery2dFrame(int inField);

#ifdef __cplusplus
}
#endif
