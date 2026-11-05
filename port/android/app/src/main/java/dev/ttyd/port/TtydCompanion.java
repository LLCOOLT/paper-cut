package dev.ttyd.port;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.Log;
import android.view.Display;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;

/**
 * The second screen on Android (port/include/port/companion.h): on a device
 * with a second display of its own (a dual-screen handheld such as the AYN
 * Thor; not an external monitor or a cast screen), a Presentation there with
 * the game's own Start menu, live while the game plays on
 * (port/include/port/companion_menu.h): its picture with buttons under it, a
 * tap on the picture the menu's own (a tab, a row; again on the selected one
 * is A). Until the first picture (or without readback), panels drawn from
 * the game's snapshot. Display > Second screen (shown only on such a device)
 * turns it off.
 *
 * A tap only queues input (tap()): the game reads it with controller 1 and
 * acts on it there, so online co-op sees the same input. A tap on an item
 * uses it on Mario; a long press, on the partner with him.
 */
final class TtydCompanion implements DisplayManager.DisplayListener {
    private static final String TAG = "ttyd";
    private static final long REFRESH_MS = 200;

    // port/android/native/companion_android.c
    private static native boolean enabled();
    private static native void setActive(boolean active);
    private static native String snapshot();
    private static native void tap(int kind, int arg, int target);
    private static native String tr(String english);
    private static native boolean menuReady();
    private static native int menuPicture(int[] argb, int seen);
    private static native void menuTap(float x, float y);
    private static native void menuButton(int button);
    private static native void setDualScreen(boolean dual);

    // port/include/port/companion_menu.h
    private static final int MENU_W = 640, MENU_H = 480;
    private static final int BUTTON_A = 1, BUTTON_B = 2, BUTTON_UP = 3, BUTTON_DOWN = 4, BUTTON_LEFT = 5,
            BUTTON_RIGHT = 6, BUTTON_L = 7, BUTTON_R = 8, BUTTON_X = 9, BUTTON_Y = 10, BUTTON_Z = 11;
    private static final long MENU_REFRESH_MS = 33;

    private static final int TAP_ITEM = 1;
    private static final int TAP_PARTNER = 2;

    private final Activity activity;
    private final DisplayManager displays;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Screen screen;
    private boolean running;

    TtydCompanion(Activity activity) {
        this.activity = activity;
        this.displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }

    /** The activity is in front: the companion on a second display, if there is one and it's on. */
    void start() {
        running = true;
        displays.registerDisplayListener(this, handler);
        update();
        handler.postDelayed(tick, REFRESH_MS);
    }

    /** The activity is going away from the front. */
    void stop() {
        running = false;
        handler.removeCallbacks(tick);
        displays.unregisterDisplayListener(this);
        dismiss();
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            update();
            boolean menu = false;
            if (screen != null) {
                menu = screen.view.refresh();
            }
            handler.postDelayed(this, menu ? MENU_REFRESH_MS : REFRESH_MS);
        }
    };

    /**
     * A second display of the device's own: built in (Display.getType(),
     * TYPE_INTERNAL, read by reflection; it isn't public), not an external
     * monitor (HDMI, USB-C), a wireless or cast screen, an overlay or a
     * virtual display (another app's, a screen recorder's). Where the type
     * can't be read: public, not a presentation-only virtual one, and not
     * named as one of those.
     */
    private static boolean builtIn(Display d) {
        try {
            java.lang.reflect.Method type = Display.class.getMethod("getType");
            return ((Integer) type.invoke(d)) == 1; // Display.TYPE_INTERNAL
        } catch (Exception | LinkageError e) {
            if ((d.getFlags() & Display.FLAG_PRIVATE) != 0) {
                return false;
            }
            String name = d.getName() != null ? d.getName().toLowerCase(java.util.Locale.ROOT) : "";
            return !(name.contains("hdmi") || name.contains("wifi") || name.contains("wireless")
                    || name.contains("cast") || name.contains("overlay") || name.contains("virtual")
                    || name.contains("usb"));
        }
    }

    private Display secondDisplay() {
        Display[] list = displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        for (Display d : list) {
            if (d.getDisplayId() != Display.DEFAULT_DISPLAY && builtIn(d)) {
                return d;
            }
        }
        return null;
    }

    private void update() {
        boolean on;
        Display second = secondDisplay();
        try {
            setDualScreen(second != null); // the setting shows only then
            on = enabled();
        } catch (UnsatisfiedLinkError e) {
            return; // the game's library isn't loaded yet
        }
        Display d = on ? second : null;
        if (screen != null && (d == null || screen.getDisplay().getDisplayId() != d.getDisplayId())) {
            dismiss();
        }
        if (d != null && screen == null) {
            try {
                screen = new Screen(activity, d);
                screen.show();
                setActive(true);
                Log.i(TAG, "companion on display " + d.getDisplayId() + " (" + d.getName() + ")");
            } catch (WindowManager.InvalidDisplayException e) {
                Log.w(TAG, "companion: the second display went away", e);
                screen = null;
            }
        }
    }

    private void dismiss() {
        if (screen != null) {
            screen.dismiss();
            screen = null;
            setActive(false);
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        update();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        update();
    }

    @Override
    public void onDisplayChanged(int displayId) {
    }

    // --- The snapshot, from the game's text (companion_pc.c portCompanionText) ---

    static final class Member {
        int id, hp, maxHp, rank, out;
        String name;
    }

    static final class Item {
        int index, id, hp, fp;
        boolean usable;
        String name;
    }

    static final class Enemy {
        boolean known, hpShown, acting;
        int hp, maxHp, defense;
        String name, text;
    }

    static final class Snap {
        int scene, serial;
        boolean ready;
        int level, starPoints, coins, starPieces, shineSprites, fp, maxFp, bp, maxBp, sp, maxSp, crystals;
        final List<String> crystalNames = new ArrayList<>();
        final List<Member> members = new ArrayList<>();
        final List<Item> items = new ArrayList<>();
        final List<String> badges = new ArrayList<>();
        int badgeTotal;
        String map = "", area = "", hint = "";
        String coinName = "", pieceName = "", spriteName = ""; // the game's own
        int pieces, piecesTotal, sprites, spritesTotal;
        float minX, minZ, maxX, maxZ;
        final List<float[]> dots = new ArrayList<>();
        int turn;
        final List<Enemy> enemies = new ArrayList<>();
        final List<String> order = new ArrayList<>();
        int orderActive = -1;

        static int i(String[] f, int k) {
            try {
                return k < f.length ? Integer.parseInt(f[k]) : 0;
            } catch (NumberFormatException e) {
                return 0;
            }
        }

        static float fl(String[] f, int k) {
            try {
                return k < f.length ? Float.parseFloat(f[k]) : 0;
            } catch (NumberFormatException e) {
                return 0;
            }
        }

        static String s(String[] f, int k) {
            return k < f.length ? f[k].replace('\u001f', '\n') : "";
        }

        static Snap parse(String text) {
            Snap s = new Snap();
            for (String line : text.split("\n")) {
                String[] f = line.split("\t", -1);
                switch (f[0]) {
                    case "scene":
                        s.scene = i(f, 1);
                        s.ready = i(f, 2) != 0;
                        s.serial = i(f, 3);
                        break;
                    case "stats":
                        s.level = i(f, 1);
                        s.starPoints = i(f, 2);
                        s.coins = i(f, 3);
                        s.starPieces = i(f, 4);
                        s.shineSprites = i(f, 5);
                        s.fp = i(f, 6);
                        s.maxFp = i(f, 7);
                        s.bp = i(f, 8);
                        s.maxBp = i(f, 9);
                        s.sp = i(f, 10);
                        s.maxSp = i(f, 11);
                        s.crystals = i(f, 12);
                        break;
                    case "names":
                        s.coinName = s(f, 1);
                        s.pieceName = s(f, 2);
                        s.spriteName = s(f, 3);
                        break;
                    case "crystal":
                        s.crystalNames.add(s(f, 1));
                        break;
                    case "member": {
                        Member m = new Member();
                        m.id = i(f, 1);
                        m.hp = i(f, 2);
                        m.maxHp = i(f, 3);
                        m.rank = i(f, 4);
                        m.out = i(f, 5);
                        m.name = s(f, 6);
                        s.members.add(m);
                        break;
                    }
                    case "item": {
                        Item it = new Item();
                        it.index = i(f, 1);
                        it.id = i(f, 2);
                        it.hp = i(f, 3);
                        it.fp = i(f, 4);
                        it.usable = i(f, 5) != 0;
                        it.name = s(f, 6);
                        s.items.add(it);
                        break;
                    }
                    case "badges":
                        s.badgeTotal = i(f, 1);
                        break;
                    case "badge":
                        s.badges.add(i(f, 1) > 1 ? s(f, 3) + " x" + i(f, 1) : s(f, 3));
                        break;
                    case "map":
                        s.map = s(f, 1);
                        s.area = s(f, 2);
                        s.hint = s(f, 3);
                        break;
                    case "collect":
                        s.pieces = i(f, 1);
                        s.piecesTotal = i(f, 2);
                        s.sprites = i(f, 3);
                        s.spritesTotal = i(f, 4);
                        break;
                    case "box":
                        s.minX = fl(f, 1);
                        s.minZ = fl(f, 2);
                        s.maxX = fl(f, 3);
                        s.maxZ = fl(f, 4);
                        break;
                    case "dot":
                        s.dots.add(new float[] {i(f, 1), fl(f, 2), fl(f, 3)});
                        break;
                    case "turn":
                        s.turn = i(f, 1);
                        break;
                    case "enemy": {
                        Enemy e = new Enemy();
                        e.known = i(f, 1) != 0;
                        e.hpShown = i(f, 2) != 0;
                        e.hp = i(f, 3);
                        e.maxHp = i(f, 4);
                        e.defense = i(f, 5);
                        e.acting = i(f, 6) != 0;
                        e.name = s(f, 7);
                        e.text = s(f, 8);
                        s.enemies.add(e);
                        break;
                    }
                    case "order":
                        if (i(f, 1) != 0) {
                            s.orderActive = s.order.size();
                        }
                        s.order.add(s(f, 2));
                        break;
                    default:
                        break;
                }
            }
            return s;
        }
    }

    // --- The presentation ---

    private static final class Screen extends Presentation {
        final Page view;

        Screen(Activity owner, Display display) {
            super(owner, display);
            view = new Page(getContext());
        }

        @Override
        protected void onCreate(Bundle state) {
            super.onCreate(state);
            // touches there leave the game's window with the focus (and its input)
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
            setContentView(view);
        }
    }

    /** A tappable place on the page. */
    private static final class Hit {
        final RectF r;
        final int kind, arg;

        Hit(RectF r, int kind, int arg) {
            this.r = r;
            this.kind = kind;
            this.arg = arg;
        }
    }

    // the launcher's paper style (port/settings/launcher_style.h)
    private static final int INK = 0xFF3B2416, PAPER = 0xFFFFF7E4, CURTAIN = 0xFFC4202A, GOLD = 0xFFF6C445;
    private static final int NIGHT = 0xFF181C4A, TRACK = 0xFFDEC4A0, MUTED = 0xFF80624A, HP = 0xFFD62C34;
    private static final int FP = 0xFF408CDC, BUTTON = 0xFFECD6B0;

    private static final class Page extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final List<Hit> hits = new ArrayList<>();
        private final GestureDetector gestures;
        private final float dp;
        private Snap snap = new Snap();
        private int tab, lastScene = -1;
        // the Start menu's picture (companion_menu.h)
        private final int[] argb = new int[MENU_W * MENU_H];
        private final Bitmap picture = Bitmap.createBitmap(MENU_W, MENU_H, Bitmap.Config.ARGB_8888);
        private final Paint filtered = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
        private int seen;
        private boolean menuMode;
        private float scroll, contentHeight;

        Page(Context context) {
            super(context);
            dp = context.getResources().getDisplayMetrics().density;
            text.setColor(INK);
            gestures = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
                @Override
                public boolean onDown(MotionEvent e) {
                    return true;
                }

                @Override
                public boolean onSingleTapUp(MotionEvent e) {
                    press(e.getX(), e.getY(), false);
                    return true;
                }

                @Override
                public void onLongPress(MotionEvent e) {
                    press(e.getX(), e.getY(), true);
                }

                @Override
                public boolean onScroll(MotionEvent a, MotionEvent b, float dx, float dy) {
                    scroll = Math.max(0, Math.min(scroll + dy, Math.max(0, contentHeight - getHeight())));
                    invalidate();
                    return true;
                }
            });
        }

        /** True while it shows the Start menu's picture (refreshed often then). */
        boolean refresh() {
            try {
                if (menuReady()) {
                    int serial = menuPicture(argb, seen);
                    if (serial != 0) {
                        if (serial != seen) {
                            picture.setPixels(argb, 0, MENU_W, 0, 0, MENU_W, MENU_H);
                            seen = serial;
                            invalidate();
                        }
                        menuMode = true;
                        return true;
                    }
                }
                menuMode = false;
            } catch (UnsatisfiedLinkError e) {
                return false; // the game's library isn't loaded yet
            }
            try {
                Snap s = Snap.parse(snapshot());
                if (s.scene != lastScene) {
                    tab = s.scene == 2 ? 2 : 0; // a battle brings its tab forward; its end, the party's
                    lastScene = s.scene;
                    scroll = 0;
                }
                snap = s;
                invalidate();
            } catch (UnsatisfiedLinkError e) {
                // the game's library isn't loaded yet
            }
            return false;
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            return gestures.onTouchEvent(e) || super.onTouchEvent(e);
        }

        private void press(float x, float y, boolean longPress) {
            for (Hit h : hits) {
                if (!h.r.contains(x, y)) {
                    continue;
                }
                if (h.kind == MENU_PICTURE) {
                    menuTap((x - h.r.left) / h.r.width(), (y - h.r.top) / h.r.height());
                } else if (h.kind == MENU_BUTTON) {
                    menuButton(h.arg);
                } else if (h.kind == 0) {
                    tab = h.arg;
                    scroll = 0;
                } else if (h.kind == TAP_ITEM) {
                    tap(TAP_ITEM, h.arg, longPress ? 1 : 0);
                } else {
                    tap(TAP_PARTNER, h.arg, 0);
                }
                performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY);
                invalidate();
                return;
            }
        }

        // --- drawing helpers: y runs down the page (scrolled); each returns the next y ---

        private float x0, x1;

        private float line(Canvas c, String s, float y, float size, int color, boolean bold) {
            text.setTextSize(size * dp);
            text.setColor(color);
            text.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            StaticLayout l = StaticLayout.Builder.obtain(s, 0, s.length(), text, (int) (x1 - x0)).build();
            c.save();
            c.translate(x0, y);
            l.draw(c);
            c.restore();
            return y + l.getHeight() + 4 * dp;
        }

        private float bar(Canvas c, String label, int v, int max, int color, float y, float w) {
            y = line(c, label + " " + v + "/" + max, y, 14, INK, false);
            float h = 8 * dp;
            paint.setColor(TRACK);
            c.drawRoundRect(new RectF(x0, y, x0 + w, y + h), h / 2, h / 2, paint);
            float f = max > 0 ? Math.max(0, Math.min(1, v / (float) max)) : 0;
            if (f > 0) {
                paint.setColor(color);
                c.drawRoundRect(new RectF(x0, y, x0 + Math.max(w * f, h), y + h), h / 2, h / 2, paint);
            }
            return y + h + 8 * dp;
        }

        private void sparkle(Canvas c, float cx, float cy, float r, int color) {
            float k = r * 0.1875f;
            Path p = new Path();
            p.moveTo(cx, cy - r);
            p.lineTo(cx + k, cy - k);
            p.lineTo(cx + r, cy);
            p.lineTo(cx + k, cy + k);
            p.lineTo(cx, cy + r);
            p.lineTo(cx - k, cy + k);
            p.lineTo(cx - r, cy);
            p.lineTo(cx - k, cy - k);
            p.close();
            paint.setColor(color);
            c.drawPath(p, paint);
        }

        // a paper card (launcherPaperCard): a shadow, a white edge, the cream face
        private float card(Canvas c, float y, float height) {
            RectF r = new RectF(8 * dp, y, getWidth() - 8 * dp, y + height);
            paint.setColor(0x78140A1E);
            c.drawRoundRect(new RectF(r.left + 4 * dp, r.top + 5 * dp, r.right + 4 * dp, r.bottom + 5 * dp), 12 * dp, 12 * dp, paint);
            paint.setColor(0xFFFFFFFF);
            c.drawRoundRect(r, 12 * dp, 12 * dp, paint);
            paint.setColor(PAPER);
            c.drawRoundRect(new RectF(r.left + 3 * dp, r.top + 3 * dp, r.right - 3 * dp, r.bottom - 3 * dp), 10 * dp, 10 * dp, paint);
            return y + 12 * dp;
        }

        // a card whose content is drawn by body (measured first on a throwaway canvas)
        private interface Body {
            float draw(Canvas c, float y);
        }

        private final Canvas measure = new Canvas();

        private float section(Canvas c, float y, Body body) {
            List<Hit> keep = new ArrayList<>(hits);
            float end = body.draw(measure, y + 12 * dp);
            hits.clear();
            hits.addAll(keep);
            card(c, y, end - y + 8 * dp);
            body.draw(c, y + 12 * dp);
            return end + 20 * dp;
        }

        // --- the Start menu: its picture, 4:3, the buttons under it ---

        private static final int MENU_PICTURE = 100, MENU_BUTTON = 101;

        private void drawMenuButton(Canvas c, float cx, float cy, float r, String letter, int dir, int face, int button) {
            paint.setColor(0x5A000000);
            c.drawCircle(cx, cy + 2 * dp, r, paint);
            paint.setColor(face);
            c.drawCircle(cx, cy, r, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(2 * dp);
            paint.setColor(GOLD);
            c.drawCircle(cx, cy, r, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xFFFFFFFF);
            if (letter != null) {
                text.setTextSize(r * 1.1f);
                text.setTypeface(Typeface.DEFAULT_BOLD);
                text.setColor(0xFFFFFFFF);
                float w = text.measureText(letter);
                Paint.FontMetrics m = text.getFontMetrics();
                c.drawText(letter, cx - w / 2, cy - (m.ascent + m.descent) / 2, text);
            } else {
                float a = r * 0.45f;
                Path p = new Path();
                switch (dir) {
                    case 0: p.moveTo(cx, cy - a); p.lineTo(cx + a, cy + a * 0.7f); p.lineTo(cx - a, cy + a * 0.7f); break;
                    case 1: p.moveTo(cx, cy + a); p.lineTo(cx - a, cy - a * 0.7f); p.lineTo(cx + a, cy - a * 0.7f); break;
                    case 2: p.moveTo(cx - a, cy); p.lineTo(cx + a * 0.7f, cy - a); p.lineTo(cx + a * 0.7f, cy + a); break;
                    default: p.moveTo(cx + a, cy); p.lineTo(cx - a * 0.7f, cy + a); p.lineTo(cx - a * 0.7f, cy - a); break;
                }
                p.close();
                c.drawPath(p, paint);
            }
            hits.add(new Hit(new RectF(cx - r * 1.2f, cy - r * 1.2f, cx + r * 1.2f, cy + r * 1.2f), MENU_BUTTON, button));
        }

        private void drawMenu(Canvas c) {
            final float W = getWidth(), H = getHeight();
            c.drawColor(NIGHT);
            float barH = Math.min(96 * dp, H * 0.3f);
            float w = W, h = w * 0.75f;
            if (h > H - barH) {
                h = H - barH;
                w = h / 0.75f;
            }
            RectF r = new RectF((W - w) / 2, Math.max(0, (H - barH - h) / 2), (W + w) / 2, 0);
            r.bottom = r.top + h;
            c.drawBitmap(picture, null, r, filtered);
            hits.add(new Hit(r, MENU_PICTURE, 0));
            float rad = Math.min(barH * 0.3f, W / 30f);
            float cy = Math.min(H - barH / 2, r.bottom + barH / 2);
            float step = rad * 2.5f, x = rad * 1.6f;
            final int grey = 0xFF6E608C;
            drawMenuButton(c, x, cy, rad, "L", 0, grey, BUTTON_L);
            drawMenuButton(c, x += step, cy, rad, null, 2, grey, BUTTON_LEFT);
            drawMenuButton(c, x += step, cy, rad, null, 0, grey, BUTTON_UP);
            drawMenuButton(c, x += step, cy, rad, null, 1, grey, BUTTON_DOWN);
            drawMenuButton(c, x += step, cy, rad, null, 3, grey, BUTTON_RIGHT);
            drawMenuButton(c, x + step, cy, rad, "R", 0, grey, BUTTON_R);
            float right = W - rad * 1.6f;
            drawMenuButton(c, right - step * 4, cy, rad * 0.85f, "Z", 0, grey, BUTTON_Z); // the sort menu
            drawMenuButton(c, right - step * 3, cy, rad * 0.85f, "X", 0, grey, BUTTON_X);
            drawMenuButton(c, right - step * 2, cy, rad * 0.85f, "Y", 0, grey, BUTTON_Y);
            drawMenuButton(c, right - step, cy, rad, "B", 0, 0xFFDCB428, BUTTON_B);
            drawMenuButton(c, right, cy, rad * 1.15f, "A", 0, 0xFF28AA78, BUTTON_A);
        }

        @Override
        protected void onDraw(Canvas c) {
            hits.clear();
            if (menuMode) {
                drawMenu(c);
                return;
            }
            c.drawColor(NIGHT);
            paint.setColor(CURTAIN);
            c.drawRect(0, 0, getWidth(), 5 * dp, paint);
            paint.setColor(GOLD);
            c.drawRect(0, 5 * dp, getWidth(), 8 * dp, paint);
            x0 = 24 * dp;
            x1 = getWidth() - 24 * dp;
            final Snap s = snap;
            float y = 16 * dp;
            if (s.scene == 0) {
                section(c, y, (cv, yy) -> {
                    yy = line(cv, tr("Paper Cut companion"), yy, 18, INK, true);
                    return line(cv, tr("The Start menu's information, the map and the battle show here while a game runs."), yy, 14, MUTED, false);
                });
                return;
            }
            // the tabs (not scrolled)
            String[] names = {tr("Party"), tr("Map"), tr("Battle")};
            float tx = 12 * dp;
            text.setTextSize(15 * dp);
            text.setTypeface(Typeface.DEFAULT_BOLD);
            for (int t = 0; t < 3; t++) {
                float w = text.measureText(names[t]) + 28 * dp;
                RectF r = new RectF(tx, y, tx + w, y + 34 * dp);
                paint.setColor(t == tab ? PAPER : BUTTON);
                c.drawRoundRect(r, 10 * dp, 10 * dp, paint);
                text.setColor(INK);
                c.drawText(names[t], tx + 14 * dp, y + 23 * dp, text);
                hits.add(new Hit(r, 0, t));
                tx += w + 6 * dp;
            }
            float top = y + 44 * dp;
            c.save();
            c.clipRect(0, top, getWidth(), getHeight());
            c.translate(0, -scroll);
            int tabHits = hits.size();
            y = top;
            if (tab == 0) {
                y = party(c, s, y);
            } else if (tab == 1) {
                y = map(c, s, y);
            } else {
                y = battle(c, s, y);
            }
            c.restore();
            contentHeight = y + scroll;
            // the page's hits move with the scroll
            for (int k = tabHits; k < hits.size(); k++) {
                hits.get(k).r.offset(0, -scroll);
                if (hits.get(k).r.bottom < top) {
                    hits.get(k).r.setEmpty();
                }
            }
        }

        private Member partnerOut(Snap s) {
            for (Member m : s.members) {
                if (m.id != 0 && m.out == 1) {
                    return m;
                }
            }
            return null;
        }

        private float party(Canvas c, Snap s, float y) {
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, tr("Level") + " " + s.level + "    " + tr("Star Points") + " " + s.starPoints + "    "
                        + s.coinName + " " + s.coins, yy, 15, INK, true);
                yy = line(cv, s.pieceName + " " + s.starPieces + "    " + s.spriteName + " " + s.shineSprites, yy, 15, INK, true);
                yy = bar(cv, tr("FP"), s.fp, s.maxFp, FP, yy, (x1 - x0) * 0.5f);
                yy = line(cv, tr("BP left") + " " + s.bp + "/" + s.maxBp, yy, 14, INK, false);
                yy = starPower(cv, s, yy);
                yy = line(cv, tr("Crystal Stars"), yy, 14, INK, false);
                int[] colors = {0xFFAADCFF, 0xFF3CBE5A, 0xFFF6C445, 0xFFDC283C, 0xFF3264E6, 0xFF9632A0, 0xFFFFAADC};
                for (int k = 0; k < 7; k++) {
                    boolean found = ((s.crystals >> k) & 1) != 0;
                    sparkle(cv, x0 + 12 * dp + k * 28 * dp, yy + 12 * dp, (found ? 12 : 8) * dp, found ? colors[k] : TRACK);
                }
                return yy + 28 * dp;
            });
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, tr("Party"), yy, 17, INK, true);
                for (Member m : s.members) {
                    float rowTop = yy;
                    boolean canSwitch = m.id != 0 && m.out == 0 && s.ready;
                    if (m.out == 1) {
                        paint.setColor(0x6EF6C445);
                        cv.drawRoundRect(new RectF(x0 - 8 * dp, rowTop - 2 * dp, x1 + 8 * dp, rowTop + 46 * dp), 8 * dp, 8 * dp, paint);
                    }
                    float save = x1;
                    x1 = x0 + (x1 - x0) * 0.45f;
                    line(cv, m.name, rowTop, 16, m.out == -1 ? MUTED : INK, true);
                    if (m.out != 0 && m.id != 0) {
                        line(cv, m.out == 1 ? tr("With Mario") : tr("Away"), rowTop + 22 * dp, 12, MUTED, false);
                    }
                    x1 = save;
                    float gx = x0 + (x1 - x0) * 0.45f;
                    float keepX0 = x0;
                    x0 = gx;
                    bar(cv, tr("HP"), m.hp, m.maxHp, HP, rowTop, x1 - gx);
                    x0 = keepX0;
                    if (canSwitch) {
                        hits.add(new Hit(new RectF(x0 - 8 * dp, rowTop, x1 + 8 * dp, rowTop + 46 * dp), TAP_PARTNER, m.id));
                    }
                    yy = rowTop + 50 * dp;
                }
                if (s.members.size() > 1) {
                    yy = line(cv, tr("Tap a partner to switch."), yy, 12, MUTED, false);
                }
                return yy;
            });
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, tr("Items"), yy, 17, INK, true);
                if (s.items.isEmpty()) {
                    return line(cv, tr("The bag is empty."), yy, 13, MUTED, false);
                }
                float w = (x1 - x0 - 8 * dp) / 2, h = 40 * dp;
                for (int k = 0; k < s.items.size(); k++) {
                    Item it = s.items.get(k);
                    float bx = x0 + (k % 2) * (w + 8 * dp), by = yy + (k / 2) * (h + 8 * dp);
                    RectF r = new RectF(bx, by, bx + w, by + h);
                    boolean can = it.usable && s.ready;
                    paint.setColor(can ? BUTTON : 0xFFF4E4C8);
                    cv.drawRoundRect(r, 10 * dp, 10 * dp, paint);
                    text.setTextSize(14 * dp);
                    text.setTypeface(Typeface.DEFAULT);
                    text.setColor(can ? INK : MUTED);
                    float tw = text.measureText(it.name);
                    cv.drawText(it.name, bx + Math.max(6 * dp, (w - tw) / 2), by + 26 * dp, text);
                    if (can) {
                        hits.add(new Hit(r, TAP_ITEM, it.index));
                    }
                }
                yy += ((s.items.size() + 1) / 2) * (h + 8 * dp);
                Member out = partnerOut(s);
                String how = s.ready ? (out != null ? tr("Tap an item to use it; hold it for the partner.") : tr("Tap an item to use it."))
                        : tr("Items can be used in the field.");
                return line(cv, how, yy, 12, MUTED, false);
            });
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, tr("Badges worn") + " (" + s.badgeTotal + ")", yy, 17, INK, true);
                if (s.badges.isEmpty()) {
                    return line(cv, tr("No badges worn."), yy, 13, MUTED, false);
                }
                return line(cv, String.join(", ", s.badges), yy, 14, INK, false);
            });
            return y;
        }

        private float starPower(Canvas c, Snap s, float y) {
            int stars = s.maxSp / 100;
            y = line(c, tr("Star Power") + " " + (s.sp / 100) + "." + String.format("%02d", s.sp % 100) + "/" + stars, y, 14, INK, false);
            for (int k = 0; k < stars; k++) {
                float fill = Math.max(0, Math.min(1, (s.sp - k * 100) / 100f));
                float cx = x0 + 12 * dp + k * 26 * dp, cy = y + 11 * dp;
                sparkle(c, cx, cy, 11 * dp, 0xFFBE9C74);
                if (fill > 0) {
                    sparkle(c, cx, cy, 11 * dp * (0.35f + 0.65f * fill), fill >= 1 ? GOLD : 0xAAF6C445);
                }
            }
            return y + (stars > 0 ? 26 * dp : 0);
        }

        private float map(Canvas c, Snap s, float y) {
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, s.area.isEmpty() ? s.map : s.area, yy, 22, INK, true);
                yy = line(cv, s.map, yy, 13, MUTED, false);
                yy = line(cv, tr("Where next"), yy, 16, INK, true);
                return line(cv, s.hint.isEmpty() ? tr("The story is over: go wherever you like.") : s.hint, yy, 14, INK, false);
            });
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, tr("This room"), yy, 16, INK, true);
                if (s.piecesTotal + s.spritesTotal == 0) {
                    yy = line(cv, tr("No Star Pieces or Shine Sprites here."), yy, 13, MUTED, false);
                } else {
                    if (s.piecesTotal > 0) {
                        yy = line(cv, s.pieceName + ": " + s.pieces + "/" + s.piecesTotal, yy, 14, INK, false);
                    }
                    if (s.spritesTotal > 0) {
                        yy = line(cv, s.spriteName + ": " + s.sprites + "/" + s.spritesTotal, yy, 14, INK, false);
                    }
                }
                float bw = s.maxX - s.minX, bh = s.maxZ - s.minZ;
                if (bw > 1 && bh > 1 && !s.dots.isEmpty()) {
                    float w = x1 - x0, h = Math.max(80 * dp, Math.min(260 * dp, w * bh / bw));
                    float k = Math.min(w / bw, h / bh);
                    float ox = x0 + (w - bw * k) / 2, oy = yy + (h - bh * k) / 2;
                    paint.setColor(0xFFE8D2AC);
                    cv.drawRoundRect(new RectF(ox, oy, ox + bw * k, oy + bh * k), 8 * dp, 8 * dp, paint);
                    int[] kinds = {HP, GOLD, FP, INK};
                    for (float[] d : s.dots) {
                        float cx = ox + (d[1] - s.minX) * k, cy = oy + (d[2] - s.minZ) * k, r = (d[0] == 0 ? 6 : 4) * dp;
                        paint.setColor(0xFFFFFFFF);
                        cv.drawCircle(cx, cy, r + 1.5f * dp, paint);
                        paint.setColor(kinds[(int) d[0] & 3]);
                        cv.drawCircle(cx, cy, r, paint);
                    }
                    yy += h + 8 * dp;
                }
                return yy;
            });
            return y;
        }

        private float battle(Canvas c, Snap s, float y) {
            if (s.scene != 2) {
                return section(c, y, (cv, yy) -> line(cv, tr("No battle now."), yy, 14, MUTED, false));
            }
            y = section(c, y, (cv, yy) -> {
                yy = line(cv, tr("Turn") + " " + s.turn, yy, 17, INK, true);
                yy = starPower(cv, s, yy);
                if (!s.order.isEmpty()) {
                    yy = line(cv, tr("Turn order"), yy, 14, INK, false);
                    for (int k = 0; k < s.order.size(); k++) {
                        if (k == s.orderActive) {
                            paint.setColor(0x78F6C445);
                            cv.drawRoundRect(new RectF(x0 - 6 * dp, yy, x1, yy + 20 * dp), 6 * dp, 6 * dp, paint);
                        }
                        yy = line(cv, (k + 1) + ". " + s.order.get(k), yy, 14, INK, false);
                    }
                }
                return yy;
            });
            for (Enemy e : s.enemies) {
                y = section(c, y, (cv, yy) -> {
                    if (e.acting) {
                        sparkle(cv, x1 - 10 * dp, yy + 10 * dp, 10 * dp, GOLD);
                    }
                    yy = line(cv, e.name, yy, 17, INK, true);
                    if (e.hpShown) {
                        yy = bar(cv, tr("HP"), e.hp, e.maxHp, HP, yy, (x1 - x0) * 0.6f);
                    }
                    if (!e.known) {
                        return line(cv, tr("Not tattled yet."), yy, 13, MUTED, false);
                    }
                    if (e.text.isEmpty()) {
                        return line(cv, tr("Defense") + " " + e.defense, yy, 14, INK, false);
                    }
                    return line(cv, e.text, yy, 14, INK, false);
                });
            }
            return y;
        }
    }
}
