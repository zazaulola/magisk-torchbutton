package me.nogrep.torchbutton;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * Mirrors the *real* flashlight LED state into files/torch_state so the native
 * torchd daemon can read it (on Pixel/Tensor the torch is behind the Camera HAL,
 * not a sysfs node it could poll).
 *
 * Device-encrypted storage (same as {@link EnableState}); the root daemon reads
 * it even before first unlock. One character: '1' on, '0' off. World-readable
 * (0644). Writes are atomic (temp + rename).
 */
public final class TorchState {
    private static final String FILE_NAME = "torch_state";

    private TorchState() {}

    static File dir(Context ctx) {
        Context de = ctx.isDeviceProtectedStorage()
                ? ctx : ctx.createDeviceProtectedStorageContext();
        return de.getFilesDir();
    }

    public static File file(Context ctx) {
        return new File(dir(ctx), FILE_NAME);
    }

    public static void write(Context ctx, boolean on) {
        FileIO.writeByteAtomically(dir(ctx), FILE_NAME, on ? '1' : '0');
    }

    public static boolean read(Context ctx) {
        File f = file(ctx);
        if (!f.exists()) return false;
        try (FileInputStream in = new FileInputStream(f)) {
            return in.read() == '1';
        } catch (IOException e) {
            return false;
        }
    }
}
