package dev.ttyd.port;

import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import dev.encounter.aurora.AuroraSurface;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * The game's activity: SDL's, with Aurora's surface and a disc pick that
 * outlives the process.
 *
 * libmain.so (the repo's CMake project) has SDL linked in statically and
 * Aurora's SDL_main, which runs the game (port/src/aurora/main_aurora.c).
 */
public class TtydActivity extends SDLActivity {
    private static final String TAG = "ttyd";

    @Override
    protected String[] getLibraries() {
        // SDL3 is inside libmain.so (AURORA_SDL3_LINKAGE=static).
        return new String[] {"main"};
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        // Tells Aurora when the surface can be drawn to (it drops and rebuilds
        // its swapchain around surfaceDestroyed / surfaceChanged).
        return new AuroraSurface(context);
    }

    /**
     * The disc picker (SDL_ShowOpenFileDialog, port/src/aurora/disc_aurora.c)
     * returns a content:// URI, which the game remembers in disc.txt. Android
     * grants access to it only until the app exits unless the grant is made
     * persistable here, before SDL hands the URI to the game.
     */
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK && data != null) {
            if (data.getData() != null) {
                keepAccess(data.getData());
            }
            ClipData clip = data.getClipData();
            if (clip != null) {
                for (int i = 0; i < clip.getItemCount(); i++) {
                    keepAccess(clip.getItemAt(i).getUri());
                }
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void keepAccess(Uri uri) {
        if (uri == null || !"content".equals(uri.getScheme())) {
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            Log.w(TAG, "no persistable access to " + uri + "; it must be picked again after a restart", e);
        }
    }
}
