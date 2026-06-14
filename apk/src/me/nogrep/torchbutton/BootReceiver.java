package me.nogrep.torchbutton;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;

/**
 * Seeds files/torch_state at boot so the daemon knows the real LED state without
 * the user having to open the app or the QS shade first. Registering the torch
 * callback reports the current state immediately; we keep it briefly, then let
 * the process go. Handles both LOCKED_BOOT_COMPLETED (direct boot, DE storage is
 * available) and BOOT_COMPLETED.
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(final Context context, Intent intent) {
        final PendingResult pr = goAsync();
        TorchWatch.start(context);   // immediate current-state callback -> writes file
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override public void run() {
                TorchWatch.stop(context);
                pr.finish();
            }
        }, 1500);
    }
}
