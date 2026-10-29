package dev.halfcraft.client;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntFunction;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.toasts.Toast;
import net.minecraft.client.gui.components.toasts.ToastManager;
import net.minecraft.client.renderer.RenderPipelines;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.world.item.ItemStack;

/**
 * Half-Life's pickup history as one of Minecraft's toasts: a row per thing picked up, the way
 * Half-Life stacks its icons down the side of the screen. A pickup of the same thing again counts
 * up on its row; the toast goes once nothing has come for a while, like Minecraft's recipe toast.
 */
public final class PickupToast implements Toast {
	public static final int MAX_ROWS = 5;
	private static final Identifier BACKGROUND = Identifier.withDefaultNamespace("toast/tutorial");
	private static final long DISPLAY_MS = 5000;
	private static final int ROW_HEIGHT = 20;
	private static final int TEXT_COLOR = 0xFF000000;

	/**
	 * @param key    what makes two pickups the same thing
	 * @param text   the row's text for how many there were
	 * @param counts whether a second pickup adds up (ammo, items) or is the same one again (a weapon)
	 */
	public record Row(String key, ItemStack icon, IntFunction<Component> text, boolean counts) {
	}

	private final List<Row> rows = new ArrayList<>();
	private final List<Integer> counts = new ArrayList<>();
	private boolean changed = true;
	private long lastChanged;
	private Visibility wantedVisibility = Visibility.SHOW;

	/** Adds a pickup to its row, or a new row. @return false when the toast is full */
	public boolean add(Row row, int count) {
		for (int i = 0; i < this.rows.size(); i++) {
			if (this.rows.get(i).key().equals(row.key())) {
				if (row.counts()) {
					this.counts.set(i, this.counts.get(i) + count);
				}
				this.changed = true;
				return true;
			}
		}
		if (this.rows.size() >= MAX_ROWS) {
			return false;
		}
		this.rows.add(row);
		this.counts.add(count);
		this.changed = true;
		return true;
	}

	@Override
	public Visibility getWantedVisibility() {
		return this.wantedVisibility;
	}

	@Override
	public void update(ToastManager manager, long now) {
		if (this.changed) {
			this.lastChanged = now;
			this.changed = false;
		}
		this.wantedVisibility = now - this.lastChanged >= DISPLAY_MS * manager.getNotificationDisplayTimeMultiplier() ? Visibility.HIDE : Visibility.SHOW;
	}

	@Override
	public int height() {
		return Math.max(SLOT_HEIGHT, 12 + this.rows.size() * ROW_HEIGHT);
	}

	@Override
	public void extractRenderState(GuiGraphicsExtractor graphics, Font font, long now) {
		graphics.blitSprite(RenderPipelines.GUI_TEXTURED, BACKGROUND, 0, 0, this.width(), this.height());
		for (int i = 0; i < this.rows.size(); i++) {
			int y = 8 + i * ROW_HEIGHT;
			graphics.fakeItem(this.rows.get(i).icon(), 8, y);
			graphics.text(font, this.rows.get(i).text().apply(this.counts.get(i)), 30, y + 4, TEXT_COLOR, false);
		}
	}
}
