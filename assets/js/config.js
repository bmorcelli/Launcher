"use strict";
// ---------------------------------------------------------------------------
// Serial line transport
//
// Talks to the Launcher serial console (see src/serial_console.cpp upstream):
// one command per line in, and free-form lines back out. There is no
// end-of-response marker, so multi-line replies (help, wifi scan, ...) are
// collected until the line stream goes quiet for a short idle window.
// ---------------------------------------------------------------------------
class SerialSession {
    constructor(port) {
        this.reader = null;
        this.writer = null;
        this.listeners = new Set();
        this.closeListeners = new Set();
        this.closed = false;
        this.port = port;
    }
    async open(baudRate = 115200) {
        await this.port.open({ baudRate });
        // Do not touch DTR/RTS here. Changing those lines on open can pulse EN on
        // ESP32 auto-reset circuits, so reset is reserved for explicit hardReset().
        this.writer = this.port.writable.getWriter();
        void this.readLoop();
    }
    async readLoop() {
        if (!this.port.readable)
            return;
        const reader = this.port.readable.getReader();
        this.reader = reader;
        const decoder = new TextDecoder();
        let buffer = "";
        try {
            while (!this.closed) {
                const { value, done } = await reader.read();
                if (done)
                    break;
                if (!value)
                    continue;
                buffer += decoder.decode(value, { stream: true });
                let idx;
                while ((idx = buffer.indexOf("\n")) >= 0) {
                    let line = buffer.slice(0, idx);
                    buffer = buffer.slice(idx + 1);
                    if (line.endsWith("\r"))
                        line = line.slice(0, -1);
                    this.emit(line);
                }
            }
        }
        catch {
            // Port likely unplugged; onLine subscribers will simply stop hearing from us.
        }
        finally {
            const wasIntentional = this.closed;
            try {
                reader.releaseLock();
            }
            catch {
                /* ignore */
            }
            // Only signal an unexpected disconnect: an intentional close() already
            // set `closed` before tearing anything down, so callers driving that
            // close don't need to be told about it again.
            if (!wasIntentional) {
                this.closed = true;
                this.closeListeners.forEach((listener) => listener());
            }
        }
    }
    emit(line) {
        this.listeners.forEach((listener) => listener(line));
    }
    onLine(listener) {
        this.listeners.add(listener);
        return () => this.listeners.delete(listener);
    }
    // Fires when the read loop ends without close() having been called first —
    // i.e. the device was unplugged or the OS dropped the port.
    onClose(listener) {
        this.closeListeners.add(listener);
        return () => this.closeListeners.delete(listener);
    }
    async writeLine(command) {
        if (!this.writer)
            throw new Error("Serial port is not open");
        await this.writer.write(new TextEncoder().encode(`${command}\n`));
    }
    // A hardware reset via the RTS line (wired to EN on ESP32 boards), not the
    // Launcher's own "reboot" console command: that command only exists inside
    // the Launcher app itself, so it's a no-op (or worse, undefined behavior)
    // if some other queued firmware has already taken over the serial console.
    // Toggling RTS resets the chip regardless of what's currently running,
    // which is what lets us reliably catch the boot banner afterwards.
    async hardReset(pulseMs = 150) {
        try {
            await this.port.setSignals({ dataTerminalReady: false, requestToSend: true });
            await sleep(pulseMs);
            await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
        }
        catch {
            // Some OS/driver combinations don't expose RTS control; nothing more we can do.
        }
    }
    async close() {
        this.closed = true;
        try {
            await this.reader?.cancel();
        }
        catch {
            /* ignore */
        }
        try {
            this.writer?.releaseLock();
        }
        catch {
            /* ignore */
        }
        try {
            await this.port.close();
        }
        catch {
            /* ignore */
        }
    }
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const waitForLine = (session, predicate, timeoutMs) => new Promise((resolve, reject) => {
    const timer = window.setTimeout(() => {
        unsubscribe();
        reject(new Error("Timed out waiting for a response from the device."));
    }, timeoutMs);
    const unsubscribe = session.onLine((line) => {
        if (predicate(line)) {
            window.clearTimeout(timer);
            unsubscribe();
            resolve(line);
        }
    });
});
// Sends a command, then collects every line the device prints until the
// stream is quiet for `idleMs` or `maxMs` total has elapsed, whichever
// comes first. Works for one-line replies and multi-line dumps alike.
const sendAndCollect = (session, command, { idleMs = 350, maxMs = 8000 } = {}) => new Promise((resolve) => {
    const lines = [];
    let idleTimer;
    const finish = () => {
        unsubscribe();
        window.clearTimeout(idleTimer);
        window.clearTimeout(maxTimer);
        resolve(lines);
    };
    const unsubscribe = session.onLine((line) => {
        lines.push(line);
        window.clearTimeout(idleTimer);
        idleTimer = window.setTimeout(finish, idleMs);
    });
    const maxTimer = window.setTimeout(finish, maxMs);
    void session.writeLine(command);
});
// Printed with "%-32s rssi=%-4d auth=%-9s%s\n" — parse by fixed column so
// SSIDs containing spaces don't confuse a whitespace-based split.
const parseWifiScan = (lines) => {
    const networks = [];
    for (const raw of lines) {
        if (raw.length <= 32)
            continue;
        const ssid = raw.slice(0, 32).trimEnd();
        const rest = raw.slice(32).trim();
        const match = rest.match(/^rssi=(-?\d+)\s+auth=(\S+)\s*(\(Saved\))?$/);
        if (!ssid || !match)
            continue;
        networks.push({
            ssid,
            rssi: Number(match[1]),
            auth: match[2],
            saved: Boolean(match[3])
        });
    }
    return networks;
};
const CALIBRATION_RE = /x0:(\d+)\s+x1:(\d+)\s+y0:(\d+)\s+y1:(\d+)\s+rot:0x([0-9A-Fa-f]+)/;
const parseCalibration = (lines) => {
    for (const line of lines) {
        const match = line.match(CALIBRATION_RE);
        if (match) {
            return {
                x0: Number(match[1]),
                x1: Number(match[2]),
                y0: Number(match[3]),
                y1: Number(match[4]),
                rot: `0x${match[5].toUpperCase()}`
            };
        }
    }
    return null;
};
const firstNonEmptyLine = (lines) => lines.map((l) => l.trim()).find((l) => l.length > 0) ?? "";
const findLauncherVersionLine = (lines) => lines.map((l) => l.trim()).find((l) => l.startsWith("Launcher")) ?? "";
const SETTINGS_NON_EDITABLE_KEYS = new Set(["wifi", "favorite"]);
// Matches "aa:bb:cc:dd:ee:ff" or "aa:bb:cc:dd:ee:ff-Device Name".
const ROTATION_KEY_RE = /^([0-9a-fA-F]{1,2}:){5}[0-9a-fA-F]{1,2}(-.*)?$/;
const isRotationKey = (key) => ROTATION_KEY_RE.test(key);
const SETTINGS_BOOL_FIELDS = [
    { key: "onlyBins", label: "Only show .bin files", help: "Filter the file browser to firmware binaries only." },
    {
        key: "bootToApp",
        label: "Boot straight into queued app",
        help: "Skip the Launcher menu and auto-boot the last installed app."
    },
    { key: "noDotFiles", label: "Hide dotfiles", help: "Hide files/folders starting with a dot in the file browser." },
    { key: "autoBackup", label: "Auto backup" },
    { key: "askSpiffs", label: "Ask before using SPIFFS", help: "Prompt before formatting/using the SPIFFS partition." },
    { key: "dev", label: "Developer mode" },
    { key: "autoConnect", label: "Auto-connect WiFi", help: "Automatically connect to a known network on boot." }
];
const SETTINGS_NUMBER_FIELDS = [
    { key: "bright", label: "Brightness (%)", min: 0, max: 100 },
    { key: "dimmerSet", label: "Dimmer timeout (seconds)", min: 0 }
];
const SETTINGS_STRING_FIELDS = [
    { key: "wui_usr", label: "Web UI username" },
    { key: "wui_pwd", label: "Web UI password", password: true },
    { key: "dwn_path", label: "Download path" }
];
const SETTINGS_COLOR_FIELDS = [
    { key: "FGCOLOR", label: "Foreground color" },
    { key: "BGCOLOR", label: "Background color" },
    { key: "ALCOLOR", label: "Accent/alert color" },
    { key: "odd", label: "List row color (odd)" },
    { key: "even", label: "List row color (even)" }
];
// RGB565 <-> #rrggbb. round(n * 255 / max) is bit-exact reversible for both
// the 5-bit and 6-bit channels, so a color nobody touches survives unchanged.
const rgb565ToHex = (value) => {
    const r5 = (value >> 11) & 0x1f;
    const g6 = (value >> 5) & 0x3f;
    const b5 = value & 0x1f;
    const toHex = (n) => n.toString(16).padStart(2, "0");
    return `#${toHex(Math.round((r5 * 255) / 31))}${toHex(Math.round((g6 * 255) / 63))}${toHex(Math.round((b5 * 255) / 31))}`;
};
const hexToRgb565 = (hex) => {
    const clean = hex.replace("#", "");
    const r8 = parseInt(clean.slice(0, 2), 16) || 0;
    const g8 = parseInt(clean.slice(2, 4), 16) || 0;
    const b8 = parseInt(clean.slice(4, 6), 16) || 0;
    const r5 = Math.round((r8 * 31) / 255);
    const g6 = Math.round((g8 * 63) / 255);
    const b5 = Math.round((b8 * 31) / 255);
    return ((r5 & 0x1f) << 11) | ((g6 & 0x3f) << 5) | (b5 & 0x1f);
};
// "settings get" can come back truncated on a busy link; only trust a
// response that actually parses as a JSON object.
const parseSettingsJson = (lines) => {
    const text = lines.join("").trim();
    if (!text)
        return null;
    try {
        const parsed = JSON.parse(text);
        if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
            return parsed;
        }
        return null;
    }
    catch {
        return null;
    }
};
const settingsWifiSsids = (settings) => {
    const wifi = settings.wifi;
    if (!Array.isArray(wifi))
        return [];
    return wifi
        .map((entry) => (entry && typeof entry === "object" ? entry.ssid : null))
        .filter((ssid) => typeof ssid === "string")
        .sort();
};
const sameSsidSet = (a, b) => a.length === b.length && a.every((ssid, i) => ssid === b[i]);
// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------
const ensureConfigStyles = () => {
    if (document.getElementById("config-style"))
        return;
    const style = document.createElement("style");
    style.id = "config-style";
    style.textContent = `
    .config-log[hidden] {
      display: none;
    }
    .config-log {
      margin-top: 12px;
      display: flex;
      flex-direction: column;
      background: rgba(6, 10, 12, 0.6);
      border: 1px solid rgba(0, 221, 0, 0.18);
      border-radius: var(--radius-sm);
      font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
      font-size: 0.82rem;
      color: rgba(245, 248, 242, 0.85);
      overflow: hidden;
    }
    .config-log__lines {
      max-height: 220px;
      overflow-y: auto;
      padding: 12px 14px;
      line-height: 1.5;
      white-space: pre-wrap;
      word-break: break-word;
    }
    .config-log__line--tx { color: var(--accent); }
    .config-log__line--rx { color: var(--text-subtle); }
    .config-log__line--err { color: #ff6f6f; }
    .config-log__input-row {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 8px 10px;
      border-top: 1px solid rgba(0, 221, 0, 0.18);
      background: rgba(0, 221, 0, 0.04);
    }
    .config-log__prompt {
      color: var(--primary);
      font-weight: 700;
      flex: 0 0 auto;
    }
    .config-log__input {
      flex: 1 1 auto;
      min-width: 0;
      background: transparent;
      border: none;
      outline: none;
      color: var(--text);
      font-family: inherit;
      font-size: 0.85rem;
      padding: 6px 4px;
    }
    .config-log__input::placeholder {
      color: rgba(245, 248, 242, 0.4);
    }
    .config-log__input:disabled,
    .config-log__send:disabled {
      opacity: 0.4;
      cursor: not-allowed;
    }
    .config-log__send {
      flex: 0 0 auto;
      background: transparent;
      border: 1px solid rgba(0, 221, 0, 0.35);
      color: var(--primary);
      border-radius: var(--radius-sm);
      padding: 6px 16px;
      font-size: 0.8rem;
      font-weight: 600;
      letter-spacing: 0.04em;
      cursor: pointer;
      transition: background 0.15s ease, border-color 0.15s ease;
    }
    .config-log__send:not(:disabled):hover {
      background: rgba(0, 221, 0, 0.12);
      border-color: rgba(0, 221, 0, 0.6);
    }
    .config-grid {
      display: grid;
      gap: 16px;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    }
    .config-inline-form {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      align-items: center;
      margin-top: 16px;
    }
    .config-inline-form .catalog__search {
      flex: 1 1 200px;
      min-width: 160px;
    }
    .config-wifi-list {
      list-style: none;
      margin: 20px 0 0;
      padding: 0;
      display: grid;
      gap: 10px;
    }
    .config-wifi-row {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 10px 14px;
      padding: 12px 16px;
      border-radius: var(--radius-sm);
      background: rgba(0, 221, 0, 0.06);
      border: 1px solid rgba(0, 221, 0, 0.16);
    }
    .config-wifi-row__ssid {
      font-weight: 600;
      margin-right: auto;
      overflow-wrap: anywhere;
    }
    .config-wifi-row__meta {
      font-size: 0.82rem;
      color: var(--text-subtle);
      white-space: nowrap;
    }
    .config-badge {
      display: inline-flex;
      align-items: center;
      padding: 2px 10px;
      border-radius: 999px;
      font-size: 0.75rem;
      font-weight: 600;
      letter-spacing: 0.04em;
      text-transform: uppercase;
      background: rgba(0, 221, 0, 0.14);
      border: 1px solid rgba(0, 221, 0, 0.35);
      color: var(--primary);
    }
    .config-badge--saved {
      background: rgba(224, 210, 4, 0.14);
      border-color: rgba(224, 210, 4, 0.4);
      color: var(--accent);
    }
    .config-password-field {
      position: relative;
      display: inline-flex;
      align-items: center;
    }
    .config-password-field input {
      padding-right: 40px;
    }
    .config-password-field__toggle {
      position: absolute;
      right: 4px;
      top: 50%;
      transform: translateY(-50%);
      display: flex;
      align-items: center;
      justify-content: center;
      width: 30px;
      height: 30px;
      border: none;
      border-radius: 50%;
      background: transparent;
      color: var(--text-subtle);
      cursor: pointer;
      transition: color 0.15s ease, background 0.15s ease;
    }
    .config-password-field__toggle:hover,
    .config-password-field__toggle:focus-visible {
      color: var(--primary);
      background: rgba(0, 221, 0, 0.12);
      outline: none;
    }
    .config-wifi-row__pwd input {
      width: 160px;
      padding-block: 8px;
      font-size: 0.9rem;
    }
    .config-toast-container {
      position: fixed;
      top: calc(var(--header-height) + 16px);
      right: 16px;
      z-index: 9500;
      display: flex;
      flex-direction: column;
      gap: 10px;
      max-width: min(360px, 92vw);
    }
    .config-toast {
      position: relative;
      display: flex;
      align-items: flex-start;
      gap: 10px;
      padding: 14px 36px 14px 16px;
      border-radius: var(--radius-sm);
      background: rgba(12, 18, 21, 0.97);
      border: 1px solid rgba(0, 221, 0, 0.3);
      border-left: 4px solid var(--primary);
      box-shadow: 0 18px 40px rgba(0, 0, 0, 0.35);
      color: var(--text);
      font-size: 0.9rem;
      line-height: 1.4;
      animation: config-toast-in 0.2s ease;
    }
    .config-toast--error {
      border-color: rgba(255, 111, 111, 0.4);
      border-left-color: #ff6f6f;
    }
    .config-toast__close {
      position: absolute;
      top: 8px;
      right: 8px;
      width: 22px;
      height: 22px;
      display: flex;
      align-items: center;
      justify-content: center;
      border: none;
      border-radius: 50%;
      background: transparent;
      color: var(--text-subtle);
      cursor: pointer;
      font-size: 0.95rem;
      line-height: 1;
    }
    .config-toast__close:hover { color: var(--text); background: rgba(255,255,255,0.08); }
    @keyframes config-toast-in {
      from { opacity: 0; transform: translateY(-6px); }
      to { opacity: 1; transform: translateY(0); }
    }
    .config-calibration-grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
      margin-bottom: 16px;
    }
    .config-field {
      display: flex;
      flex-direction: column;
      gap: 6px;
      font-size: 0.8rem;
      color: var(--text-subtle);
      text-transform: uppercase;
      letter-spacing: 0.06em;
    }
    .config-field input {
      text-transform: none;
      letter-spacing: normal;
      padding: 10px 12px;
    }
    .config-settings-meta {
      color: var(--text-subtle);
      font-size: 0.85rem;
      margin: 4px 0 0;
    }
    .config-settings-group {
      margin-top: 20px;
    }
    .config-settings-group__title {
      margin: 0 0 12px;
      font-size: 0.95rem;
      color: var(--accent);
    }
    .config-settings-toggles {
      display: grid;
      gap: 10px;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    }
    .config-settings-toggle {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 10px 12px;
      border-radius: var(--radius-sm);
      background: rgba(0, 221, 0, 0.05);
      border: 1px solid rgba(0, 221, 0, 0.14);
      cursor: pointer;
    }
    .config-settings-toggle input[type="checkbox"] {
      width: 16px;
      height: 16px;
      accent-color: var(--primary);
      cursor: pointer;
      flex: 0 0 auto;
    }
    .config-settings-toggle__text {
      display: flex;
      flex-direction: column;
      gap: 2px;
    }
    .config-settings-toggle__help {
      font-size: 0.78rem;
      color: var(--text-subtle);
    }
    .config-settings-fields {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    }
    .config-settings-field {
      display: flex;
      flex-direction: column;
      gap: 6px;
      font-size: 0.8rem;
      color: var(--text-subtle);
    }
    .config-settings-field input[type="color"] {
      appearance: none;
      -webkit-appearance: none;
      width: 100%;
      height: 40px;
      padding: 3px;
      border: 1px solid rgba(0, 221, 0, 0.35);
      border-radius: var(--radius-sm);
      background: rgba(0, 221, 0, 0.08);
      cursor: pointer;
    }
    .config-settings-field input[type="color"]::-webkit-color-swatch {
      border: none;
      border-radius: 6px;
    }
    .config-settings-field input[type="color"]::-webkit-color-swatch-wrapper {
      padding: 0;
    }
    [data-config-connect]:disabled,
    [data-config-disconnect]:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .config-reboot-toggle {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      color: var(--text-subtle);
      font-size: 0.9rem;
      cursor: pointer;
    }
    /* .button and .config-reboot-toggle set "display" themselves, which
       otherwise beats the UA stylesheet's [hidden] { display: none }
       (author rules always outrank user-agent rules regardless of
       specificity), so the hidden attribute alone wouldn't hide them. */
    .button[hidden],
    .config-reboot-toggle[hidden] {
      display: none;
    }
    .config-reboot-toggle input {
      accent-color: var(--primary);
      cursor: pointer;
    }
    .config-section-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-bottom: 24px;
    }
    .config-section-header .section__heading {
      margin: 0;
    }
    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .config-password-field--error input {
      border-color: #ff6f6f;
      box-shadow: 0 0 0 2px rgba(255, 111, 111, 0.25);
    }
    .config-modal-backdrop {
      position: fixed;
      inset: 0;
      background: rgba(6, 10, 12, 0.75);
      backdrop-filter: blur(6px);
      z-index: 9600;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 16px;
    }
    .config-modal {
      box-sizing: border-box;
      background: rgba(12, 18, 21, 0.97);
      border-radius: var(--radius-lg, 24px);
      border: 1px solid rgba(255, 111, 111, 0.5);
      box-shadow: 0 34px 120px rgba(0, 0, 0, 0.45);
      color: var(--text, #f5f8f2);
      width: min(420px, 92vw);
      padding: 24px;
    }
    .config-modal__title {
      margin: 0 0 12px;
      color: #ff6f6f;
      font-size: 1.15rem;
    }
    .config-modal__message {
      margin: 0 0 20px;
      color: var(--text-subtle);
      line-height: 1.5;
    }
    .config-modal__actions {
      display: flex;
      justify-content: flex-end;
    }
  `;
    document.head.appendChild(style);
};
// ---------------------------------------------------------------------------
// Toast notifications
// ---------------------------------------------------------------------------
const ensureToastContainer = () => {
    let container = document.querySelector(".config-toast-container");
    if (!container) {
        container = document.createElement("div");
        container.className = "config-toast-container";
        document.body.appendChild(container);
    }
    return container;
};
const showNotification = (message, kind = "success") => {
    const container = ensureToastContainer();
    const toast = document.createElement("div");
    toast.className = `config-toast config-toast--${kind}`;
    toast.setAttribute("role", "status");
    const text = document.createElement("span");
    text.textContent = message;
    toast.append(text);
    const closeBtn = document.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "config-toast__close";
    closeBtn.setAttribute("aria-label", "Dismiss notification");
    closeBtn.textContent = "×";
    closeBtn.addEventListener("click", () => toast.remove());
    toast.append(closeBtn);
    container.append(toast);
    window.setTimeout(() => toast.remove(), 8000);
};
const showAlertModal = (title, message) => {
    const backdrop = document.createElement("div");
    backdrop.className = "config-modal-backdrop";
    backdrop.setAttribute("role", "dialog");
    backdrop.setAttribute("aria-modal", "true");
    backdrop.setAttribute("aria-label", title);
    const dialog = document.createElement("div");
    dialog.className = "config-modal";
    const heading = document.createElement("h3");
    heading.className = "config-modal__title";
    heading.textContent = title;
    const body = document.createElement("p");
    body.className = "config-modal__message";
    body.textContent = message;
    const actions = document.createElement("div");
    actions.className = "config-modal__actions";
    const okBtn = document.createElement("button");
    okBtn.type = "button";
    okBtn.className = "button button--primary";
    okBtn.textContent = "OK";
    actions.append(okBtn);
    dialog.append(heading, body, actions);
    backdrop.append(dialog);
    document.body.append(backdrop);
    const close = () => {
        backdrop.remove();
        document.removeEventListener("keydown", onKey);
    };
    const onKey = (event) => {
        if (event.key === "Escape")
            close();
    };
    okBtn.addEventListener("click", close);
    backdrop.addEventListener("click", (event) => {
        if (event.target === backdrop)
            close();
    });
    document.addEventListener("keydown", onKey);
    okBtn.focus();
};
// ---------------------------------------------------------------------------
// Password field with a show/hide toggle
// ---------------------------------------------------------------------------
const EYE_ICON = '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="M12 5c-7 0-10 7-10 7s3 7 10 7 10-7 10-7-3-7-10-7Zm0 12a5 5 0 1 1 0-10 5 5 0 0 1 0 10Zm0-8a3 3 0 1 0 0 6 3 3 0 0 0 0-6Z"/></svg>';
const EYE_OFF_ICON = '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path fill="currentColor" d="m3.28 2.22-1.06 1.06 3.4 3.4C3.86 8.2 2.4 9.9 1 12c0 0 3 7 11 7 1.98 0 3.68-.44 5.11-1.1l3.6 3.6 1.07-1.06L3.28 2.22ZM12 17c-5.06 0-7.66-3.94-8.44-5C4.32 10.5 5.8 8.7 7.9 7.62l1.9 1.9A5 5 0 0 0 12 17a4.9 4.9 0 0 0 1.86-.37l1.5 1.5A9.6 9.6 0 0 1 12 17Zm-1.9-6.8 3.7 3.7A3 3 0 0 1 10.1 10.2ZM12 6.5c1.5 0 2.87.28 4.08.75l1.46-1.46C15.9 5.1 14.06 4.5 12 4.5c-.86 0-1.68.09-2.46.26l1.6 1.6c.28-.03.57-.05.86-.05Zm9.44 5.5c-.4.56-1.13 1.5-2.19 2.44l1.06 1.06C21.65 14.2 22.6 12.94 23 12c0 0-1.44-3.39-5.06-5.63l-1.06 1.06C19.24 8.7 20.5 10.4 21.44 12Z"/></svg>';
const createPasswordField = (placeholder, extraInputClass = "") => {
    const wrapper = document.createElement("div");
    wrapper.className = "config-password-field";
    const input = document.createElement("input");
    input.type = "password";
    input.className = `catalog__search ${extraInputClass}`.trim();
    input.placeholder = placeholder;
    input.autocomplete = "off";
    input.spellcheck = false;
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "config-password-field__toggle";
    toggle.setAttribute("aria-label", "Show password");
    toggle.innerHTML = EYE_ICON;
    toggle.addEventListener("click", () => {
        const willShow = input.type === "password";
        input.type = willShow ? "text" : "password";
        toggle.innerHTML = willShow ? EYE_OFF_ICON : EYE_ICON;
        toggle.setAttribute("aria-label", willShow ? "Hide password" : "Show password");
    });
    wrapper.append(input, toggle);
    return { wrapper, input };
};
// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
document.addEventListener("DOMContentLoaded", () => {
    ensureConfigStyles();
    const connectBtn = document.querySelector("[data-config-connect]");
    const disconnectBtn = document.querySelector("[data-config-disconnect]");
    const rebootToggle = document.querySelector("[data-config-reboot-toggle]");
    const rebootWrapper = document.querySelector("[data-config-reboot-wrapper]");
    const statusSection = document.querySelector("[data-status-section]");
    const statusTile = document.querySelector("[data-config-status]");
    const logEl = document.querySelector("[data-config-log]");
    const logToggleBtn = document.querySelector("[data-config-log-toggle]");
    const logLinesEl = document.querySelector("[data-config-log-lines]");
    const logForm = document.querySelector("[data-config-log-form]");
    const logInput = document.querySelector("[data-config-log-input]");
    const logSendBtn = document.querySelector("[data-config-log-send]");
    const deviceSection = document.querySelector("[data-device-section]");
    const versionEl = document.querySelector("[data-config-version]");
    const whoamiEl = document.querySelector("[data-config-whoami]");
    const wifiSection = document.querySelector("[data-wifi-section]");
    const wifiScanBtn = document.querySelector("[data-wifi-scan]");
    const wifiListBtn = document.querySelector("[data-wifi-list-btn]");
    const wifiList = document.querySelector("[data-wifi-list]");
    const wifiAddToggle = document.querySelector("[data-wifi-add-toggle]");
    const wifiAddForm = document.querySelector("[data-wifi-add-form]");
    const wifiAddSsid = document.querySelector("[data-wifi-add-ssid]");
    const wifiAddPwdSlot = document.querySelector("[data-wifi-add-pwd-slot]");
    const wifiAddSubmit = document.querySelector("[data-wifi-add-submit]");
    const wifiDelToggle = document.querySelector("[data-wifi-del-toggle]");
    const wifiDelForm = document.querySelector("[data-wifi-del-form]");
    const wifiDelSsid = document.querySelector("[data-wifi-del-ssid]");
    const wifiDelSubmit = document.querySelector("[data-wifi-del-submit]");
    const wifiClearBtn = document.querySelector("[data-wifi-clear]");
    const calibSection = document.querySelector("[data-calibration-section]");
    const calibX0 = document.querySelector("[data-calib-x0]");
    const calibX1 = document.querySelector("[data-calib-x1]");
    const calibY0 = document.querySelector("[data-calib-y0]");
    const calibY1 = document.querySelector("[data-calib-y1]");
    const calibRot = document.querySelector("[data-calib-rot]");
    const calibStartBtn = document.querySelector("[data-calib-start]");
    const calibSetBtn = document.querySelector("[data-calib-set]");
    const calibMirrorXBtn = document.querySelector("[data-calib-mirror-x]");
    const calibMirrorYBtn = document.querySelector("[data-calib-mirror-y]");
    const calibSwapBtn = document.querySelector("[data-calib-swap]");
    const settingsSection = document.querySelector("[data-settings-section]");
    const settingsRefreshBtn = document.querySelector("[data-settings-refresh]");
    const settingsSaveBtn = document.querySelector("[data-settings-save]");
    const settingsMetaEl = document.querySelector("[data-settings-meta]");
    const settingsFormEl = document.querySelector("[data-settings-form]");
    if (!connectBtn ||
        !disconnectBtn ||
        !rebootToggle ||
        !rebootWrapper ||
        !statusSection ||
        !statusTile ||
        !logEl ||
        !logToggleBtn ||
        !logLinesEl ||
        !logForm ||
        !logInput ||
        !logSendBtn ||
        !deviceSection ||
        !versionEl ||
        !whoamiEl ||
        !wifiSection ||
        !wifiScanBtn ||
        !wifiListBtn ||
        !wifiList ||
        !wifiAddToggle ||
        !wifiAddForm ||
        !wifiAddSsid ||
        !wifiAddPwdSlot ||
        !wifiAddSubmit ||
        !wifiDelToggle ||
        !wifiDelForm ||
        !wifiDelSsid ||
        !wifiDelSubmit ||
        !wifiClearBtn ||
        !calibSection ||
        !calibX0 ||
        !calibX1 ||
        !calibY0 ||
        !calibY1 ||
        !calibRot ||
        !calibStartBtn ||
        !calibSetBtn ||
        !calibMirrorXBtn ||
        !calibMirrorYBtn ||
        !calibSwapBtn ||
        !settingsSection ||
        !settingsRefreshBtn ||
        !settingsSaveBtn ||
        !settingsMetaEl ||
        !settingsFormEl) {
        return;
    }
    const wifiAddPwdField = createPasswordField("Password");
    wifiAddPwdSlot.replaceWith(wifiAddPwdField.wrapper);
    const wifiAddPwd = wifiAddPwdField.input;
    const setStatus = (message) => {
        statusTile.textContent = message;
    };
    const appendLog = (text, kind = "rx") => {
        const time = new Date().toLocaleTimeString();
        const row = document.createElement("div");
        row.className = `config-log__line config-log__line--${kind}`;
        row.textContent = `[${time}] ${kind === "tx" ? ">>" : "<<"} ${text}`;
        logLinesEl.appendChild(row);
        logLinesEl.scrollTop = logLinesEl.scrollHeight;
    };
    logToggleBtn.addEventListener("click", () => {
        const willShow = logEl.hidden;
        logEl.hidden = !willShow;
        logToggleBtn.textContent = willShow ? "Hide console" : "Show console";
        if (willShow) {
            logLinesEl.scrollTop = logLinesEl.scrollHeight;
            if (!logInput.disabled)
                logInput.focus();
        }
    });
    let session = null;
    let busy = false;
    const actionButtons = [
        wifiScanBtn,
        wifiListBtn,
        wifiAddToggle,
        wifiAddSubmit,
        wifiDelToggle,
        wifiDelSubmit,
        wifiClearBtn,
        calibStartBtn,
        calibSetBtn,
        calibMirrorXBtn,
        calibMirrorYBtn,
        calibSwapBtn,
        settingsRefreshBtn,
        settingsSaveBtn
    ];
    // Re-run per-row validity checks (e.g. "Connect" needs 8+ char password)
    // after a busy period ends, since re-enabling every wifi-list button
    // unconditionally would clobber those constraints.
    let wifiRowValidators = [];
    const setBusy = (value) => {
        busy = value;
        actionButtons.forEach((btn) => (btn.disabled = value));
        wifiList.querySelectorAll("button").forEach((btn) => (btn.disabled = value));
        if (!value) {
            wifiRowValidators.forEach((fn) => fn());
        }
        logInput.disabled = value || !session;
        logSendBtn.disabled = value || !session;
    };
    const logSession = (targetSession) => {
        targetSession.onLine((line) => appendLog(line, "rx"));
    };
    const runCommand = async (label, command, opts, displayCommand) => {
        if (!session)
            return [];
        appendLog(displayCommand ?? command, "tx");
        const lines = await sendAndCollect(session, command, opts);
        return lines;
    };
    const PASSWORD_MASK = "********";
    const notifyWifiConnectResult = (lines, ssid) => {
        const resultLine = lines
            .map((l) => l.trim())
            .find((l) => /^OK connected to/i.test(l) || /^ERR/i.test(l));
        if (!resultLine)
            return;
        const okMatch = resultLine.match(/^OK connected to (.+?),\s*ip=(\S+)/i);
        if (okMatch) {
            showNotification(`Connected to ${okMatch[1]} — IP ${okMatch[2]}`, "success");
        }
        else {
            showNotification(`Failed to connect to ${ssid}: ${resultLine.replace(/^ERR\s*/i, "")}`, "error");
        }
    };
    // -------------------------------------------------------------------------
    // WiFi
    // -------------------------------------------------------------------------
    const renderWifiNetworks = (networks) => {
        wifiList.innerHTML = "";
        wifiRowValidators = [];
        if (networks.length === 0) {
            const empty = document.createElement("li");
            empty.className = "config-wifi-row";
            empty.textContent = "No networks found. Try scanning again.";
            wifiList.appendChild(empty);
            return;
        }
        networks.forEach((network) => {
            const row = document.createElement("li");
            row.className = "config-wifi-row";
            const ssid = document.createElement("span");
            ssid.className = "config-wifi-row__ssid";
            ssid.textContent = network.ssid;
            row.append(ssid);
            const meta = document.createElement("span");
            meta.className = "config-wifi-row__meta";
            meta.textContent = `${network.auth} · ${network.rssi} dBm`;
            row.append(meta);
            const authBadge = document.createElement("span");
            authBadge.className = "config-badge";
            authBadge.textContent = network.auth;
            row.append(authBadge);
            if (network.saved) {
                const savedBadge = document.createElement("span");
                savedBadge.className = "config-badge config-badge--saved";
                savedBadge.textContent = "Saved";
                row.append(savedBadge);
            }
            // Secured networks get a password field. Unsaved ones require it to
            // connect at all; saved ones can leave it blank to reconnect with the
            // stored credential, or fill it in to replace that saved password.
            const showPasswordField = network.auth !== "open";
            const passwordRequired = showPasswordField && !network.saved;
            const MIN_WIFI_PASSWORD_LENGTH = 8; // shortest possible WPA/WPA2 passphrase
            let pwdInput = null;
            let pwdWrapper = null;
            if (showPasswordField) {
                const field = createPasswordField(network.saved ? "New password (optional)" : "Password (min. 8 characters)");
                field.wrapper.classList.add("config-wifi-row__pwd");
                pwdInput = field.input;
                pwdWrapper = field.wrapper;
                row.append(field.wrapper);
            }
            const connectNetworkBtn = document.createElement("button");
            connectNetworkBtn.type = "button";
            connectNetworkBtn.className = "button button--ghost";
            connectNetworkBtn.textContent = "Connect";
            const clearPasswordError = () => pwdWrapper?.classList.remove("config-password-field--error");
            const updateConnectAvailability = () => {
                if (!passwordRequired) {
                    connectNetworkBtn.disabled = false;
                    return;
                }
                const pwd = pwdInput?.value ?? "";
                connectNetworkBtn.disabled = pwd.length < MIN_WIFI_PASSWORD_LENGTH;
            };
            if (pwdInput) {
                pwdInput.addEventListener("input", () => {
                    clearPasswordError();
                    updateConnectAvailability();
                });
            }
            updateConnectAvailability();
            wifiRowValidators.push(updateConnectAvailability);
            connectNetworkBtn.addEventListener("click", async () => {
                if (busy)
                    return;
                const pwd = pwdInput?.value.trim() ?? "";
                if (passwordRequired && pwd.length < MIN_WIFI_PASSWORD_LENGTH) {
                    pwdWrapper?.classList.add("config-password-field--error");
                    showAlertModal("Missing Password", `Enter a WiFi password of at least ${MIN_WIFI_PASSWORD_LENGTH} characters for "${network.ssid}" to connect.`);
                    return;
                }
                if (network.saved && pwd.length > 0 && pwd.length < MIN_WIFI_PASSWORD_LENGTH) {
                    pwdWrapper?.classList.add("config-password-field--error");
                    showAlertModal("Missing Password", `The new password for "${network.ssid}" must be at least ${MIN_WIFI_PASSWORD_LENGTH} characters, or left blank to keep the saved one.`);
                    return;
                }
                setBusy(true);
                if (network.saved && pwd.length > 0) {
                    setStatus(`Updating saved password for ${network.ssid}...`);
                    await runCommand("wifi del", `wifi del ${network.ssid}`, { idleMs: 400, maxMs: 4000 });
                    await sleep(200);
                    await runCommand("wifi add", `wifi add ${network.ssid} ${pwd}`, { idleMs: 400, maxMs: 4000 }, `wifi add ${network.ssid} ${PASSWORD_MASK}`);
                }
                setStatus(`Connecting to ${network.ssid}...`);
                const command = passwordRequired ? `wifi connect ${network.ssid} ${pwd}` : `wifi connect ${network.ssid}`;
                const displayCommand = passwordRequired
                    ? `wifi connect ${network.ssid} ${PASSWORD_MASK}`
                    : command;
                const lines = await runCommand("wifi connect", command, { idleMs: 500, maxMs: 15000 }, displayCommand);
                setStatus(firstNonEmptyLine(lines) || `No response connecting to ${network.ssid}.`);
                notifyWifiConnectResult(lines, network.ssid);
                setBusy(false);
            });
            row.append(connectNetworkBtn);
            if (network.saved) {
                const deleteBtn = document.createElement("button");
                deleteBtn.type = "button";
                deleteBtn.className = "button button--warning";
                deleteBtn.textContent = "Delete";
                deleteBtn.addEventListener("click", async () => {
                    if (busy)
                        return;
                    setBusy(true);
                    setStatus(`Removing ${network.ssid}...`);
                    const lines = await runCommand("wifi del", `wifi del ${network.ssid}`, { idleMs: 400, maxMs: 4000 });
                    setStatus(firstNonEmptyLine(lines) || `No response removing ${network.ssid}.`);
                    row.remove();
                    setBusy(false);
                });
                row.append(deleteBtn);
            }
            wifiList.appendChild(row);
        });
    };
    wifiScanBtn.addEventListener("click", async () => {
        if (busy || !session)
            return;
        setBusy(true);
        setStatus("Scanning for WiFi networks...");
        wifiList.innerHTML = "";
        const lines = await runCommand("wifi scan", "wifi scan", { idleMs: 500, maxMs: 8000 });
        const networks = parseWifiScan(lines);
        renderWifiNetworks(networks);
        setStatus(`Found ${networks.length} network(s).`);
        setBusy(false);
    });
    wifiListBtn.addEventListener("click", async () => {
        if (busy || !session)
            return;
        setBusy(true);
        setStatus("Listing saved WiFi networks...");
        wifiList.innerHTML = "";
        const lines = await runCommand("wifi list", "wifi list", { idleMs: 500, maxMs: 8000 });
        // Every entry here is inherently saved, whether or not the device's
        // reply happens to include the "(Saved)" suffix like "wifi scan" does.
        const networks = parseWifiScan(lines).map((network) => ({ ...network, saved: true }));
        renderWifiNetworks(networks);
        setStatus(`${networks.length} saved network(s).`);
        setBusy(false);
    });
    wifiAddToggle.addEventListener("click", () => {
        wifiAddForm.hidden = !wifiAddForm.hidden;
        wifiDelForm.hidden = true;
    });
    wifiDelToggle.addEventListener("click", () => {
        wifiDelForm.hidden = !wifiDelForm.hidden;
        wifiAddForm.hidden = true;
    });
    wifiAddSubmit.addEventListener("click", async () => {
        if (busy || !session)
            return;
        const ssid = wifiAddSsid.value.trim();
        const pwd = wifiAddPwd.value.trim();
        if (!ssid) {
            setStatus("Enter an SSID to save.");
            return;
        }
        setBusy(true);
        setStatus(`Saving ${ssid}...`);
        const lines = await runCommand("wifi add", `wifi add ${ssid} ${pwd}`, { idleMs: 400, maxMs: 4000 }, `wifi add ${ssid} ${PASSWORD_MASK}`);
        setStatus(firstNonEmptyLine(lines) || `No response saving ${ssid}.`);
        setBusy(false);
    });
    wifiDelSubmit.addEventListener("click", async () => {
        if (busy || !session)
            return;
        const ssid = wifiDelSsid.value.trim();
        if (!ssid) {
            setStatus("Enter an SSID to delete.");
            return;
        }
        setBusy(true);
        setStatus(`Removing ${ssid}...`);
        const lines = await runCommand("wifi del", `wifi del ${ssid}`, { idleMs: 400, maxMs: 4000 });
        setStatus(firstNonEmptyLine(lines) || `No response removing ${ssid}.`);
        setBusy(false);
    });
    wifiClearBtn.addEventListener("click", async () => {
        if (busy || !session)
            return;
        const confirmed = window.confirm("This deletes ALL WiFi networks saved on the device. This cannot be undone. Continue?");
        if (!confirmed)
            return;
        setBusy(true);
        setStatus("Clearing all saved networks...");
        const lines = await runCommand("wifi clear", "wifi clear", { idleMs: 400, maxMs: 4000 });
        setStatus(firstNonEmptyLine(lines) || "No response clearing networks.");
        wifiList.innerHTML = "";
        wifiRowValidators = [];
        setBusy(false);
    });
    // -------------------------------------------------------------------------
    // Touch calibration
    // -------------------------------------------------------------------------
    const applyCalibrationToFields = (calibration) => {
        calibX0.value = String(calibration.x0);
        calibX1.value = String(calibration.x1);
        calibY0.value = String(calibration.y0);
        calibY1.value = String(calibration.y1);
        calibRot.value = calibration.rot;
    };
    calibStartBtn.addEventListener("click", async () => {
        if (busy || !session)
            return;
        setBusy(true);
        setStatus("Starting the on-device calibration wizard. Follow the prompts on the touchscreen, then use Apply/Mirror/Swap or reconnect to read back the new values.");
        await runCommand("calibrate", "calibrate", { idleMs: 300, maxMs: 1500 });
        setBusy(false);
    });
    calibSetBtn.addEventListener("click", async () => {
        if (busy || !session)
            return;
        const x0 = calibX0.value.trim();
        const x1 = calibX1.value.trim();
        const y0 = calibY0.value.trim();
        const y1 = calibY1.value.trim();
        const rot = calibRot.value.trim();
        if (!x0 || !x1 || !y0 || !y1 || !rot) {
            setStatus("Fill in x0, x1, y0, y1 and rot before applying calibration.");
            return;
        }
        setBusy(true);
        setStatus("Applying calibration...");
        // The firmware's "calibrate set" takes <Xmax> <Xmin> <Ymax> <Ymin> <rot>,
        // while "calibrate show" prints x0/x1/y0/y1 in storage order (x0=Xmin,
        // x1=Xmax, y0=Ymin, y1=Ymax) — swap here so round-tripping the shown
        // values back through "set" reproduces them exactly.
        const setLines = await runCommand("calibrate set", `calibrate set ${x1} ${x0} ${y1} ${y0} ${rot}`, {
            idleMs: 400,
            maxMs: 4000
        });
        const showLines = await runCommand("calibrate show", "calibrate show", { idleMs: 400, maxMs: 4000 });
        const calibration = parseCalibration(showLines);
        if (calibration)
            applyCalibrationToFields(calibration);
        setStatus(firstNonEmptyLine(setLines) || "Calibration updated.");
        setBusy(false);
    });
    const bindCalibrationToggle = (button, command, label) => {
        button.addEventListener("click", async () => {
            if (busy || !session)
                return;
            setBusy(true);
            setStatus(`Applying ${label}...`);
            const lines = await runCommand(label, command, { idleMs: 400, maxMs: 4000 });
            const calibration = parseCalibration(lines);
            if (calibration) {
                applyCalibrationToFields(calibration);
                setStatus(`${label} applied.`);
            }
            else {
                setStatus(firstNonEmptyLine(lines) || `No response applying ${label}.`);
            }
            setBusy(false);
        });
    };
    bindCalibrationToggle(calibMirrorXBtn, "calibrate mirror X", "Mirror X");
    bindCalibrationToggle(calibMirrorYBtn, "calibrate mirror Y", "Mirror Y");
    bindCalibrationToggle(calibSwapBtn, "calibrate swapXY", "Swap XY");
    // -------------------------------------------------------------------------
    // Device settings ("settings get" / "settings set <json>")
    // -------------------------------------------------------------------------
    let settingsSnapshot = null;
    let settingsEditableKeys = [];
    const fetchSettingsWithRetry = async (maxAttempts = 5) => {
        for (let attempt = 1; attempt <= maxAttempts; attempt++) {
            setStatus(`Reading device settings (attempt ${attempt}/${maxAttempts})...`);
            const lines = await runCommand("settings get", "settings get", { idleMs: 500, maxMs: 6000 });
            const parsed = parseSettingsJson(lines);
            if (parsed)
                return parsed;
            if (attempt < maxAttempts)
                await sleep(300);
        }
        return null;
    };
    const addSettingsGroup = (title) => {
        const group = document.createElement("div");
        group.className = "config-settings-group";
        const heading = document.createElement("h3");
        heading.className = "config-settings-group__title";
        heading.textContent = title;
        const grid = document.createElement("div");
        group.append(heading, grid);
        settingsFormEl.append(group);
        return { group, grid };
    };
    const renderSettingsForm = (settings) => {
        settingsFormEl.innerHTML = "";
        settingsEditableKeys = [];
        const boolFields = SETTINGS_BOOL_FIELDS.filter((f) => typeof settings[f.key] === "boolean");
        if (boolFields.length > 0) {
            const { grid } = addSettingsGroup("Behavior");
            grid.className = "config-settings-toggles";
            boolFields.forEach((field) => {
                const label = document.createElement("label");
                label.className = "config-settings-toggle";
                const input = document.createElement("input");
                input.type = "checkbox";
                input.checked = Boolean(settings[field.key]);
                input.addEventListener("change", () => {
                    settings[field.key] = input.checked;
                });
                const text = document.createElement("span");
                text.className = "config-settings-toggle__text";
                const titleEl = document.createElement("span");
                titleEl.textContent = field.label;
                text.append(titleEl);
                if (field.help) {
                    const help = document.createElement("span");
                    help.className = "config-settings-toggle__help";
                    help.textContent = field.help;
                    text.append(help);
                }
                label.append(input, text);
                grid.append(label);
                settingsEditableKeys.push(field.key);
            });
        }
        const numberFields = SETTINGS_NUMBER_FIELDS.filter((f) => typeof settings[f.key] === "number");
        if (numberFields.length > 0) {
            const { grid } = addSettingsGroup("Display");
            grid.className = "config-settings-fields";
            numberFields.forEach((field) => {
                const wrapper = document.createElement("label");
                wrapper.className = "config-settings-field";
                const labelText = document.createElement("span");
                labelText.textContent = field.label;
                const input = document.createElement("input");
                input.className = "catalog__search";
                input.type = "number";
                if (field.min !== undefined)
                    input.min = String(field.min);
                if (field.max !== undefined)
                    input.max = String(field.max);
                input.value = String(settings[field.key]);
                input.addEventListener("input", () => {
                    const n = Number(input.value);
                    if (!Number.isNaN(n))
                        settings[field.key] = n;
                });
                wrapper.append(labelText, input);
                grid.append(wrapper);
                settingsEditableKeys.push(field.key);
            });
        }
        const colorFields = SETTINGS_COLOR_FIELDS.filter((f) => typeof settings[f.key] === "number");
        if (colorFields.length > 0) {
            const { grid } = addSettingsGroup("Colors");
            grid.className = "config-settings-fields";
            colorFields.forEach((field) => {
                const wrapper = document.createElement("label");
                wrapper.className = "config-settings-field";
                const labelText = document.createElement("span");
                labelText.textContent = field.label;
                const input = document.createElement("input");
                input.type = "color";
                input.value = rgb565ToHex(settings[field.key]);
                input.addEventListener("input", () => {
                    settings[field.key] = hexToRgb565(input.value);
                });
                wrapper.append(labelText, input);
                grid.append(wrapper);
                settingsEditableKeys.push(field.key);
            });
        }
        const stringFields = SETTINGS_STRING_FIELDS.filter((f) => typeof settings[f.key] === "string");
        if (stringFields.length > 0) {
            const { grid } = addSettingsGroup("Web UI");
            grid.className = "config-settings-fields";
            stringFields.forEach((field) => {
                const wrapper = document.createElement("label");
                wrapper.className = "config-settings-field";
                const labelText = document.createElement("span");
                labelText.textContent = field.label;
                wrapper.append(labelText);
                if (field.password) {
                    const pf = createPasswordField(field.label);
                    pf.input.value = String(settings[field.key]);
                    pf.input.addEventListener("input", () => {
                        settings[field.key] = pf.input.value;
                    });
                    wrapper.append(pf.wrapper);
                }
                else {
                    const input = document.createElement("input");
                    input.className = "catalog__search";
                    input.type = "text";
                    input.value = String(settings[field.key]);
                    input.addEventListener("input", () => {
                        settings[field.key] = input.value;
                    });
                    wrapper.append(input);
                }
                grid.append(wrapper);
                settingsEditableKeys.push(field.key);
            });
        }
        const rotationKeys = Object.keys(settings).filter((k) => isRotationKey(k) && typeof settings[k] === "number");
        if (rotationKeys.length > 0) {
            const { grid } = addSettingsGroup("Per-device screen rotation");
            grid.className = "config-settings-fields";
            rotationKeys.forEach((key) => {
                const wrapper = document.createElement("label");
                wrapper.className = "config-settings-field";
                const labelText = document.createElement("span");
                labelText.textContent = key;
                const input = document.createElement("input");
                input.className = "catalog__search";
                input.type = "number";
                input.min = "0";
                input.max = "7";
                input.value = String(settings[key]);
                input.addEventListener("input", () => {
                    const n = Number(input.value);
                    if (!Number.isNaN(n))
                        settings[key] = n;
                });
                wrapper.append(labelText, input);
                grid.append(wrapper);
                settingsEditableKeys.push(key);
            });
        }
        const knownKeys = new Set([
            ...SETTINGS_BOOL_FIELDS.map((f) => f.key),
            ...SETTINGS_NUMBER_FIELDS.map((f) => f.key),
            ...SETTINGS_COLOR_FIELDS.map((f) => f.key),
            ...SETTINGS_STRING_FIELDS.map((f) => f.key),
            ...rotationKeys
        ]);
        const otherKeys = Object.keys(settings).filter((k) => !SETTINGS_NON_EDITABLE_KEYS.has(k) && !knownKeys.has(k) && !isRotationKey(k));
        const editableOtherKeys = otherKeys.filter((k) => {
            const v = settings[k];
            return typeof v === "string" || typeof v === "number" || typeof v === "boolean";
        });
        if (editableOtherKeys.length > 0) {
            const { grid } = addSettingsGroup("Other");
            grid.className = "config-settings-fields";
            editableOtherKeys.forEach((key) => {
                const value = settings[key];
                const wrapper = document.createElement("label");
                wrapper.className = "config-settings-field";
                const labelText = document.createElement("span");
                labelText.textContent = key;
                wrapper.append(labelText);
                if (typeof value === "boolean") {
                    const input = document.createElement("input");
                    input.type = "checkbox";
                    input.checked = value;
                    input.addEventListener("change", () => {
                        settings[key] = input.checked;
                    });
                    wrapper.append(input);
                }
                else {
                    const input = document.createElement("input");
                    input.className = "catalog__search";
                    input.type = typeof value === "number" ? "number" : "text";
                    input.value = String(value);
                    input.addEventListener("input", () => {
                        settings[key] = typeof value === "number" ? Number(input.value) : input.value;
                    });
                    wrapper.append(input);
                }
                grid.append(wrapper);
                settingsEditableKeys.push(key);
            });
        }
        const wifiCount = Array.isArray(settings.wifi) ? settings.wifi.length : 0;
        const favoriteCount = Array.isArray(settings.favorite) ? settings.favorite.length : 0;
        settingsMetaEl.textContent = `${wifiCount} saved WiFi network(s), ${favoriteCount} favorite(s) — not editable here.`;
    };
    settingsRefreshBtn.addEventListener("click", async () => {
        if (busy || !session)
            return;
        setBusy(true);
        const settings = await fetchSettingsWithRetry();
        if (!settings) {
            setStatus("Could not read a valid settings JSON from the device.");
            showAlertModal("Settings unavailable", "The device did not return a complete settings JSON after several attempts. Try again.");
            setBusy(false);
            return;
        }
        settingsSnapshot = settings;
        renderSettingsForm(settingsSnapshot);
        setStatus("Device settings loaded.");
        setBusy(false);
    });
    settingsSaveBtn.addEventListener("click", async () => {
        if (busy || !session || !settingsSnapshot)
            return;
        setBusy(true);
        setStatus("Checking the current WiFi list before saving...");
        const listLines = await runCommand("wifi list", "wifi list", { idleMs: 500, maxMs: 6000 });
        const currentSsids = parseWifiScan(listLines)
            .map((n) => n.ssid)
            .sort();
        const loadedSsids = settingsWifiSsids(settingsSnapshot);
        if (!sameSsidSet(currentSsids, loadedSsids)) {
            showNotification("Saved WiFi networks changed on the device since settings were loaded — keeping the device's current list.", "error");
        }
        setStatus("Refreshing settings before saving...");
        const fresh = await fetchSettingsWithRetry();
        if (!fresh) {
            showAlertModal("Save failed", "Could not read the current settings from the device to merge your changes into. Nothing was saved.");
            setBusy(false);
            return;
        }
        // Always write wifi/favorite exactly as freshly read — never the possibly
        // stale copy from when the form was loaded — and only overlay the keys
        // this form actually has editors for.
        const payload = { ...fresh };
        settingsEditableKeys.forEach((key) => {
            payload[key] = settingsSnapshot[key];
        });
        setStatus("Saving settings...");
        const lines = await runCommand("settings set", `settings set ${JSON.stringify(payload)}`, {
            idleMs: 500,
            maxMs: 6000
        });
        settingsSnapshot = payload;
        renderSettingsForm(settingsSnapshot);
        setStatus(firstNonEmptyLine(lines) || "Settings saved.");
        showNotification("Device settings saved.", "success");
        setBusy(false);
    });
    // -------------------------------------------------------------------------
    // Connect / handshake
    // -------------------------------------------------------------------------
    const resetUi = () => {
        deviceSection.hidden = true;
        wifiSection.hidden = true;
        calibSection.hidden = true;
        settingsSection.hidden = true;
        settingsSnapshot = null;
        settingsEditableKeys = [];
        settingsFormEl.innerHTML = "";
        settingsMetaEl.textContent = "";
        wifiList.innerHTML = "";
        wifiRowValidators = [];
        wifiAddForm.hidden = true;
        wifiDelForm.hidden = true;
        versionEl.textContent = "—";
        whoamiEl.textContent = "—";
        connectBtn.hidden = false;
        rebootWrapper.hidden = false;
        statusSection.hidden = true;
        logLinesEl.innerHTML = "";
    };
    const disconnect = async () => {
        if (session) {
            await session.close();
            session = null;
        }
        resetUi();
        setBusy(false);
        setStatus('Not connected. Click "Connect Device" and select your board\'s serial port.');
    };
    disconnectBtn.addEventListener("click", () => {
        void disconnect();
    });
    const BOOT_BANNER = "Press the button to enter the Launcher!";
    const runHandshake = async (activeSession) => {
        if (rebootToggle.checked) {
            setStatus("Resetting device...");
            appendLog("(hardware reset via RTS/EN)", "tx");
            await activeSession.hardReset();
        }
        // Whether or not we asked for a reset, some boards reboot on their own
        // when the serial port opens (a DTR/RTS glitch some USB-serial drivers
        // introduce right on open). So on every attempt race the boot banner
        // against a "version" reply and handle whichever shows up, retrying a
        // few times in case the board is still mid-boot.
        const MAX_HANDSHAKE_ATTEMPTS = 5;
        let version = "";
        let lastVersionReply = [];
        for (let attempt = 1; attempt <= MAX_HANDSHAKE_ATTEMPTS; attempt++) {
            setStatus(`Reading device info (attempt ${attempt}/${MAX_HANDSHAKE_ATTEMPTS})...`);
            const bannerPromise = waitForLine(activeSession, (line) => line.includes(BOOT_BANNER), 4000).catch(() => null);
            const versionLines = await sendAndCollect(activeSession, "version", { idleMs: 300, maxMs: 3000 });
            lastVersionReply = versionLines;
            appendLog("version", "tx");
            const bannerLine = await bannerPromise;
            if (bannerLine) {
                setStatus("Boot banner seen — pressing through to the Launcher menu...");
                appendLog("nav SelPress", "tx");
                await activeSession.writeLine("nav SelPress");
                await sleep(1000);
                continue;
            }
            version = findLauncherVersionLine(versionLines);
            if (version)
                break;
        }
        const whoamiLines = await sendAndCollect(activeSession, "whoami", { idleMs: 300, maxMs: 4000 });
        appendLog("whoami", "tx");
        const whoami = firstNonEmptyLine(whoamiLines);
        if (!version) {
            const reply = firstNonEmptyLine(lastVersionReply);
            throw new Error(reply ? `Launcher not detected. Version reply: ${reply}` : "Launcher not detected");
        }
        const helpLines = await sendAndCollect(activeSession, "help", { idleMs: 500, maxMs: 5000 });
        appendLog("help", "tx");
        const hasCalibration = helpLines.some((line) => /calibrate/i.test(line));
        const hasSettings = helpLines.some((line) => /settings\s+get/i.test(line));
        versionEl.textContent = version || "Unknown";
        whoamiEl.textContent = whoami || "Unknown";
        deviceSection.hidden = false;
        wifiSection.hidden = false;
        if (hasCalibration) {
            const showLines = await sendAndCollect(activeSession, "calibrate show", { idleMs: 400, maxMs: 4000 });
            appendLog("calibrate show", "tx");
            const calibration = parseCalibration(showLines);
            if (calibration) {
                applyCalibrationToFields(calibration);
                calibSection.hidden = false;
            }
        }
        if (hasSettings) {
            const settings = await fetchSettingsWithRetry();
            if (settings) {
                settingsSnapshot = settings;
                renderSettingsForm(settingsSnapshot);
                settingsSection.hidden = false;
            }
        }
        setStatus(`Connected to ${whoami || "device"} (${version || "unknown version"}).`);
    };
    connectBtn.addEventListener("click", async () => {
        if (!("serial" in navigator)) {
            setStatus("WebSerial is not supported in this browser. Use Chrome or Edge over HTTPS.");
            return;
        }
        let port;
        try {
            port = await navigator.serial.requestPort();
        }
        catch (err) {
            if (err?.name === "NotFoundError")
                return;
            const msg = err instanceof Error ? err.message : String(err);
            setStatus(`Serial error: ${msg}`);
            return;
        }
        connectBtn.disabled = true;
        setStatus("Opening serial port...");
        logLinesEl.innerHTML = "";
        const newSession = new SerialSession(port);
        try {
            await newSession.open(115200);
        }
        catch (err) {
            const msg = err instanceof Error ? err.message : String(err);
            setStatus(`Failed to open serial port: ${msg}`);
            connectBtn.disabled = false;
            return;
        }
        session = newSession;
        logSession(newSession);
        newSession.onClose(() => {
            if (session !== newSession)
                return;
            session = null;
            resetUi();
            setBusy(false);
            setStatus("Device disconnected. Click \"Connect Device\" to reconnect.");
        });
        connectBtn.hidden = true;
        rebootWrapper.hidden = true;
        statusSection.hidden = false;
        connectBtn.disabled = false;
        setBusy(true);
        try {
            await runHandshake(newSession);
        }
        catch (err) {
            const msg = err instanceof Error ? err.message : String(err);
            appendLog(msg, "err");
            setStatus(`Connection failed: ${msg}`);
            if (msg.startsWith("Launcher not detected")) {
                showAlertModal("Launcher not detected", "The connected device did not report a Launcher version response. Make sure the Launcher app is running on the board and try again.");
                await disconnect();
            }
        }
        finally {
            setBusy(false);
        }
    });
    logForm.addEventListener("submit", (event) => {
        event.preventDefault();
        if (busy || !session)
            return;
        const command = logInput.value.trim();
        if (!command)
            return;
        logInput.value = "";
        void (async () => {
            setBusy(true);
            await runCommand("manual", command, { idleMs: 400, maxMs: 8000 });
            setBusy(false);
            logInput.focus();
        })();
    });
    // -------------------------------------------------------------------------
    // Remote control: arrow keys / Enter / Esc drive the device's InputHandler
    // via "nav <target>", mirroring its physical buttons.
    // -------------------------------------------------------------------------
    const NAV_KEY_TARGETS = {
        ArrowUp: "UpPress",
        ArrowDown: "DownPress",
        ArrowLeft: "PrevPress",
        ArrowRight: "NextPress",
        Enter: "SelPress",
        Escape: "EscPress"
    };
    const isEditableTarget = (target) => {
        if (!(target instanceof HTMLElement))
            return false;
        if (target.isContentEditable)
            return true;
        return target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT";
    };
    document.addEventListener("keydown", (event) => {
        if (!session || isEditableTarget(event.target))
            return;
        const navTarget = NAV_KEY_TARGETS[event.key];
        if (!navTarget)
            return;
        event.preventDefault();
        const command = `nav ${navTarget}`;
        appendLog(command, "tx");
        void session.writeLine(command);
    });
    window.addEventListener("beforeunload", () => {
        if (session)
            void session.close();
    });
});
