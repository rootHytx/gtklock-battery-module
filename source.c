// gtklock-battery-module
// Copyright (c) 2026 Zayed Alabbad
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shows battery state as an icon + percentage chip on the gtklock lockscreen.
//
// Mirrors the lifecycle of the official gtklock modules (userinfo/powerbar):
//   on_activation     -> remember our module id, install CSS
//   on_focus_change   -> build the widget and attach it to the window
//   on_window_destroy -> tear down (and cancel the refresh timer)
//   on_idle_hide/show -> toggle the revealer with the rest of the UI

#include <stdlib.h>
#include <sys/types.h>
#include "gtklock-module.h"

#define MODULE_DATA(x) (x->module_data[self_id])
#define BATTERY(x) ((struct battery *)MODULE_DATA(x))

struct battery {
	GtkWidget *revealer;
	GtkWidget *box;
	GtkWidget *icon;
	GtkWidget *label;
	guint timer;
};

// gtklock checks these against its own version (v4.0.x here).
const gchar module_name[] = "battery";
const guint module_major_version = 4;
const guint module_minor_version = 0;

static int self_id;

// Configurable via the [battery] group in the gtklock config, e.g.:
//   [battery]
//   battery-path=/sys/class/power_supply/BAT0
//   battery-refresh=30
//   battery-low-percent=20
static gchar *battery_path = "/sys/class/power_supply/BAT0";
static gint battery_refresh = 30;
static gint battery_low_percent = 20;

GOptionEntry module_entries[] = {
	{ "battery-path", 0, 0, G_OPTION_ARG_STRING, &battery_path, NULL, NULL },
	{ "battery-refresh", 0, 0, G_OPTION_ARG_INT, &battery_refresh, NULL, NULL },
	{ "battery-low-percent", 0, 0, G_OPTION_ARG_INT, &battery_low_percent, NULL, NULL },
	{ NULL },
};

// Read a single sysfs attribute (e.g. "capacity", "status"), newline-trimmed.
// Returns a newly-allocated string, or NULL on failure. Caller frees.
static gchar *read_attr(const char *attr) {
	gchar *path = g_strdup_printf("%s/%s", battery_path, attr);
	gchar *contents = NULL;
	if(g_file_get_contents(path, &contents, NULL, NULL))
		g_strchomp(contents);
	g_free(path);
	return contents;
}

// Timer callback: refresh icon, label, and state classes.
// Stops itself if the window's module data is gone.
static gboolean update_battery(gpointer user_data) {
	struct Window *ctx = user_data;
	if(MODULE_DATA(ctx) == NULL) return G_SOURCE_REMOVE;

	gchar *cap_str = read_attr("capacity");
	gchar *status = read_attr("status");

	// No battery (desktop PC, or it was removed): hide the chip entirely.
	// Keep the timer running so it reappears if a battery shows up.
	if(cap_str == NULL) {
		gtk_widget_hide(BATTERY(ctx)->revealer);
		g_free(status);
		return G_SOURCE_CONTINUE;
	}
	gtk_widget_show(BATTERY(ctx)->revealer);

	gboolean charging = g_strcmp0(status, "Charging") == 0;
	gboolean full = g_strcmp0(status, "Full") == 0;

	// Adwaita ships battery-level-{0..100 in 10s}[-charging]-symbolic
	// plus battery-level-100-charged-symbolic.
	gint cap = CLAMP(atoi(cap_str), 0, 100);
	gint level = MIN(((cap + 5) / 10) * 10, 100);
	const char *icon_name;
	gchar icon_buf[64];
	if(full || (charging && cap == 100)) {
		icon_name = "battery-level-100-charged-symbolic";
	} else {
		g_snprintf(icon_buf, sizeof(icon_buf),
			"battery-level-%d%s-symbolic", level, charging ? "-charging" : "");
		icon_name = icon_buf;
	}
	gchar *text = g_strdup_printf("%d%%", cap);

	gtk_image_set_from_icon_name(GTK_IMAGE(BATTERY(ctx)->icon), icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_label_set_text(GTK_LABEL(BATTERY(ctx)->label), text);

	// State classes drive the chip color: green while on AC, red when low.
	GtkStyleContext *style = gtk_widget_get_style_context(BATTERY(ctx)->box);
	if(charging || full) gtk_style_context_add_class(style, "battery-charging");
	else gtk_style_context_remove_class(style, "battery-charging");
	if(!charging && !full && cap <= battery_low_percent)
		gtk_style_context_add_class(style, "battery-low");
	else gtk_style_context_remove_class(style, "battery-low");

	g_free(text);
	g_free(cap_str);
	g_free(status);
	return G_SOURCE_CONTINUE;
}

static void setup_battery(struct Window *ctx) {
	// Rebuild from scratch if we were already attached to this window.
	if(MODULE_DATA(ctx) != NULL) {
		if(BATTERY(ctx)->timer != 0) g_source_remove(BATTERY(ctx)->timer);
		gtk_widget_destroy(BATTERY(ctx)->revealer);
		g_free(MODULE_DATA(ctx));
		MODULE_DATA(ctx) = NULL;
	}
	MODULE_DATA(ctx) = g_malloc0(sizeof(struct battery));

	// Top-center overlay so it sits above the clock without disturbing layout.
	BATTERY(ctx)->revealer = gtk_revealer_new();
	g_object_set(BATTERY(ctx)->revealer, "margin", 20, NULL);
	gtk_widget_set_halign(BATTERY(ctx)->revealer, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(BATTERY(ctx)->revealer, GTK_ALIGN_START);
	gtk_widget_set_name(BATTERY(ctx)->revealer, "battery-revealer");
	gtk_revealer_set_reveal_child(GTK_REVEALER(BATTERY(ctx)->revealer), TRUE);
	gtk_revealer_set_transition_type(GTK_REVEALER(BATTERY(ctx)->revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
	gtk_overlay_add_overlay(GTK_OVERLAY(ctx->overlay), BATTERY(ctx)->revealer);

	// The pill chip: icon + percentage in a horizontal box.
	BATTERY(ctx)->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_name(BATTERY(ctx)->box, "battery-box");
	gtk_container_add(GTK_CONTAINER(BATTERY(ctx)->revealer), BATTERY(ctx)->box);

	BATTERY(ctx)->icon = gtk_image_new();
	gtk_image_set_pixel_size(GTK_IMAGE(BATTERY(ctx)->icon), 22);
	gtk_widget_set_name(BATTERY(ctx)->icon, "battery-icon");
	gtk_container_add(GTK_CONTAINER(BATTERY(ctx)->box), BATTERY(ctx)->icon);

	BATTERY(ctx)->label = gtk_label_new(NULL);
	gtk_widget_set_name(BATTERY(ctx)->label, "battery-label");
	gtk_container_add(GTK_CONTAINER(BATTERY(ctx)->box), BATTERY(ctx)->label);

	// Show children first; update_battery() then owns the revealer's
	// visibility (it hides the whole chip when no battery is present).
	gtk_widget_show_all(BATTERY(ctx)->revealer);
	update_battery(ctx);
	BATTERY(ctx)->timer =
		g_timeout_add_seconds(battery_refresh > 0 ? battery_refresh : 30, update_battery, ctx);
}

void on_activation(struct GtkLock *gtklock, int id) {
	self_id = id;

	GtkCssProvider *provider = gtk_css_provider_new();
	GError *err = NULL;
	const char css[] =
		"#battery-box {"
		"  background-color: rgba(255, 255, 255, 0.08);"
		"  border-radius: 999px;"
		"  padding: 6px 14px;"
		"}"
		"#battery-box #battery-label {"
		"  font-size: 13pt;"
		"}"
		"#battery-box.battery-low,"
		"#battery-box.battery-low #battery-label,"
		"#battery-box.battery-low #battery-icon {"
		"  color: #f38ba8;"
		"}"
		"#battery-box.battery-charging,"
		"#battery-box.battery-charging #battery-label,"
		"#battery-box.battery-charging #battery-icon {"
		"  color: #a6e3a1;"
		"}";
	gtk_css_provider_load_from_data(provider, css, -1, &err);
	if(err != NULL) {
		g_warning("battery-module: style loading failed: %s", err->message);
		g_error_free(err);
	} else {
		// gtklock loads the user's style= CSS at APPLICATION+1; go one above it
		// so state colors aren't clobbered by global rules like `* { color: ... }`.
		gtk_style_context_add_provider_for_screen(
			gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2
		);
	}
	g_object_unref(provider);
}

void on_focus_change(struct GtkLock *gtklock, struct Window *win, struct Window *old) {
	setup_battery(win);
	if(gtklock->hidden)
		gtk_revealer_set_reveal_child(GTK_REVEALER(BATTERY(win)->revealer), FALSE);
	if(old != NULL && win != old)
		gtk_revealer_set_reveal_child(GTK_REVEALER(BATTERY(old)->revealer), FALSE);
}

void on_window_destroy(struct GtkLock *gtklock, struct Window *ctx) {
	if(MODULE_DATA(ctx) != NULL) {
		if(BATTERY(ctx)->timer != 0) g_source_remove(BATTERY(ctx)->timer);
		gtk_widget_destroy(BATTERY(ctx)->revealer);
		g_free(MODULE_DATA(ctx));
		MODULE_DATA(ctx) = NULL;
	}
}

void on_idle_hide(struct GtkLock *gtklock) {
	if(gtklock->focused_window)
		gtk_revealer_set_reveal_child(GTK_REVEALER(BATTERY(gtklock->focused_window)->revealer), FALSE);
}

void on_idle_show(struct GtkLock *gtklock) {
	if(gtklock->focused_window)
		gtk_revealer_set_reveal_child(GTK_REVEALER(BATTERY(gtklock->focused_window)->revealer), TRUE);
}
