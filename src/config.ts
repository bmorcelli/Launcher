// ---------------------------------------------------------------------------
// Serial line transport
//
// Talks to the Launcher serial console (see src/serial_console.cpp upstream):
// one command per line in, and free-form lines back out. There is no
// end-of-response marker, so multi-line replies (help, wifi scan, ...) are
// collected until the line stream goes quiet for a short idle window.
// ---------------------------------------------------------------------------

type LineListener = (line: string) => void;

class SerialSession {
  private port: SerialPort;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private listeners = new Set<LineListener>();
  private closed = false;

  constructor(port: SerialPort) {
    this.port = port;
  }

  async open(baudRate = 115200): Promise<void> {
    await this.port.open({ baudRate });
    this.writer = this.port.writable!.getWriter();
    void this.readLoop();
  }

  private async readLoop(): Promise<void> {
    if (!this.port.readable) return;
    const reader = this.port.readable.getReader();
    this.reader = reader;
    const decoder = new TextDecoder();
    let buffer = "";
    try {
      while (!this.closed) {
        const { value, done } = await reader.read();
        if (done) break;
        if (!value) continue;
        buffer += decoder.decode(value, { stream: true });
        let idx: number;
        while ((idx = buffer.indexOf("\n")) >= 0) {
          let line = buffer.slice(0, idx);
          buffer = buffer.slice(idx + 1);
          if (line.endsWith("\r")) line = line.slice(0, -1);
          this.emit(line);
        }
      }
    } catch {
      // Port likely unplugged; onLine subscribers will simply stop hearing from us.
    } finally {
      try {
        reader.releaseLock();
      } catch {
        /* ignore */
      }
    }
  }

  private emit(line: string): void {
    this.listeners.forEach((listener) => listener(line));
  }

  onLine(listener: LineListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async writeLine(command: string): Promise<void> {
    if (!this.writer) throw new Error("Serial port is not open");
    await this.writer.write(new TextEncoder().encode(`${command}\n`));
  }

  // A hardware reset via the RTS line (wired to EN on ESP32 boards), not the
  // Launcher's own "reboot" console command: that command only exists inside
  // the Launcher app itself, so it's a no-op (or worse, undefined behavior)
  // if some other queued firmware has already taken over the serial console.
  // Toggling RTS resets the chip regardless of what's currently running,
  // which is what lets us reliably catch the boot banner afterwards.
  async hardReset(pulseMs = 150): Promise<void> {
    try {
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: true });
      await sleep(pulseMs);
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } catch {
      // Some OS/driver combinations don't expose RTS control; nothing more we can do.
    }
  }

  async close(): Promise<void> {
    this.closed = true;
    try {
      await this.reader?.cancel();
    } catch {
      /* ignore */
    }
    try {
      this.writer?.releaseLock();
    } catch {
      /* ignore */
    }
    try {
      await this.port.close();
    } catch {
      /* ignore */
    }
  }
}

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

const waitForLine = (
  session: SerialSession,
  predicate: (line: string) => boolean,
  timeoutMs: number
): Promise<string> =>
  new Promise((resolve, reject) => {
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
const sendAndCollect = (
  session: SerialSession,
  command: string,
  { idleMs = 350, maxMs = 8000 }: { idleMs?: number; maxMs?: number } = {}
): Promise<string[]> =>
  new Promise((resolve) => {
    const lines: string[] = [];
    let idleTimer: number | undefined;

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

// ---------------------------------------------------------------------------
// Parsing helpers for the console's plain-text replies
// ---------------------------------------------------------------------------

type WifiNetwork = {
  ssid: string;
  rssi: number;
  auth: string;
  saved: boolean;
};

// Printed with "%-32s rssi=%-4d auth=%-9s%s\n" — parse by fixed column so
// SSIDs containing spaces don't confuse a whitespace-based split.
const parseWifiScan = (lines: string[]): WifiNetwork[] => {
  const networks: WifiNetwork[] = [];
  for (const raw of lines) {
    if (raw.length <= 32) continue;
    const ssid = raw.slice(0, 32).trimEnd();
    const rest = raw.slice(32).trim();
    const match = rest.match(/^rssi=(-?\d+)\s+auth=(\S+)\s*(\(Saved\))?$/);
    if (!ssid || !match) continue;
    networks.push({
      ssid,
      rssi: Number(match[1]),
      auth: match[2],
      saved: Boolean(match[3])
    });
  }
  return networks;
};

type TouchCalibration = {
  x0: number;
  x1: number;
  y0: number;
  y1: number;
  rot: string;
};

const CALIBRATION_RE = /x0:(\d+)\s+x1:(\d+)\s+y0:(\d+)\s+y1:(\d+)\s+rot:0x([0-9A-Fa-f]+)/;

const parseCalibration = (lines: string[]): TouchCalibration | null => {
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

const firstNonEmptyLine = (lines: string[]): string =>
  lines.map((l) => l.trim()).find((l) => l.length > 0) ?? "";

// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------

const ensureConfigStyles = () => {
  if (document.getElementById("config-style")) return;
  const style = document.createElement("style");
  style.id = "config-style";
  style.textContent = `
    .config-log {
      margin-top: 12px;
      max-height: 220px;
      overflow-y: auto;
      background: rgba(6, 10, 12, 0.6);
      border: 1px solid rgba(0, 221, 0, 0.18);
      border-radius: var(--radius-sm);
      padding: 12px 14px;
      font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
      font-size: 0.82rem;
      line-height: 1.5;
      white-space: pre-wrap;
      word-break: break-word;
      color: rgba(245, 248, 242, 0.85);
    }
    .config-log__line--tx { color: var(--accent); }
    .config-log__line--rx { color: var(--text-subtle); }
    .config-log__line--err { color: #ff6f6f; }
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
    .config-wifi-row__pwd {
      width: 160px;
      padding: 8px 12px;
      font-size: 0.9rem;
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
    [data-config-connect]:disabled,
    [data-config-disconnect]:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
  `;
  document.head.appendChild(style);
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

document.addEventListener("DOMContentLoaded", () => {
  ensureConfigStyles();

  const connectBtn = document.querySelector<HTMLButtonElement>("[data-config-connect]");
  const disconnectBtn = document.querySelector<HTMLButtonElement>("[data-config-disconnect]");
  const statusTile = document.querySelector<HTMLElement>("[data-config-status]");
  const logEl = document.querySelector<HTMLElement>("[data-config-log]");

  const deviceSection = document.querySelector<HTMLElement>("[data-device-section]");
  const versionEl = document.querySelector<HTMLElement>("[data-config-version]");
  const whoamiEl = document.querySelector<HTMLElement>("[data-config-whoami]");

  const wifiSection = document.querySelector<HTMLElement>("[data-wifi-section]");
  const wifiScanBtn = document.querySelector<HTMLButtonElement>("[data-wifi-scan]");
  const wifiList = document.querySelector<HTMLElement>("[data-wifi-list]");
  const wifiAddToggle = document.querySelector<HTMLButtonElement>("[data-wifi-add-toggle]");
  const wifiAddForm = document.querySelector<HTMLElement>("[data-wifi-add-form]");
  const wifiAddSsid = document.querySelector<HTMLInputElement>("[data-wifi-add-ssid]");
  const wifiAddPwd = document.querySelector<HTMLInputElement>("[data-wifi-add-pwd]");
  const wifiAddSubmit = document.querySelector<HTMLButtonElement>("[data-wifi-add-submit]");
  const wifiDelToggle = document.querySelector<HTMLButtonElement>("[data-wifi-del-toggle]");
  const wifiDelForm = document.querySelector<HTMLElement>("[data-wifi-del-form]");
  const wifiDelSsid = document.querySelector<HTMLInputElement>("[data-wifi-del-ssid]");
  const wifiDelSubmit = document.querySelector<HTMLButtonElement>("[data-wifi-del-submit]");
  const wifiClearBtn = document.querySelector<HTMLButtonElement>("[data-wifi-clear]");

  const calibSection = document.querySelector<HTMLElement>("[data-calibration-section]");
  const calibX0 = document.querySelector<HTMLInputElement>("[data-calib-x0]");
  const calibX1 = document.querySelector<HTMLInputElement>("[data-calib-x1]");
  const calibY0 = document.querySelector<HTMLInputElement>("[data-calib-y0]");
  const calibY1 = document.querySelector<HTMLInputElement>("[data-calib-y1]");
  const calibRot = document.querySelector<HTMLInputElement>("[data-calib-rot]");
  const calibStartBtn = document.querySelector<HTMLButtonElement>("[data-calib-start]");
  const calibSetBtn = document.querySelector<HTMLButtonElement>("[data-calib-set]");
  const calibMirrorXBtn = document.querySelector<HTMLButtonElement>("[data-calib-mirror-x]");
  const calibMirrorYBtn = document.querySelector<HTMLButtonElement>("[data-calib-mirror-y]");
  const calibSwapBtn = document.querySelector<HTMLButtonElement>("[data-calib-swap]");

  if (
    !connectBtn ||
    !disconnectBtn ||
    !statusTile ||
    !logEl ||
    !deviceSection ||
    !versionEl ||
    !whoamiEl ||
    !wifiSection ||
    !wifiScanBtn ||
    !wifiList ||
    !wifiAddToggle ||
    !wifiAddForm ||
    !wifiAddSsid ||
    !wifiAddPwd ||
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
    !calibSwapBtn
  ) {
    return;
  }

  const setStatus = (message: string) => {
    statusTile.textContent = message;
  };

  const appendLog = (text: string, kind: "tx" | "rx" | "err" = "rx") => {
    logEl.hidden = false;
    const time = new Date().toLocaleTimeString();
    const row = document.createElement("div");
    row.className = `config-log__line config-log__line--${kind}`;
    row.textContent = `[${time}] ${kind === "tx" ? ">>" : "<<"} ${text}`;
    logEl.appendChild(row);
    logEl.scrollTop = logEl.scrollHeight;
  };

  let session: SerialSession | null = null;
  let busy = false;
  const actionButtons = [
    wifiScanBtn,
    wifiAddToggle,
    wifiAddSubmit,
    wifiDelToggle,
    wifiDelSubmit,
    wifiClearBtn,
    calibStartBtn,
    calibSetBtn,
    calibMirrorXBtn,
    calibMirrorYBtn,
    calibSwapBtn
  ];

  const setBusy = (value: boolean) => {
    busy = value;
    actionButtons.forEach((btn) => (btn.disabled = value));
    wifiList.querySelectorAll<HTMLButtonElement>("button").forEach((btn) => (btn.disabled = value));
  };

  const logSession = (targetSession: SerialSession) => {
    targetSession.onLine((line) => appendLog(line, "rx"));
  };

  const runCommand = async (
    label: string,
    command: string,
    opts?: { idleMs?: number; maxMs?: number }
  ): Promise<string[]> => {
    if (!session) return [];
    appendLog(command, "tx");
    const lines = await sendAndCollect(session, command, opts);
    return lines;
  };

  // -------------------------------------------------------------------------
  // WiFi
  // -------------------------------------------------------------------------

  const renderWifiNetworks = (networks: WifiNetwork[]) => {
    wifiList.innerHTML = "";
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

      let pwdInput: HTMLInputElement | null = null;
      if (network.auth !== "open") {
        pwdInput = document.createElement("input");
        pwdInput.type = "password";
        pwdInput.className = "catalog__search config-wifi-row__pwd";
        pwdInput.placeholder = "Password";
        row.append(pwdInput);
      }

      const connectNetworkBtn = document.createElement("button");
      connectNetworkBtn.type = "button";
      connectNetworkBtn.className = "button button--ghost";
      connectNetworkBtn.textContent = "Connect";
      connectNetworkBtn.addEventListener("click", async () => {
        if (busy) return;
        const pwd = pwdInput?.value.trim() ?? "";
        if (network.auth !== "open" && !pwd) {
          setStatus(`Enter a password for ${network.ssid} first.`);
          return;
        }
        setBusy(true);
        setStatus(`Connecting to ${network.ssid}...`);
        const command =
          network.auth === "open" ? `wifi connect ${network.ssid}` : `wifi connect ${network.ssid} ${pwd}`;
        const lines = await runCommand("wifi connect", command, { idleMs: 500, maxMs: 15000 });
        setStatus(firstNonEmptyLine(lines) || `No response connecting to ${network.ssid}.`);
        setBusy(false);
      });
      row.append(connectNetworkBtn);

      if (network.saved) {
        const deleteBtn = document.createElement("button");
        deleteBtn.type = "button";
        deleteBtn.className = "button button--warning";
        deleteBtn.textContent = "Delete";
        deleteBtn.addEventListener("click", async () => {
          if (busy) return;
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
    if (busy || !session) return;
    setBusy(true);
    setStatus("Scanning for WiFi networks...");
    wifiList.innerHTML = "";
    const lines = await runCommand("wifi scan", "wifi scan", { idleMs: 500, maxMs: 8000 });
    const networks = parseWifiScan(lines);
    renderWifiNetworks(networks);
    setStatus(`Found ${networks.length} network(s).`);
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
    if (busy || !session) return;
    const ssid = wifiAddSsid.value.trim();
    const pwd = wifiAddPwd.value.trim();
    if (!ssid) {
      setStatus("Enter an SSID to save.");
      return;
    }
    setBusy(true);
    setStatus(`Saving ${ssid}...`);
    const lines = await runCommand("wifi add", `wifi add ${ssid} ${pwd}`, { idleMs: 400, maxMs: 4000 });
    setStatus(firstNonEmptyLine(lines) || `No response saving ${ssid}.`);
    setBusy(false);
  });

  wifiDelSubmit.addEventListener("click", async () => {
    if (busy || !session) return;
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
    if (busy || !session) return;
    const confirmed = window.confirm(
      "This deletes ALL WiFi networks saved on the device. This cannot be undone. Continue?"
    );
    if (!confirmed) return;
    setBusy(true);
    setStatus("Clearing all saved networks...");
    const lines = await runCommand("wifi clear", "wifi clear", { idleMs: 400, maxMs: 4000 });
    setStatus(firstNonEmptyLine(lines) || "No response clearing networks.");
    wifiList.innerHTML = "";
    setBusy(false);
  });

  // -------------------------------------------------------------------------
  // Touch calibration
  // -------------------------------------------------------------------------

  const applyCalibrationToFields = (calibration: TouchCalibration) => {
    calibX0.value = String(calibration.x0);
    calibX1.value = String(calibration.x1);
    calibY0.value = String(calibration.y0);
    calibY1.value = String(calibration.y1);
    calibRot.value = calibration.rot;
  };

  calibStartBtn.addEventListener("click", async () => {
    if (busy || !session) return;
    setBusy(true);
    setStatus("Starting the on-device calibration wizard. Follow the prompts on the touchscreen, then use Apply/Mirror/Swap or reconnect to read back the new values.");
    await runCommand("calibrate", "calibrate", { idleMs: 300, maxMs: 1500 });
    setBusy(false);
  });

  calibSetBtn.addEventListener("click", async () => {
    if (busy || !session) return;
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
    if (calibration) applyCalibrationToFields(calibration);
    setStatus(firstNonEmptyLine(setLines) || "Calibration updated.");
    setBusy(false);
  });

  const bindCalibrationToggle = (button: HTMLButtonElement, command: string, label: string) => {
    button.addEventListener("click", async () => {
      if (busy || !session) return;
      setBusy(true);
      setStatus(`Applying ${label}...`);
      const lines = await runCommand(label, command, { idleMs: 400, maxMs: 4000 });
      const calibration = parseCalibration(lines);
      if (calibration) {
        applyCalibrationToFields(calibration);
        setStatus(`${label} applied.`);
      } else {
        setStatus(firstNonEmptyLine(lines) || `No response applying ${label}.`);
      }
      setBusy(false);
    });
  };

  bindCalibrationToggle(calibMirrorXBtn, "calibrate mirror X", "Mirror X");
  bindCalibrationToggle(calibMirrorYBtn, "calibrate mirror Y", "Mirror Y");
  bindCalibrationToggle(calibSwapBtn, "calibrate swapXY", "Swap XY");

  // -------------------------------------------------------------------------
  // Connect / handshake
  // -------------------------------------------------------------------------

  const resetUi = () => {
    deviceSection.hidden = true;
    wifiSection.hidden = true;
    calibSection.hidden = true;
    wifiList.innerHTML = "";
    wifiAddForm.hidden = true;
    wifiDelForm.hidden = true;
    versionEl.textContent = "—";
    whoamiEl.textContent = "—";
    connectBtn.hidden = false;
    disconnectBtn.hidden = true;
  };

  const disconnect = async () => {
    if (session) {
      await session.close();
      session = null;
    }
    resetUi();
    setStatus('Not connected. Click "Connect Device" and select your board\'s serial port.');
  };

  disconnectBtn.addEventListener("click", () => {
    void disconnect();
  });

  const runHandshake = async (activeSession: SerialSession) => {
    setStatus("Resetting device...");
    appendLog("(hardware reset via RTS/EN)", "tx");
    await activeSession.hardReset();

    setStatus('Waiting for the "Press the button to enter the Launcher!" banner...');
    await waitForLine(
      activeSession,
      (line) => line.includes("Press the button to enter the Launcher!"),
      20000
    );

    appendLog("nav SelPress", "tx");
    await activeSession.writeLine("nav SelPress");
    await sleep(1000);

    setStatus("Reading device info...");
    const versionLines = await sendAndCollect(activeSession, "version", { idleMs: 300, maxMs: 4000 });
    appendLog("version", "tx");
    const version = firstNonEmptyLine(versionLines);

    const whoamiLines = await sendAndCollect(activeSession, "whoami", { idleMs: 300, maxMs: 4000 });
    appendLog("whoami", "tx");
    const whoami = firstNonEmptyLine(whoamiLines);

    const helpLines = await sendAndCollect(activeSession, "help", { idleMs: 500, maxMs: 5000 });
    appendLog("help", "tx");
    const hasCalibration = helpLines.some((line) => /calibrate/i.test(line));

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

    setStatus(`Connected to ${whoami || "device"} (${version || "unknown version"}).`);
  };

  connectBtn.addEventListener("click", async () => {
    if (!("serial" in navigator)) {
      setStatus("WebSerial is not supported in this browser. Use Chrome or Edge over HTTPS.");
      return;
    }

    let port: SerialPort;
    try {
      port = await navigator.serial.requestPort();
    } catch (err) {
      if ((err as DOMException)?.name === "NotFoundError") return;
      const msg = err instanceof Error ? err.message : String(err);
      setStatus(`Serial error: ${msg}`);
      return;
    }

    connectBtn.disabled = true;
    setStatus("Opening serial port...");
    logEl.innerHTML = "";
    logEl.hidden = false;

    const newSession = new SerialSession(port);
    try {
      await newSession.open(115200);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setStatus(`Failed to open serial port: ${msg}`);
      connectBtn.disabled = false;
      return;
    }

    session = newSession;
    logSession(newSession);
    connectBtn.hidden = true;
    disconnectBtn.hidden = false;
    connectBtn.disabled = false;

    try {
      await runHandshake(newSession);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      appendLog(msg, "err");
      setStatus(`Connection failed: ${msg}`);
    }
  });

  window.addEventListener("beforeunload", () => {
    if (session) void session.close();
  });
});
