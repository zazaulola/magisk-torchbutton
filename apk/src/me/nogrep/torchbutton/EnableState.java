package me.nogrep.torchbutton;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * The on/off flag lives at getFilesDir()/enabled and is shared between the
 * UI (MainActivity, TorchToggleTileService) and the native torchd daemon
 * (which polls the file from /data/data/me.nogrep.torchbutton/files/enabled).
 *
 * The file contains a single character: '1' for enabled, '0' for disabled.
 * If the file is missing, both the daemon and this code default to enabled
 * — which means a fresh install behaves the way the user expected.
 */
public final class EnableState {
    private static final String FILE_NAME = "enabled";

    private EnableState() {}

    public static File file(Context ctx) {
        return new File(ctx.getFilesDir(), FILE_NAME);
    }

    public static boolean read(Context ctx) {
        File f = file(ctx);
        if (!f.exists()) return true;
        try (FileInputStream in = new FileInputStream(f)) {
            int b = in.read();
            return b != '0';
        } catch (IOException e) {
            return true;
        }
    }

    public static void write(Context ctx, boolean enabled) {
        File f = file(ctx);
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(enabled ? '1' : '0');
        } catch (IOException e) {
            /* nothing we can do; daemon defaults to enabled */
        }
        // File must be world-readable so torchd (root, magisk SELinux domain)
        // can read it without us having to relax SELinux further. On Android
        // the default mode of newly-created app files is 0600 — we need 0644.
        //noinspection ResultOfMethodCallIgnored
        f.setReadable(true, /* ownerOnly */ false);
    }
}
