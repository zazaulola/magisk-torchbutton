package me.nogrep.torchbutton;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.util.Log;

/**
 * Receiver invoked by the native torchd daemon via:
 *   am broadcast -a me.nogrep.torchbutton.SET --es state on|off \
 *               -p me.nogrep.torchbutton --include-stopped-packages
 *
 * setTorchMode() does NOT require the CAMERA permission since API 23, so
 * this needs no permissions in the manifest.
 */
public class TorchReceiver extends BroadcastReceiver {
    private static final String TAG = "TorchReceiver";
    private static final String ACTION = "me.nogrep.torchbutton.SET";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION.equals(intent.getAction())) {
            return;
        }
        String state = intent.getStringExtra("state");
        if (state == null) {
            Log.w(TAG, "missing state extra");
            return;
        }
        boolean enable;
        if ("on".equalsIgnoreCase(state)) enable = true;
        else if ("off".equalsIgnoreCase(state)) enable = false;
        else { Log.w(TAG, "unknown state: " + state); return; }

        CameraManager cm = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
        if (cm == null) { Log.e(TAG, "no CameraManager"); return; }

        try {
            String[] ids = cm.getCameraIdList();
            for (String id : ids) {
                Boolean hasFlash = cm.getCameraCharacteristics(id)
                        .get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                if (Boolean.TRUE.equals(hasFlash)) {
                    cm.setTorchMode(id, enable);
                    Log.i(TAG, "torch " + (enable ? "ON" : "OFF") + " on camera " + id);
                    return;
                }
            }
            Log.w(TAG, "no camera reports a flash unit");
        } catch (CameraAccessException e) {
            Log.e(TAG, "setTorchMode failed", e);
        } catch (IllegalArgumentException e) {
            Log.e(TAG, "setTorchMode illegal arg", e);
        }
    }
}
