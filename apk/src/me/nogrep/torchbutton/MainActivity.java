package me.nogrep.torchbutton;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.FileObserver;
import android.util.TypedValue;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;

import java.io.File;

/**
 * Settings-style screen, built programmatically to avoid shipping XML
 * resources. One Switch — turns the long-press torch behaviour on or off.
 * The native daemon polls {@link EnableState#file(android.content.Context)}
 * and either runs its full state machine or operates as a transparent
 * passthrough.
 *
 * A FileObserver keeps the Switch live: if the enable flag is flipped from the
 * Quick Settings tile while this screen is showing (the QS shade sits over the
 * Activity without pausing it), the Switch updates immediately.
 */
public class MainActivity extends Activity {
    private Switch toggle;
    private FileObserver enableObserver;
    private boolean suppressListener;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        int pad = dp(24);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad * 2, pad, pad);

        TextView title = new TextView(this);
        title.setText("Torch Button");
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        title.setPadding(0, 0, 0, dp(8));
        root.addView(title);

        TextView desc = new TextView(this);
        desc.setText(
            "Long-press the Power button to toggle the flashlight " +
            "when the screen is off or locked. Long-press again to " +
            "turn the flashlight off, no matter the screen state. " +
            "When the screen is on and unlocked, long-press behaves " +
            "normally (system power dialog)."
        );
        desc.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        desc.setPadding(0, 0, 0, dp(24));
        root.addView(desc);

        toggle = new Switch(this);
        toggle.setText("Enable long-press torch");
        toggle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        toggle.setChecked(EnableState.read(this));
        toggle.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton v, boolean checked) {
                if (suppressListener) return;   // programmatic sync, not a user tap
                EnableState.write(MainActivity.this, checked);
                // Ask SystemUI to refresh the QS tile state.
                TorchToggleTileService.requestUpdate(MainActivity.this);
            }
        });
        toggle.setPadding(0, dp(12), 0, dp(12));
        root.addView(toggle);

        TextView hint = new TextView(this);
        hint.setText(
            "Tip: this can also be toggled from a Quick Settings tile " +
            "(\"Torch Button\")."
        );
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        hint.setPadding(0, dp(24), 0, 0);
        hint.setTextColor(Color.GRAY);
        root.addView(hint);

        setContentView(root);
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Watch the enable flag so a QS-tile toggle reflects here live, even
        // though the shade overlay doesn't trigger onResume.
        final File dir = getFilesDir();
        enableObserver = new FileObserver(dir.getPath(), FileObserver.CLOSE_WRITE) {
            @Override
            public void onEvent(int event, String path) {
                if ("enabled".equals(path)) {
                    runOnUiThread(new Runnable() {
                        @Override public void run() { syncSwitch(); }
                    });
                }
            }
        };
        enableObserver.startWatching();
        syncSwitch();
    }

    @Override
    protected void onStop() {
        if (enableObserver != null) {
            enableObserver.stopWatching();
            enableObserver = null;
        }
        super.onStop();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Re-sync the real torch state into the file while we're foreground.
        TorchWatch.start(this);
        syncSwitch();
    }

    @Override
    protected void onPause() {
        TorchWatch.stop(this);
        super.onPause();
    }

    /** Set the Switch to match the persisted enable flag, without re-firing
     *  the listener (which would write the value straight back). */
    private void syncSwitch() {
        if (toggle == null) return;
        boolean current = EnableState.read(this);
        if (toggle.isChecked() != current) {
            suppressListener = true;
            toggle.setChecked(current);
            suppressListener = false;
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()
        );
    }
}
