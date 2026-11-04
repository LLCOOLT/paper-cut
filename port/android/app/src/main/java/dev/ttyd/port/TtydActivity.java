package dev.ttyd.port;

import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

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

    /**
     * An https:// GET for port/settings/http.cpp (the update check, the mod
     * browser), called on a thread of the game's own: the body goes to
     * httpData in chunks as it arrives, for the transfer at handle. Returns
     * null once it all arrived, else why not. Redirects are followed only
     * to https:// (HttpURLConnection never follows a change of protocol).
     */
    public static String httpGet(String address, String accept, long handle) {
        HttpURLConnection connection = null;
        try {
            URL url = new URL(address);
            if (!"https".equals(url.getProtocol())) {
                return "not an https:// address";
            }
            connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(30000);
            connection.setInstanceFollowRedirects(true);
            connection.setUseCaches(false);
            connection.setRequestProperty("User-Agent", "PaperCut");
            if (accept != null) {
                connection.setRequestProperty("Accept", accept);
            }
            int status = connection.getResponseCode();
            if (status != HttpURLConnection.HTTP_OK) {
                return "the server answered " + status;
            }
            if (!"https".equals(connection.getURL().getProtocol())) {
                return "redirected away from https://";
            }
            long total = Math.max(connection.getContentLengthLong(), 0);
            byte[] buffer = new byte[65536];
            try (InputStream in = connection.getInputStream()) {
                int n;
                while ((n = in.read(buffer)) >= 0) {
                    if (n > 0 && !httpData(handle, buffer, n, total)) {
                        return "stopped";
                    }
                }
            }
            return null;
        } catch (Exception e) {
            Log.w(TAG, "download of " + address + " failed", e);
            String message = e.getMessage();
            return message != null ? message : e.toString();
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    /** A chunk of httpGet's body (port/settings/http.cpp registers it); false stops the transfer. */
    private static native boolean httpData(long handle, byte[] data, int length, long total);
}
