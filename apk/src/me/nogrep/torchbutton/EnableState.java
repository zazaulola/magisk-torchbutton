package me.nogrep.torchbutton;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * The on/off flag, shared between the UI (MainActivity, TorchToggleTileService)
 * and the native torchd daemon.
 *
 * Stored in DEVICE-ENCRYPTED storage (/data/user_de/0/<pkg>/files/enabled) so
 * the root daemon can read it even before the first unlock (BFU) — credential-
 * encrypted /data/data isn't available then. One character: '1' enabled, '0'
 * disabled. Missing file => enabled (a fresh install works without opening the
 * app). World-readable (0644) so the root daemon can read it. Writes are atomic
 * (temp + rename) so the daemon never sees a half-written / zero-length file.
 */
public final class EnableState {
    private static final String FILE_NAME = "enabled";

    private EnableState() {}

    static File dir(Context ctx) {
        Context de = ctx.isDeviceProtectedStorage()
                ? ctx : ctx.createDeviceProtectedStorageContext();
        return de.getFilesDir();
    }

    public static File file(Context ctx) {
        return new File(dir(ctx), FILE_NAME);
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
        FileIO.writeByteAtomically(dir(ctx), FILE_NAME, enabled ? '1' : '0');
    }
}
