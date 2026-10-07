// D-Bus helper for pomo: GNOME on Wayland only lets the shell move windows around.
import Gio from 'gi://Gio';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as MessageTray from 'resource:///org/gnome/shell/ui/messageTray.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const IFACE = `<node><interface name="org.gnome.Shell.Extensions.Pomo">
  <method name="Minimize"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="Activate"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="Notify"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="b" direction="in"/></method>
</interface></node>`;

// pomo puts a unique tag in its terminal title; find the window showing it
const find = tag => global.get_window_actors().map(a => a.meta_window).find(w => w.get_title()?.includes(tag));

export default class PomoExtension extends Extension {
    enable() {
        this._dbus = Gio.DBusExportedObject.wrapJSObject(IFACE, this);
        this._dbus.export(Gio.DBus.session, '/org/gnome/Shell/Extensions/Pomo');
    }

    disable() {
        this._dbus.unexport();
        this._dbus = null;
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
