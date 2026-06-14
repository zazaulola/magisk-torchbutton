package me.nogrep.torchbutton;

import android.content.Context;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/**
 * Keeps {@link TorchState} in sync with the real flashlight LED by listening to
 * CameraManager.registerTorchCallback(). The callback fires for ANY torch
 * change — our own setTorchMode(), the system flashlight Quick Settings tile,
 * other apps — and reports the current state immediately on registration.
 *
 * We register while a UI surface that can observe an external toggle is alive:
 *  - the QS tile while the shade is open (onStartListening/onStopListening) —
 *    this is where the user taps the system flashlight tile, so we catch it;
 *  - MainActivity while it is in the foreground.
 *
 * Ref-counted so overlapping registrations from both don't double-register.
 */
public final class TorchWatch {
    private static final String TAG = "TorchWatch";

    private static CameraManager.TorchCallback sCallback;
    private static String sFlashId;
    private static int sRefs;

    private TorchWatch() {}

    public static synchronized void start(Context ctx) {
        final Context app = ctx.getApplicationContext();
        CameraManager cm = (CameraManager) app.getSystemService(Context.CAMERA_SERVICE);
        if (cm == null) return;
        if (sFlashId == null) sFlashId = flashCameraId(cm);

        if (sCallback == null) {
            sCallback = new CameraManager.TorchCallback() {
                @Override
                public void onTorchModeChanged(String cameraId, boolean enabled) {
                    if (sFlashId == null || sFlashId.equals(cameraId)) {
                        TorchState.write(app, enabled);
                    }
                }
                @Override
                public void onTorchModeUnavailable(String cameraId) {
                    // Another app opened the camera -> the torch is forced off
                    // and we can't drive it. Record off so a long-press doesn't
                    // try to "turn off" a torch that's already gone.
                    if (sFlashId == null || sFlashId.equals(cameraId)) {
                        TorchState.write(app, false);
                    }
                }
            };
        }
        if (sRefs++ == 0) {
            try {
                cm.registerTorchCallback(sCallback, new Handler(Looper.getMainLooper()));
            } catch (Throwable t) {
                Log.w(TAG, "registerTorchCallback failed", t);
                sRefs = 0;
            }
        }
    }

    public static synchronized void stop(Context ctx) {
        if (sCallback == null) return;
        if (--sRefs <= 0) {
            sRefs = 0;
            CameraManager cm = (CameraManager)
                    ctx.getApplicationContext().getSystemService(Context.CAMERA_SERVICE);
            if (cm != null) {
                try { cm.unregisterTorchCallback(sCallback); } catch (Throwable ignored) {}
            }
        }
    }

    /** First camera that reports a flash unit (matches TorchReceiver's choice). */
    public static String flashCameraId(CameraManager cm) {
        try {
            for (String id : cm.getCameraIdList()) {
                Boolean hasFlash = cm.getCameraCharacteristics(id)
                        .get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                if (Boolean.TRUE.equals(hasFlash)) return id;
            }
        } catch (CameraAccessException e) {
            Log.w(TAG, "getCameraIdList failed", e);
        }
        return null;
    }
}
