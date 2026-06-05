package me.nogrep.torchbutton;

import android.content.ComponentName;
import android.content.Context;
import android.graphics.drawable.Icon;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/**
 * Quick Settings tile — tap to toggle the long-press-torch behaviour.
 * Listens to the same shared {@link EnableState} as MainActivity, so the
 * two stay in sync.
 */
public class TorchToggleTileService extends TileService {

    @Override
    public void onStartListening() {
        super.onStartListening();
        refreshTile();
    }

    @Override
    public void onClick() {
        super.onClick();
        boolean nowEnabled = !EnableState.read(this);
        EnableState.write(this, nowEnabled);
        refreshTile();
    }

    private void refreshTile() {
        Tile tile = getQsTile();
        if (tile == null) return;
        boolean enabled = EnableState.read(this);
        tile.setState(enabled ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setLabel("Torch Button");
        tile.setIcon(Icon.createWithResource(this, R.drawable.ic_torch));
        tile.updateTile();
    }

    /** Request SystemUI to ask us for fresh tile state. Safe to call from
     *  any context (e.g. MainActivity after the user flips the switch). */
    public static void requestUpdate(Context ctx) {
        try {
            TileService.requestListeningState(
                ctx,
                new ComponentName(ctx, TorchToggleTileService.class)
            );
        } catch (Throwable t) {
            // SystemUI may refuse if the tile isn't added — that's fine.
        }
    }
}
