// D-Bus helper for pomo: GNOME on Wayland only lets the shell move windows around.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';
import Clutter from 'gi://Clutter';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as MessageTray from 'resource:///org/gnome/shell/ui/messageTray.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const IFACE = `<node><interface name="org.gnome.Shell.Extensions.Pomo">
  <method name="Minimize"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="Activate"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="Notify"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="b" direction="in"/></method>
  <method name="SetProgress"><arg type="s" direction="in"/><arg type="d" direction="in"/><arg type="s" direction="in"/></method>
  <method name="HideProgress"><arg type="s" direction="in"/></method>
</interface></node>`;

// pomo puts a unique tag in its terminal title; find the window showing it
const BAR_WIDTH = 100;   // px
const STALE_SECONDS = 5; // hide the bar if pomo stops sending updates (e.g. it was killed)

const find = tag => global.get_window_actors().map(a => a.meta_window).find(w => w.get_title()?.includes(tag));

export default class PomoExtension extends Extension {
    enable() {
        this._dbus = Gio.DBusExportedObject.wrapJSObject(IFACE, this);
        this._dbus.export(Gio.DBus.session, '/org/gnome/Shell/Extensions/Pomo');
    }

    disable() {
        this._dbus.unexport();
        this._dbus = null;
        this.HideProgress();
    }

    // Shows (or updates) a progress bar in the top bar. `color` is a CSS color.
    SetProgress(tag, fraction, color) {
        if (!this._button) this._createButton();
        this._tag = tag;
        const filled = Math.round(BAR_WIDTH * Math.min(1, Math.max(0, fraction)));
        this._fill.set_width(filled);
        this._fill.set_style(`background-color: ${color}; border-radius: 4px;`);

        if (this._staleTimer) GLib.source_remove(this._staleTimer);
        this._staleTimer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, STALE_SECONDS, () => {
            this._staleTimer = null;
            this.HideProgress();
            return GLib.SOURCE_REMOVE;
        });
    }

    HideProgress(tag) {
        if (this._staleTimer) GLib.source_remove(this._staleTimer);
        this._staleTimer = null;
        this._button?.destroy();
        this._button = null;
    }

    _createButton() {
        this._button = new PanelMenu.Button(0.0, 'pomo', true);
        const track = new St.Widget({
            width: BAR_WIDTH, height: 8, y_align: Clutter.ActorAlign.CENTER,
            style: 'background-color: rgba(255,255,255,0.2); border-radius: 4px;',
        });
        this._fill = new St.Widget({width: 0, height: 8});
        track.add_child(this._fill);
        this._button.add_child(track);
        this._button.connect('button-press-event', () => {  // click to bring up pomo
            if (this._tag) this.Activate(this._tag);
            return Clutter.EVENT_STOP;
        });
        Main.panel.addToStatusArea(this.uuid, this._button);
    }

    Minimize(tag) {
        const w = find(tag);
        w?.minimize();
        return !!w;
    }

    Activate(tag) {
        const w = find(tag);
        if (w) Main.activateWindow(w);
        return !!w;
    }

    Notify(tag, body, urgent) {
        const source = MessageTray.getSystemSource();
        const n = new MessageTray.Notification({
            source, title: 'Pomodoro', body,
            urgency: urgent ? MessageTray.Urgency.CRITICAL : MessageTray.Urgency.NORMAL,
        });
        n.connect('activated', () => this.Activate(tag));
        source.addNotification(n);
    }
}
