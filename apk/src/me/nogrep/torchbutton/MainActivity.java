package me.nogrep.torchbutton;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;

/**
 * Settings-style screen, built programmatically to avoid shipping XML
 * resources. One Switch — turns the long-press torch behaviour on or off.
 * The native daemon polls {@link EnableState#file(android.content.Context)}
 * and either runs its full state machine or operates as a transparent
 * passthrough.
 */
public class MainActivity extends Activity {
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

        final Switch toggle = new Switch(this);
        toggle.setText("Enable long-press torch");
        toggle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        toggle.setChecked(EnableState.read(this));
        toggle.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton v, boolean checked) {
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
    protected void onResume() {
        super.onResume();
        // If the user toggled via the QS tile while this Activity was paused,
        // re-sync the Switch.
        View root = findViewById(android.R.id.content);
        if (root instanceof ViewGroup) {
            syncSwitchFromState((ViewGroup) root);
        }
    }

    private void syncSwitchFromState(ViewGroup parent) {
        for (int i = 0; i < parent.getChildCount(); i++) {
            View child = parent.getChildAt(i);
            if (child instanceof Switch) {
                Switch sw = (Switch) child;
                boolean current = EnableState.read(this);
                if (sw.isChecked() != current) {
                    sw.setChecked(current);
                }
                return;
            } else if (child instanceof ViewGroup) {
                syncSwitchFromState((ViewGroup) child);
            }
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()
        );
    }
}
