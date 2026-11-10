package dev.ttyd.port;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.speech.tts.TextToSpeech;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.Charset;
import java.util.Locale;

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
    private TtydCompanion companion; // the second screen (TtydCompanion.java)

    /*
     * Text to speech (port/settings/features/speech.cpp): the system's voice,
     * made on the UI thread the first time a line comes, in the game's
     * language; the game's text decoded from its own encoding (Shift-JIS on
     * the Japanese release, Windows-1252 on the others). Each new line cuts
     * off the last.
     */
    private static TextToSpeech sSpeech;
    private static boolean sSpeechReady;

    public static void speechSay(byte[] text, boolean japanese, String language, float rate) {
        final String words = new String(text, Charset.forName(japanese ? "Shift_JIS" : "windows-1252"));
        final Context context = SDLActivity.getContext();
        if (context == null || words.isEmpty() || !(context instanceof TtydActivity)) {
            return;
        }
        ((TtydActivity) context).runOnUiThread(() -> {
            if (sSpeech == null) {
                sSpeech = new TextToSpeech(context, status -> {
                    sSpeechReady = status == TextToSpeech.SUCCESS;
                    if (sSpeechReady) {
                        sSpeech.setLanguage(Locale.forLanguageTag(language));
                        sSpeech.setSpeechRate(rate);
                        sSpeech.speak(words, TextToSpeech.QUEUE_FLUSH, null, "paper-cut");
                    }
                });
                return; // (the first line once it's ready)
            }
            if (sSpeechReady) {
                sSpeech.setLanguage(Locale.forLanguageTag(language));
                sSpeech.setSpeechRate(rate);
                sSpeech.speak(words, TextToSpeech.QUEUE_FLUSH, null, "paper-cut");
            }
        });
    }

    public static void speechStop() {
        final Context context = SDLActivity.getContext();
        if (context instanceof TtydActivity) {
            ((TtydActivity) context).runOnUiThread(() -> {
                if (sSpeech != null && sSpeechReady) {
                    sSpeech.stop();
                }
            });
        }
    }

    @Override
    protected String[] getLibraries() {
        // SDL3 is inside libmain.so (AURORA_SDL3_LINKAGE=static).
        return new String[] {"main"};
    }

    /*
     * Invite links (papercut://join/<CODE>, the manifest's intent filter):
     * the whole link to the game as SDL's drop event
     * (port/settings/features/invite.cpp), at the start and while running
     * (singleInstance: a link to the running app comes as a new intent).
     * SDL's own drop of the intent's data is only the link's path.
     */
    private static void passInvite(Intent intent) {
        if (intent != null && intent.getData() != null && "papercut".equals(intent.getData().getScheme())) {
            SDLActivity.onNativeDropFile(intent.getDataString());
        }
    }

    /*
     * Full screen (the system_bars setting off, port/settings/features/window.cpp):
     * the game's SDL window is fullscreen, and SDL's COMMAND_CHANGE_WINDOW_STYLE
     * hides the status and navigation bars (immersive sticky). It sets that once,
     * when the window style changes; after the disc picker, Android's installer
     * or settings, the keyboard, a rotation or a dialog the bars can stay shown,
     * so they are hidden again here (WindowInsetsController on Android 11+, a
     * swipe from an edge shows them for a moment). The picture draws edge to
     * edge and into the display cutout; the on-screen controls keep to the safe
     * area SDL reports (port/src/aurora/layout_aurora.c), as on iOS.
     */
    private void useCutout() {
        Window window = getWindow();
        WindowManager.LayoutParams lp = window.getAttributes();
        int mode = Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                                               : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        if (lp.layoutInDisplayCutoutMode != mode) {
            // (SDL sets the field without setAttributes, so it may not take)
            lp.layoutInDisplayCutoutMode = mode;
            window.setAttributes(lp);
        }
    }

    private final Runnable applySystemBars = new Runnable() {
        @Override
        public void run() {
            Window window = getWindow();
            if (window == null) {
                return;
            }
            useCutout();
            boolean full = SDLActivity.mFullscreenModeActive;
            if (Build.VERSION.SDK_INT >= 30) {
                window.setDecorFitsSystemWindows(!full);
                WindowInsetsController c = window.getInsetsController();
                if (c != null && full) {
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    c.hide(WindowInsets.Type.systemBars());
                } else if (c != null) {
                    c.show(WindowInsets.Type.systemBars());
                }
            } else if (full) {
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
        }
    };

    private void reapplySystemBars() {
        View decor = getWindow() != null ? getWindow().getDecorView() : null;
        if (decor != null) {
            decor.removeCallbacks(applySystemBars);
            decor.post(applySystemBars);
        }
    }

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        useCutout(); // before SDL's setContentView
        super.onCreate(savedInstanceState);
        passInvite(getIntent());
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            reapplySystemBars();
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        reapplySystemBars(); // (a rotation, a keyboard attached: the activity isn't remade)
    }

    /**
     * SDL's window style changed (the setting, the window made) or the bars
     * came back: follow it. While the keyboard is up, SDL's own re-hide (2 s)
     * and the keyboard's closing (createSDLSurface) take care of it.
     */
    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        if (!keyboardShown) {
            reapplySystemBars();
        }
    }

    private boolean keyboardShown; // (Android 11+: the window's insets say)

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        passInvite(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (companion == null) {
            companion = new TtydCompanion(this);
        }
        companion.start();
        reapplySystemBars();
    }

    @Override
    protected void onPause() {
        if (companion != null) {
            companion.stop();
        }
        super.onPause();
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        // Tells Aurora when the surface can be drawn to (it drops and rebuilds
        // its swapchain around surfaceDestroyed / surfaceChanged).
        return new AuroraSurface(context) {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                WindowInsets result = super.onApplyWindowInsets(v, insets);
                if (Build.VERSION.SDK_INT >= 30) {
                    // the keyboard closed (the launcher's text entry): the bars hidden again
                    boolean shown = insets.isVisible(WindowInsets.Type.ime());
                    if (keyboardShown && !shown) {
                        reapplySystemBars();
                    }
                    keyboardShown = shown;
                } else if (!SDLActivity.mBrokenLibraries) {
                    // Android 9 and 10: SDL reports no safe area there; the
                    // cutout's keeps the controls off the notch
                    DisplayCutout cutout = insets.getDisplayCutout();
                    if (cutout != null) {
                        SDLActivity.onNativeInsetsChanged(cutout.getSafeInsetLeft(), cutout.getSafeInsetRight(),
                                                          cutout.getSafeInsetTop(), cutout.getSafeInsetBottom());
                    } else {
                        SDLActivity.onNativeInsetsChanged(0, 0, 0, 0);
                    }
                }
                return result;
            }
        };
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

    /**
     * A content:// URI the picker gave (port/android/native/android_pc.c,
     * portAndroidContentInfo): null if it can't be read now (gone, or the
     * grant lost), else its size and its name as the file manager shows it,
     * "size\nname". Such a URI is no file path: stat() can't see it, and it
     * often holds no file name (".../document/msf%3A1234"). Called on the
     * game's threads.
     */
    public static String contentInfo(String address) {
        final Context context = SDLActivity.getContext();
        if (context == null || address == null) {
            return null;
        }
        Uri uri = Uri.parse(address);
        android.content.ContentResolver resolver = context.getContentResolver();
        long size = -1;
        String name = null;
        try (android.os.ParcelFileDescriptor fd = resolver.openFileDescriptor(uri, "r")) {
            if (fd == null) {
                return null;
            }
            size = fd.getStatSize();
        } catch (Exception e) {
            Log.w(TAG, "can't read " + address + ": " + e);
            return null;
        }
        String[] columns = {android.provider.OpenableColumns.DISPLAY_NAME, android.provider.OpenableColumns.SIZE};
        try (android.database.Cursor row = resolver.query(uri, columns, null, null, null)) {
            if (row != null && row.moveToFirst()) {
                if (!row.isNull(0)) {
                    name = row.getString(0);
                }
                if (size < 0 && !row.isNull(1)) {
                    size = row.getLong(1);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "no name for " + address + ": " + e);
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment() != null ? uri.getLastPathSegment() : "";
            int slash = name.lastIndexOf('/');
            name = slash >= 0 ? name.substring(slash + 1) : name;
        }
        return Math.max(size, 0) + "\n" + name.replace('\n', ' ');
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

    /**
     * An https:// POST for port/settings/http.cpp (the relay's paper notes and
     * leaderboards), as httpGet: body sent with contentType, the answer's body
     * to httpData whatever its status. Returns the status, or -1 when no answer
     * came (no redirects: a POST's answer is the relay's own).
     */
    public static int httpPost(String address, byte[] body, String contentType, long handle) {
        HttpURLConnection connection = null;
        try {
            URL url = new URL(address);
            if (!"https".equals(url.getProtocol())) {
                return -1;
            }
            connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(30000);
            connection.setInstanceFollowRedirects(false);
            connection.setUseCaches(false);
            connection.setDoOutput(true);
            connection.setRequestMethod("POST");
            connection.setRequestProperty("User-Agent", "PaperCut");
            connection.setRequestProperty("Content-Type", contentType != null ? contentType : "application/octet-stream");
            connection.setFixedLengthStreamingMode(body.length);
            try (java.io.OutputStream out = connection.getOutputStream()) {
                out.write(body);
            }
            int status = connection.getResponseCode();
            long total = Math.max(connection.getContentLengthLong(), 0);
            byte[] buffer = new byte[65536];
            InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            if (stream != null) {
                try (InputStream in = stream) {
                    int n;
                    while ((n = in.read(buffer)) >= 0) {
                        if (n > 0 && !httpData(handle, buffer, n, total)) {
                            return -1;
                        }
                    }
                }
            }
            return status;
        } catch (Exception e) {
            Log.w(TAG, "post to " + address + " failed", e);
            return -1;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    /*
     * Updates (port/settings/features/update_check.cpp, docs/UPDATES.md): the
     * release's APK for this app, downloaded to the cache and checked against
     * the release's SHA-256 by the game, then handed to Android's own
     * installer with a PackageInstaller session. Android always asks: the
     * session requires the player's action, so the player taps Install in
     * Android's dialog (never a silent install). Android installs it over
     * this app only when both are signed with the same key.
     */
    private static final String ACTION_INSTALL_RESULT = "dev.ttyd.port.INSTALL_RESULT";
    private static BroadcastReceiver sInstallReceiver;
    private static volatile String sInstallResult; // "code\nmessage" for installResult, once

    /**
     * The APK at path to Android's installer, from the game's threads: 0 when
     * Android's dialog is coming, 1 when this app may not install apps yet
     * (Android's "Install unknown apps" screen for it is opened instead), -1
     * when it failed.
     */
    public static int installApk(String path) {
        final Context context = SDLActivity.getContext();
        if (!(context instanceof TtydActivity) || path == null) {
            return -1;
        }
        final TtydActivity activity = (TtydActivity) context;
        if (!activity.getPackageManager().canRequestPackageInstalls()) {
            activity.runOnUiThread(() -> {
                try {
                    activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.parse("package:" + activity.getPackageName())));
                } catch (Exception e) {
                    Log.w(TAG, "no Install unknown apps screen", e);
                }
            });
            return 1;
        }
        sInstallResult = null;
        try {
            activity.listenForInstall();
            File file = new File(path);
            PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams params =
                    new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            params.setAppPackageName(activity.getPackageName());
            params.setSize(file.length());
            if (Build.VERSION.SDK_INT >= 31) {
                // (an app that installed itself could otherwise update itself without asking)
                params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED);
            }
            int id = installer.createSession(params);
            try (PackageInstaller.Session session = installer.openSession(id)) {
                try (InputStream in = new FileInputStream(file);
                     OutputStream out = session.openWrite("base.apk", 0, file.length())) {
                    byte[] buffer = new byte[65536];
                    int n;
                    while ((n = in.read(buffer)) >= 0) {
                        out.write(buffer, 0, n);
                    }
                    session.fsync(out);
                }
                Intent result = new Intent(ACTION_INSTALL_RESULT).setPackage(activity.getPackageName());
                // mutable: the installer adds the status to it
                int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                PendingIntent pending = PendingIntent.getBroadcast(activity, id, result, flags);
                session.commit(pending.getIntentSender());
            }
            return 0;
        } catch (Exception e) {
            Log.w(TAG, "the update's install failed", e);
            return -1;
        }
    }

    /** What became of installApk's install, once: "code\nmessage" (android_pc.h, portAndroidInstallResult), or null. */
    public static String installResult() {
        String result = sInstallResult;
        sInstallResult = null;
        return result;
    }

    // The session's answers: Android's dialog to show, or how it ended.
    private void listenForInstall() {
        if (sInstallReceiver != null) {
            return;
        }
        sInstallReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
                String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                Log.i(TAG, "update install: status " + status + (message != null ? " (" + message + ")" : ""));
                switch (status) {
                case PackageInstaller.STATUS_PENDING_USER_ACTION: {
                    Intent confirm = Build.VERSION.SDK_INT >= 33
                            ? intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class)
                            : intent.getParcelableExtra(Intent.EXTRA_INTENT);
                    if (confirm != null) {
                        try {
                            startActivity(confirm);
                        } catch (Exception e) {
                            Log.w(TAG, "can't show Android's install dialog", e);
                            sInstallResult = "4\n" + e;
                        }
                    }
                    break;
                }
                case PackageInstaller.STATUS_SUCCESS:
                    break; // (this app is replaced and ends)
                case PackageInstaller.STATUS_FAILURE_ABORTED:
                    sInstallResult = "3\n";
                    break;
                case PackageInstaller.STATUS_FAILURE_CONFLICT:
                case PackageInstaller.STATUS_FAILURE_INCOMPATIBLE:
                    sInstallResult = "2\n" + (message != null ? message : "");
                    break;
                default:
                    sInstallResult = "4\n" + (message != null ? message : "status " + status);
                    break;
                }
            }
        };
        IntentFilter filter = new IntentFilter(ACTION_INSTALL_RESULT);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(sInstallReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(sInstallReceiver, filter);
        }
    }

    @Override
    protected void onDestroy() {
        if (sInstallReceiver != null) {
            try {
                unregisterReceiver(sInstallReceiver);
            } catch (Exception e) {
                // (not registered with this activity)
            }
            sInstallReceiver = null;
        }
        super.onDestroy();
    }

    /** A chunk of httpGet's or httpPost's body (port/settings/http.cpp registers it); false stops the transfer. */
    private static native boolean httpData(long handle, byte[] data, int length, long total);
}
