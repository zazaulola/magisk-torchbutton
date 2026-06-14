package me.nogrep.torchbutton;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/** Shared atomic single-byte file writer for the daemon-visible state files. */
final class FileIO {
    private FileIO() {}

    /**
     * Write one byte to dir/name atomically: write to name+".tmp", fsync, make
     * it world-readable (0644 so the root daemon can read it), then rename over
     * the target. The daemon polling the file therefore never observes a
     * truncated or zero-length file (which it would otherwise read as a default).
     */
    static void writeByteAtomically(File dir, String name, int b) {
        File target = new File(dir, name);
        File tmp = new File(dir, name + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(b);
            out.flush();
            out.getFD().sync();
        } catch (IOException e) {
            // best-effort: fall back to a direct (non-atomic) write below
            tmp.delete();
            try (FileOutputStream out = new FileOutputStream(target)) {
                out.write(b);
            } catch (IOException ignored) { /* daemon falls back to its default */ }
            target.setReadable(true, /* ownerOnly */ false);
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        tmp.setReadable(true, /* ownerOnly */ false);
        if (!tmp.renameTo(target)) {
            // rename failed (shouldn't, same dir) — direct write fallback
            try (FileOutputStream out = new FileOutputStream(target)) {
                out.write(b);
            } catch (IOException ignored) {}
            target.setReadable(true, false);
            tmp.delete();
        }
    }
}
