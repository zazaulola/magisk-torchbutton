package me.nogrep.torchbutton;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * Mirrors the *real* flashlight LED state into files/torch_state so the native
 * torchd daemon can read it (it has no other way to know — on Pixel/Tensor the
 * torch is behind the Camera HAL, not a sysfs node).
 *
 * One character: '1' = on, '0' = off. World-readable (0644) so the root daemon
 * can read it, same as {@link EnableState}.
 */
public final class TorchState {
    private static final String FILE_NAME = "torch_state";

    private TorchState() {}

    public static File file(Context ctx) {
        return new File(ctx.getFilesDir(), FILE_NAME);
    }

    public static void write(Context ctx, boolean on) {
        File f = file(ctx);
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(on ? '1' : '0');
        } catch (IOException e) {
            /* daemon falls back to its in-memory guess */
        }
        //noinspection ResultOfMethodCallIgnored
        f.setReadable(true, /* ownerOnly */ false);
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
