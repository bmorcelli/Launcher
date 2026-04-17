import { Transport, ESPLoader } from "esptool-js";

// ---------------------------------------------------------------------------
// Types (mirror of esp-web-tools const.ts)
// ---------------------------------------------------------------------------

type ManifestDevice = {
  display?: boolean;
  name: string;
  description?: string;
  link?: string;
  family: string;
  id: string;
};

type ManifestData = Record<string, ManifestDevice[]>;

type FlasherRelease = {
  tag: string;
  name: string;
  published_at: string;
  prerelease?: boolean;
  devices: Record<string, string>;
};

type FlasherData = {
  releases: FlasherRelease[];
  beta: FlasherRelease | null;
};

type SelectionState = {
  releaseValue: string;
  releaseLabel: string;
  selectedVersion: string;
  vendor: string;
  deviceId: string;
  deviceName: string;
};

const releaseOptions = [
  {
    value: "Release",
    label: "Last Release",
    description: "Stable builds recommended for everyday use."
  },
  {
    value: "Beta",
    label: "Beta Release",
    description: "Early-access updates with the newest features."
  }
] as const;

// ---------------------------------------------------------------------------
// Selection card styles
// ---------------------------------------------------------------------------

const ensureSelectionStyles = () => {
  if (document.getElementById("webflasher-selection-style")) {
    return;
  }
  const style = document.createElement("style");
  style.id = "webflasher-selection-style";
  style.textContent = `
    .selection-card {
      border-radius: var(--radius-md);
      border: 1px solid rgba(0, 221, 0, 0.2);
      background: rgba(28, 32, 36, 0.8);
      display: grid;
      gap: 12px;
      padding: 20px;
      text-align: left;
      cursor: pointer;
      transition: transform 0.2s ease, box-shadow 0.2s ease, border-color 0.2s ease;
    }
    .selection-card--device {
      padding-bottom: 72px;
    }
    .selection-card:hover {
      transform: translateY(-2px);
      border-color: rgba(0, 221, 0, 0.4);
    }
    .selection-card strong {
      font-size: 1.1rem;
    }
    .selection-card[data-selected="true"] {
      border-color: rgba(224, 210, 4, 0.7);
      box-shadow: 0 0 0 2px rgba(224, 210, 4, 0.25);
    }
    .selection-card:focus-visible {
      outline: 2px solid rgba(224, 210, 4, 0.7);
      outline-offset: 3px;
    }
    .selection-card-wrapper {
      position: relative;
      height: 100%;
    }
    .selection-card-wrapper .selection-card {
      width: 100%;
      height: 100%;
    }
    .selection-card__store-link {
      position: absolute;
      right: 20px;
      bottom: 20px;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 40px;
      height: 40px;
      border-radius: 50%;
      background: rgba(20, 24, 28, 0.9);
      box-shadow: 0 8px 18px rgba(0, 0, 0, 0.35);
      color: #facc15;
      transition: transform 0.2s ease, box-shadow 0.2s ease;
      text-decoration: none;
      pointer-events: auto;
    }
    .selection-card__store-link:hover,
    .selection-card__store-link:focus-visible {
      transform: translateY(-2px) scale(1.05);
      box-shadow: 0 10px 22px rgba(0, 0, 0, 0.45);
      outline: none;
    }
    .selection-card__store-link svg {
      width: 22px;
      height: 22px;
    }
    .selection-card__badge {
      position: absolute;
      top: 12px;
      right: 12px;
      padding: 4px 10px;
      border-radius: 999px;
      background: rgba(224, 210, 4, 0.92);
      color: #05160d;
      font-size: 0.75rem;
      font-weight: 600;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      box-shadow: 0 4px 14px rgba(224, 210, 4, 0.35);
    }
    [data-device-container] {
      grid-auto-rows: 1fr;
    }
    [data-device-container] > .selection-card-wrapper {
      height: 100%;
    }
  `;
  document.head.appendChild(style);
};

const createSelectionCard = (config: {
  title: string;
  subtitle?: string;
  value: string;
}): HTMLButtonElement => {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "selection-card";
  button.dataset.value = config.value;
  button.setAttribute("aria-pressed", "false");
  button.innerHTML = `
    <strong>${config.title}</strong>
    ${config.subtitle ? `<p>${config.subtitle}</p>` : ""}
  `;
  return button;
};

// ---------------------------------------------------------------------------
// Flash dialog
// ---------------------------------------------------------------------------

const FLASH_DIALOG_STYLE_ID = "webflasher-dialog-style";

const ROCKET_STYLE = `
  .rocket-progress {
    position: relative;
    width: min(360px, 82vw);
    padding: 32px 28px 68px;
    display: grid;
    gap: 24px;
    justify-items: center;
    background: linear-gradient(180deg, rgba(0, 221, 0, 0.16) 0%, rgba(0, 0, 0, 0) 65%);
    border-radius: var(--radius-md, 16px);
    border: 1px solid rgba(0, 221, 0, 0.22);
    box-shadow: 0 18px 48px rgba(0, 221, 0, 0.2);
    overflow: hidden;
  }
  .rocket-progress[data-indeterminate="true"] .rocket-progress__rocket {
    animation: rocket-indeterminate 3.4s ease-in-out infinite;
  }
  .rocket-progress__scene {
    position: relative;
    width: 100%;
    height: 150px;
    border-radius: var(--radius-md, 16px);
    border: 1px solid rgba(0, 221, 0, 0.15);
    background: radial-gradient(circle at 15% 85%, rgba(0, 221, 0, 0.18), rgba(4, 8, 10, 0.9) 65%), linear-gradient(180deg, rgba(4, 8, 11, 0.85), rgba(4, 8, 11, 0.45));
    box-shadow: inset 0 0 0 1px rgba(224, 210, 4, 0.08);
    overflow: hidden;
  }
  .rocket-progress__scene::after {
    content: "";
    position: absolute;
    inset: 0;
    background-image: radial-gradient(circle at 12% 10%, rgba(224, 210, 4, 0.6) 0, rgba(224, 210, 4, 0.6) 1px, transparent 1px), radial-gradient(circle at 82% 28%, rgba(245, 248, 242, 0.75) 0, rgba(245, 248, 242, 0.75) 1px, transparent 1px), radial-gradient(circle at 44% 42%, rgba(0, 221, 0, 0.4) 0, rgba(0, 221, 0, 0.4) 1px, transparent 1px), radial-gradient(circle at 65% 70%, rgba(245, 248, 242, 0.4) 0, rgba(245, 248, 242, 0.4) 1px, transparent 1px);
    opacity: 0.75;
    mix-blend-mode: screen;
    animation: rocket-stars 6s linear infinite;
  }
  .rocket-progress__earth {
    position: absolute;
    bottom: -36px;
    left: 12px;
    width: 132px;
    height: 132px;
    border-radius: 50%;
    background: radial-gradient(circle at 35% 25%, rgba(0, 221, 0, 0.9), rgba(0, 34, 14, 0.88) 70%);
    box-shadow: 0 -6px 18px rgba(0, 221, 0, 0.36);
  }
  .rocket-progress__moon {
    position: absolute;
    top: 20px;
    right: 18px;
    width: 54px;
    height: 54px;
    border-radius: 50%;
    background: radial-gradient(circle at 35% 35%, rgba(224, 210, 4, 0.95), rgba(88, 80, 0, 0.7) 80%);
    box-shadow: 0 0 24px rgba(224, 210, 4, 0.35);
  }
  .rocket-progress__rocket {
    position: absolute;
    left: 34px;
    bottom: 16px;
    width: 32px;
    height: 80px;
    transform: translate(calc(var(--rocket-progress, 0) * 220px), calc(var(--rocket-progress, 0) * -90px)) rotate(calc(var(--rocket-progress, 0) * 90deg));
    transform-origin: center;
    transition: transform 200ms ease;
  }
  .rocket-progress__rocket-body {
    position: relative;
    width: 100%;
    height: 100%;
    border-radius: 50% 50% 28% 28%;
    background: linear-gradient(180deg, rgba(245, 248, 242, 0.92) 0%, rgba(224, 210, 4, 0.82) 78%, rgba(255, 140, 64, 0.75) 100%);
    border: 1px solid rgba(224, 210, 4, 0.55);
    box-shadow: 0 12px 28px rgba(224, 210, 4, 0.24);
  }
  .rocket-progress__rocket-body::before {
    content: "";
    position: absolute;
    top: 18px;
    left: 50%;
    transform: translateX(-50%);
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: radial-gradient(circle at 30% 30%, rgba(0, 221, 0, 0.95), rgba(0, 32, 12, 0.85));
    box-shadow: 0 0 10px rgba(0, 221, 0, 0.45);
  }
  .rocket-progress__rocket-body::after {
    content: "";
    position: absolute;
    bottom: -16px;
    left: 50%;
    transform: translate(-50%, 4px) scale(0.9, 1.2);
    width: 22px;
    height: 26px;
    border-radius: 50% 50% 50% 50%;
    background: radial-gradient(circle at 50% 20%, rgba(255, 184, 96, 0.95), rgba(255, 94, 0, 0.8));
    filter: blur(0.4px);
    animation: rocket-flame 280ms ease-in-out infinite alternate;
  }
  .rocket-progress__rocket::before,
  .rocket-progress__rocket::after {
    content: "";
    position: absolute;
    bottom: 28px;
    width: 24px;
    height: 22px;
    border-radius: 40% 20% 10% 10%;
    background: linear-gradient(180deg, rgba(0, 221, 0, 0.7), rgba(6, 18, 10, 0.9));
    transition: inherit;
  }
  .rocket-progress__rocket::before {
    left: -18px;
    transform: skewY(-16deg);
  }
  .rocket-progress__rocket::after {
    right: -18px;
    transform: scaleX(-1) skewY(-16deg);
  }
  .rocket-progress__trail {
    position: absolute;
    inset: auto 50% 0 50%;
    width: 2px;
    height: 100%;
    background: linear-gradient(180deg, rgba(224, 210, 4, 0.15), rgba(0, 221, 0, 0));
    transform-origin: top;
    transform: translateX(-50%) scaleY(calc(var(--rocket-progress, 0) * 0.8 + 0.2));
    transition: transform 220ms ease;
    mix-blend-mode: screen;
  }
  .rocket-progress__counter {
    font-size: clamp(1.5rem, 2vw + 1rem, 2rem);
    font-weight: 700;
    letter-spacing: 0.08em;
    color: var(--accent);
    text-shadow: 0 0 18px rgba(224, 210, 4, 0.45);
  }
  .rocket-progress__label {
    margin: 0;
    font-size: 1rem;
    color: var(--text, #f5f8f2);
    text-align: center;
    text-shadow: 0 0 18px rgba(0, 221, 0, 0.25);
  }
  @keyframes rocket-flame {
    from { transform: translate(-50%, 6px) scale(0.85, 0.9); opacity: 0.75; }
    to   { transform: translate(-50%, 0) scale(1.05, 1.2); opacity: 1; }
  }
  @keyframes rocket-indeterminate {
    0%   { transform: translate(0, 0) rotate(-6deg); }
    30%  { transform: translate(70px, -40px) rotate(-12deg); }
    60%  { transform: translate(140px, -66px) rotate(-10deg); }
    100% { transform: translate(0, 0) rotate(-6deg); }
  }
  @keyframes rocket-stars {
    0%   { transform: translateY(0); }
    100% { transform: translateY(20px); }
  }
`;

const ROCKET_SCENE_TEMPLATE = `
  <div class="rocket-progress__earth"></div>
  <div class="rocket-progress__moon"></div>
  <div class="rocket-progress__rocket">
    <div class="rocket-progress__rocket-body"></div>
    <div class="rocket-progress__trail"></div>
  </div>
`;

const ensureFlashDialogStyles = () => {
  if (document.getElementById(FLASH_DIALOG_STYLE_ID)) {
    return;
  }
  const style = document.createElement("style");
  style.id = FLASH_DIALOG_STYLE_ID;
  style.textContent = `
    .wf-dialog-backdrop {
      position: fixed;
      inset: 0;
      background: rgba(6, 10, 12, 0.75);
      backdrop-filter: blur(6px);
      z-index: 9000;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 16px;
    }
    .wf-dialog {
      box-sizing: border-box;
      background: rgba(12, 18, 21, 0.97);
      border-radius: var(--radius-lg, 24px);
      border: 1px solid var(--primary, #00dd00);
      box-shadow: 0 34px 120px rgba(0, 221, 0, 0.25);
      color: var(--text, #f5f8f2);
      font-family: "Inter", "Segoe UI", system-ui, -apple-system, sans-serif;
      width: min(460px, 94vw);
      display: grid;
      gap: 0;
      overflow: hidden;
    }
    .wf-dialog__header {
      padding: 20px 24px 0;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }
    .wf-dialog__title {
      font-size: 1.15rem;
      font-weight: 600;
      color: var(--accent, #e0d204);
      margin: 0;
    }
    .wf-dialog__close {
      display: flex;
      align-items: center;
      justify-content: center;
      width: 32px;
      height: 32px;
      border-radius: 50%;
      border: none;
      background: transparent;
      color: var(--text-subtle, rgba(245,248,242,0.55));
      cursor: pointer;
      font-size: 1.1rem;
      transition: color 0.15s ease, background 0.15s ease;
      flex-shrink: 0;
    }
    .wf-dialog__close:hover { color: var(--text); background: rgba(255,255,255,0.06); }
    .wf-dialog__close:disabled { opacity: 0.3; cursor: not-allowed; }
    .wf-dialog__body {
      padding: 20px 24px;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 20px;
    }
    .wf-dialog__status {
      font-size: 0.9rem;
      color: var(--text-subtle, rgba(245,248,242,0.7));
      text-align: center;
      line-height: 1.5;
      min-height: 2.4em;
    }
    .wf-dialog__status--error {
      color: #ff6f6f;
    }
    .wf-dialog__status--success {
      color: var(--primary, #00dd00);
    }
    .wf-dialog__erase-row {
      display: flex;
      align-items: center;
      gap: 10px;
      font-size: 0.9rem;
      color: var(--text, #f5f8f2);
      cursor: pointer;
      user-select: none;
    }
    .wf-dialog__erase-row input[type="checkbox"] {
      width: 16px;
      height: 16px;
      accent-color: var(--primary, #00dd00);
      cursor: pointer;
    }
    .wf-dialog__footer {
      padding: 0 24px 20px;
      border-top: 1px solid rgba(0, 221, 0, 0.18);
      margin-top: 4px;
      padding-top: 18px;
      display: flex;
      justify-content: flex-end;
      gap: 12px;
    }
    .wf-btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 10px 24px;
      border-radius: var(--radius-lg, 24px);
      border: none;
      font-size: 0.9rem;
      font-weight: 600;
      letter-spacing: 0.06em;
      cursor: pointer;
      transition: transform 0.15s ease, box-shadow 0.15s ease, opacity 0.15s ease;
    }
    .wf-btn:disabled { opacity: 0.4; cursor: not-allowed; transform: none !important; box-shadow: none !important; }
    .wf-btn--primary {
      background: linear-gradient(135deg, rgba(0,221,0,0.92) 0%, rgba(224,210,4,0.88) 100%);
      color: #05160d;
      box-shadow: 0 8px 24px rgba(0,221,0,0.22);
    }
    .wf-btn--primary:not(:disabled):hover { transform: translateY(-1px); box-shadow: 0 12px 32px rgba(0,221,0,0.32); }
    .wf-btn--ghost {
      background: transparent;
      color: var(--accent, #e0d204);
      border: 1px solid rgba(224,210,4,0.3);
    }
    .wf-btn--ghost:not(:disabled):hover { border-color: rgba(224,210,4,0.6); }
    ${ROCKET_STYLE}
  `;
  document.head.appendChild(style);
};

// ---------------------------------------------------------------------------
// Flash dialog state machine
// ---------------------------------------------------------------------------

type FlashPhase =
  | "idle"
  | "connecting"
  | "preparing"
  | "erasing"
  | "writing"
  | "done"
  | "error";

interface FlashDialogState {
  phase: FlashPhase;
  message: string;
  progress: number | null; // null = indeterminate
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

const hardReset = async (transport: Transport, esploader: ESPLoader) => {
  await transport.setRTS(true);
  await sleep(100);
  await esploader.after();
};

const flashFirmware = async (
  port: SerialPort,
  manifestPath: string,
  eraseFirst: boolean,
  onState: (s: FlashDialogState) => void
) => {
  const fire = (phase: FlashPhase, message: string, progress: number | null = null) =>
    onState({ phase, message, progress });

  const transport = new Transport(port);
  const esploader = new ESPLoader({
    transport,
    baudrate: 115200,
    serialOptions: { bufferSize: 8192 },
    enableTracing: false,
  });

  fire("connecting", "Connecting to device...");

  try {
    await esploader.main();
    await esploader.flashId();
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    fire("error", `Failed to initialize. Try resetting your device or holding the BOOT button.\n${msg}`);
    await hardReset(transport, esploader).catch(() => {});
    await transport.disconnect().catch(() => {});
    return;
  }

  const chipFamily = esploader.chip.CHIP_NAME;
  fire("connecting", `Connected. Found ${chipFamily}.`);

  // Fetch manifest and find matching build
  let manifest: {
    name: string;
    builds: Array<{ chipFamily: string; parts: Array<{ path: string; offset: number }> }>;
  };
  try {
    const resp = await fetch(manifestPath);
    if (!resp.ok) {
      throw new Error(`HTTP ${resp.status}`);
    }
    manifest = await resp.json();
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    fire("error", `Failed to load manifest: ${msg}`);
    await hardReset(transport, esploader).catch(() => {});
    await transport.disconnect().catch(() => {});
    return;
  }

  const build = manifest.builds.find((b) => b.chipFamily === chipFamily);
  if (!build) {
    fire("error", `Your ${chipFamily} board is not supported by this firmware.`);
    await hardReset(transport, esploader).catch(() => {});
    await transport.disconnect().catch(() => {});
    return;
  }

  fire("preparing", "Downloading firmware...");

  const manifestURL = new URL(manifestPath, location.toString()).toString();
  let totalSize = 0;
  const fileArray: Array<{ data: Uint8Array; address: number }> = [];

  for (const part of build.parts) {
    const url = new URL(part.path, manifestURL).toString();
    try {
      const resp = await fetch(url);
      if (!resp.ok) {
        throw new Error(`HTTP ${resp.status}`);
      }
      const buffer = await resp.arrayBuffer();
      const data = new Uint8Array(buffer);
      fileArray.push({ data, address: part.offset });
      totalSize += data.length;
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      fire("error", `Failed to download firmware: ${msg}`);
      await hardReset(transport, esploader).catch(() => {});
      await transport.disconnect().catch(() => {});
      return;
    }
  }

  fire("preparing", "Firmware downloaded. Preparing to flash...", 0);

  if (eraseFirst) {
    fire("erasing", "Erasing device flash...");
    try {
      await esploader.eraseFlash();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      fire("error", `Erase failed: ${msg}`);
      await hardReset(transport, esploader).catch(() => {});
      await transport.disconnect().catch(() => {});
      return;
    }
    fire("erasing", "Erase complete.", 0);
  }

  fire("writing", "Writing firmware: 0%", 0);

  let totalWritten = 0;

  try {
    await esploader.writeFlash({
      fileArray,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex: number, written: number, total: number) => {
        const uncompressedWritten = (written / total) * fileArray[fileIndex].data.length;
        const pct = Math.floor(((totalWritten + uncompressedWritten) / totalSize) * 100);
        if (written === total) {
          totalWritten += fileArray[fileIndex].data.length;
          return;
        }
        fire("writing", `Writing firmware: ${pct}%`, pct);
      },
    });
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    fire("error", `Write failed: ${msg}`);
    await hardReset(transport, esploader).catch(() => {});
    await transport.disconnect().catch(() => {});
    return;
  }

  fire("writing", "Writing complete.", 100);

  await hardReset(transport, esploader).catch(() => {});
  await transport.disconnect().catch(() => {});

  fire("done", "Firmware installed successfully!", 100);
};

// ---------------------------------------------------------------------------
// Flash dialog UI
// ---------------------------------------------------------------------------

const openFlashDialog = (manifestUrl: string) => {
  ensureFlashDialogStyles();

  const backdrop = document.createElement("div");
  backdrop.className = "wf-dialog-backdrop";
  backdrop.setAttribute("role", "dialog");
  backdrop.setAttribute("aria-modal", "true");
  backdrop.setAttribute("aria-label", "Flash firmware");

  const dialog = document.createElement("div");
  dialog.className = "wf-dialog";
  dialog.innerHTML = `
    <div class="wf-dialog__header">
      <p class="wf-dialog__title">Flash Firmware</p>
      <button class="wf-dialog__close" aria-label="Close" data-wf-close>&#x2715;</button>
    </div>
    <div class="wf-dialog__body">
      <div class="rocket-progress" data-indeterminate="true">
        <div class="rocket-progress__scene">${ROCKET_SCENE_TEMPLATE}</div>
        <div class="rocket-progress__counter">0%</div>
      </div>
      <p class="rocket-progress__label">Select options and click Install.</p>
      <p class="wf-dialog__status" data-wf-status></p>
      <label class="wf-dialog__erase-row">
        <input type="checkbox" data-wf-erase />
        Erase device before flashing
      </label>
    </div>
    <div class="wf-dialog__footer">
      <button class="wf-btn wf-btn--ghost" data-wf-close>Cancel</button>
      <button class="wf-btn wf-btn--primary" data-wf-install>Install</button>
    </div>
  `;

  backdrop.appendChild(dialog);
  document.body.appendChild(backdrop);

  const rocketWrapper = dialog.querySelector<HTMLElement>(".rocket-progress")!;
  const counter = dialog.querySelector<HTMLElement>(".rocket-progress__counter")!;
  const label = dialog.querySelector<HTMLElement>(".rocket-progress__label")!;
  const statusEl = dialog.querySelector<HTMLElement>("[data-wf-status]")!;
  const eraseCheckbox = dialog.querySelector<HTMLInputElement>("[data-wf-erase]")!;
  const installBtn = dialog.querySelector<HTMLButtonElement>("[data-wf-install]")!;
  const closeBtns = dialog.querySelectorAll<HTMLButtonElement>("[data-wf-close]");

  let isFlashing = false;

  const close = () => {
    if (isFlashing) {
      return;
    }
    backdrop.remove();
  };

  closeBtns.forEach((btn) => btn.addEventListener("click", close));
  backdrop.addEventListener("click", (e) => {
    if (e.target === backdrop) {
      close();
    }
  });

  const setLocked = (locked: boolean) => {
    isFlashing = locked;
    installBtn.disabled = locked;
    eraseCheckbox.disabled = locked;
    closeBtns.forEach((btn) => (btn.disabled = locked));
  };

  const updateUI = (s: FlashDialogState) => {
    const pct = s.progress ?? 0;
    rocketWrapper.style.setProperty("--rocket-progress", (pct / 100).toFixed(4));
    counter.textContent = `${pct.toFixed(0)}%`;
    label.textContent = s.message;

    if (s.progress === null) {
      rocketWrapper.setAttribute("data-indeterminate", "true");
    } else {
      rocketWrapper.removeAttribute("data-indeterminate");
    }

    statusEl.textContent = "";
    statusEl.className = "wf-dialog__status";

    if (s.phase === "error") {
      statusEl.textContent = s.message;
      statusEl.classList.add("wf-dialog__status--error");
      label.textContent = "Flash failed.";
      setLocked(false);
      installBtn.textContent = "Retry";
    } else if (s.phase === "done") {
      statusEl.textContent = s.message;
      statusEl.classList.add("wf-dialog__status--success");
      setLocked(false);
      installBtn.textContent = "Close";
      installBtn.dataset.wfDone = "true";
    }
  };

  const startFlash = async () => {
    let port: SerialPort;
    try {
      port = await navigator.serial.requestPort();
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err);
      if ((err as DOMException)?.name === "NotFoundError") {
        return;
      }
      statusEl.textContent = `Serial error: ${msg}`;
      statusEl.className = "wf-dialog__status wf-dialog__status--error";
      return;
    }

    setLocked(true);
    label.textContent = "Starting...";
    rocketWrapper.setAttribute("data-indeterminate", "true");
    counter.textContent = "0%";
    rocketWrapper.style.setProperty("--rocket-progress", "0");
    statusEl.textContent = "";
    statusEl.className = "wf-dialog__status";

    await flashFirmware(port, manifestUrl, eraseCheckbox.checked, updateUI);
  };

  installBtn.addEventListener("click", () => {
    if (installBtn.dataset.wfDone === "true") {
      backdrop.remove();
      return;
    }
    if (!isFlashing) {
      void startFlash();
    }
  });
};

// ---------------------------------------------------------------------------
// Connect button (replaces esp-web-install-button)
// ---------------------------------------------------------------------------

const ensureConnectButtonStyles = () => {
  if (document.getElementById("webflasher-connect-btn-style")) {
    return;
  }
  const style = document.createElement("style");
  style.id = "webflasher-connect-btn-style";
  style.textContent = `
    .wf-connect-btn {
      position: relative;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 12px;
      padding: 14px 38px;
      border-radius: var(--radius-lg, 24px);
      border: 1px solid rgba(224, 210, 4, 0.3);
      background: linear-gradient(135deg, rgba(0, 221, 0, 0.92) 0%, rgba(224, 210, 4, 0.88) 100%);
      color: #05160d;
      font-size: 0.95rem;
      font-weight: 600;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      box-shadow: 0 18px 38px rgba(0, 221, 0, 0.24), 0 0 0 1px rgba(0, 221, 0, 0.3);
      transition: transform 0.2s ease, box-shadow 0.2s ease, filter 0.2s ease;
      cursor: pointer;
      font-family: inherit;
    }
    .wf-connect-btn:hover,
    .wf-connect-btn:focus-visible {
      transform: translateY(-1px) scale(1.01);
      box-shadow: 0 22px 48px rgba(0, 221, 0, 0.35), 0 0 0 1px rgba(224, 210, 4, 0.45);
    }
    .wf-connect-btn:focus-visible {
      outline: 2px solid rgba(224, 210, 4, 0.8);
      outline-offset: 3px;
    }
    .wf-connect-btn[hidden] {
      display: none;
    }
  `;
  document.head.appendChild(style);
};

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

document.addEventListener("DOMContentLoaded", () => {
  ensureSelectionStyles();
  ensureConnectButtonStyles();

  const releaseContainer = document.querySelector<HTMLElement>("[data-release-container]");
  const versionContainer = document.querySelector<HTMLElement>("[data-version-container]");
  const vendorContainer = document.querySelector<HTMLElement>("[data-vendor-container]");
  const vendorPlaceholder = document.querySelector<HTMLElement>("[data-vendor-empty]");
  const deviceContainer = document.querySelector<HTMLElement>("[data-device-container]");
  const descriptionTarget = document.querySelector<HTMLElement>("[data-device-description]");
  const vendorSection = document.querySelector<HTMLElement>("[data-vendor-section]");
  const deviceSection = document.querySelector<HTMLElement>("[data-device-section]");
  const installSection = document.querySelector<HTMLElement>("[data-install-section]");
  const installBtn = document.querySelector<HTMLButtonElement>("[data-wf-connect-btn]");
  const downloadButton = document.querySelector<HTMLAnchorElement>("[data-download-button]");
  const manifestSummary = document.querySelector<HTMLElement>("[data-manifest-summary]");

  if (
    !releaseContainer ||
    !vendorContainer ||
    !deviceContainer ||
    !installBtn ||
    !downloadButton ||
    !manifestSummary
  ) {
    return;
  }

  const vendorMap = new Map<string, ManifestDevice[]>();
  const vendorDevices = new Map<string, ManifestDevice[]>();
  const summaryFallback = "Selection pending...";
  const state: SelectionState = {
    releaseValue: releaseOptions[0]?.value ?? "Release",
    releaseLabel: releaseOptions[0]?.label ?? "Release",
    selectedVersion: "",
    vendor: "",
    deviceId: "",
    deviceName: ""
  };
  let flasherData: FlasherData | null = null;

  let releaseCards: HTMLButtonElement[] = [];
  let vendorCards: HTMLButtonElement[] = [];
  let deviceCards: HTMLButtonElement[] = [];
  let currentManifestUrl: string | null = null;
  const manifestBaseUrl: URL | null = (() => {
    try {
      const manifestUrl = new URL("manifest.json", window.location.href);
      return new URL(".", manifestUrl);
    } catch {
      try {
        return new URL(".", window.location.href);
      } catch {
        return null;
      }
    }
  })();

  const revokeManifestUrl = () => {
    if (currentManifestUrl) {
      URL.revokeObjectURL(currentManifestUrl);
      currentManifestUrl = null;
    }
  };

  const scrollToSection = (section: HTMLElement | null | undefined) => {
    if (!section) {
      return;
    }
    const header = document.querySelector<HTMLElement>("header");
    const headerHeight = header ? header.getBoundingClientRect().height : 0;
    const sectionTop = section.getBoundingClientRect().top + window.scrollY;
    const offset = Math.max(sectionTop - headerHeight - 16, 0);
    window.scrollTo({ top: offset, behavior: "smooth" });
  };

  const setSummary = () => {
    const parts: string[] = [];
    if (state.releaseLabel) {
      parts.push(state.releaseLabel);
    }
    if (state.releaseValue === "Release" && state.selectedVersion) {
      parts.push(state.selectedVersion);
    }
    if (state.vendor) {
      parts.push(state.vendor);
    }
    if (state.deviceName) {
      parts.push(state.deviceName);
    }
    manifestSummary.textContent = parts.length > 0 ? parts.join(" -> ") : summaryFallback;
  };

  const hideDownloadButton = () => {
    downloadButton.setAttribute("hidden", "");
    downloadButton.removeAttribute("href");
    downloadButton.removeAttribute("download");
  };

  const hideInstallButton = () => {
    revokeManifestUrl();
    installBtn.setAttribute("hidden", "");
    hideDownloadButton();
  };

  const resetDeviceDetails = (message?: string) => {
    if (descriptionTarget) {
      if (message) {
        descriptionTarget.hidden = false;
        descriptionTarget.textContent = message;
      } else {
        descriptionTarget.hidden = true;
        descriptionTarget.textContent = "";
      }
    }
  };

  const highlightCards = (cards: HTMLButtonElement[], selected: string) => {
    cards.forEach((card) => {
      const isSelected = card.dataset.value === selected;
      if (isSelected) {
        card.dataset.selected = "true";
      } else {
        delete card.dataset.selected;
      }
      card.setAttribute("aria-pressed", isSelected.toString());
    });
  };

  const getActiveBins = (): Record<string, string> | null => {
    if (!flasherData) return null;
    if (state.releaseValue === "Beta") {
      return flasherData.beta?.devices ?? null;
    }
    const rel = flasherData.releases.find((r) => r.tag === state.selectedVersion);
    return rel?.devices ?? null;
  };

  const shouldDisplayDevice = (device: ManifestDevice) => {
    const bins = getActiveBins();
    if (bins !== null) {
      return device.id in bins;
    }
    if (state.releaseValue === "Beta") {
      return true;
    }
    return device.display !== false;
  };

  const applySelection = () => {
    if (!state.vendor || !state.deviceId || !state.releaseValue) {
      state.deviceName = "";
      setSummary();
      hideInstallButton();
      resetDeviceDetails();
      return;
    }

    const devices = vendorDevices.get(state.vendor) ?? [];
    const device = devices.find((entry) => entry.id === state.deviceId);
    if (!device) {
      state.deviceName = "";
      setSummary();
      hideInstallButton();
      resetDeviceDetails("Device not found in manifest.");
      return;
    }

    state.deviceName = device.name;
    setSummary();

    if (descriptionTarget) {
      if (device.description) {
        descriptionTarget.hidden = false;
        descriptionTarget.textContent = device.description;
      } else {
        descriptionTarget.hidden = true;
        descriptionTarget.textContent = "";
      }
    }

    const downloadFileName = `Launcher-${device.id}.bin`;
    const bins = getActiveBins();
    let binAbsolutePath: string;

    if (bins && device.id in bins) {
      binAbsolutePath = bins[device.id];
    } else {
      const binRelativePath = `bins${state.releaseValue}/Launcher-${device.id}.bin`;
      binAbsolutePath = binRelativePath;
      if (manifestBaseUrl) {
        try {
          binAbsolutePath = new URL(binRelativePath, manifestBaseUrl).toString();
        } catch (error) {
          console.warn("Unable to resolve firmware from manifest base path.", error);
        }
      }
    }

    downloadButton.href = binAbsolutePath;
    downloadButton.download = downloadFileName;
    downloadButton.removeAttribute("hidden");

    const manifest = {
      name: device.name,
      new_install_prompt_erase: true,
      builds: [
        {
          chipFamily: device.family,
          parts: [
            {
              path: binAbsolutePath,
              offset: 0
            }
          ]
        }
      ]
    };

    revokeManifestUrl();
    currentManifestUrl = URL.createObjectURL(
      new Blob([JSON.stringify(manifest)], { type: "application/json" })
    );
    installBtn.removeAttribute("hidden");
  };

  installBtn.addEventListener("click", () => {
    if (currentManifestUrl) {
      openFlashDialog(currentManifestUrl);
    }
  });

  const renderDeviceCards = (devices: ManifestDevice[]) => {
    deviceContainer.innerHTML = "";
    deviceCards = [];

    if (devices.length === 0) {
      const tile = document.createElement("div");
      tile.className = "tile";
      tile.textContent = "No devices available for this vendor.";
      deviceContainer.appendChild(tile);
      return;
    }

    devices.forEach((device) => {
      const isWip = state.releaseValue === "Beta" && device.display === false;
      const card = createSelectionCard({
        title: device.name,
        subtitle: device.description ?? "Manifest generated automatically.",
        value: device.id
      });
      card.classList.add("selection-card--device");

      const wrapper = document.createElement("div");
      wrapper.className = "selection-card-wrapper";
      if (isWip) {
        card.dataset.wip = "true";
        const badge = document.createElement("span");
        badge.className = "selection-card__badge selection-card__badge--wip";
        badge.textContent = "WIP";
        wrapper.appendChild(badge);
      }
      wrapper.appendChild(card);

      card.addEventListener("click", () => {
        state.deviceId = device.id;
        highlightCards(deviceCards, state.deviceId);
        applySelection();
        scrollToSection(installSection);
      });

      if (device.link) {
        const link = document.createElement("a");
        link.className = "selection-card__store-link";
        link.href = device.link;
        link.target = "_blank";
        link.rel = "noopener noreferrer";
        link.setAttribute("aria-label", `Open ${device.name} device link in a new tab`);
        link.title = `Open ${device.name} link`;
        link.innerHTML = `
          <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
            <path
              fill="currentColor"
              d="M7 18c-1.1 0-1.99.9-1.99 2S5.9 22 7 22s2-.9 2-2-.9-2-2-2zm10
              0c-1.1 0-1.99.9-1.99 2S15.9 22 17 22s2-.9 2-2-.9-2-2-2zm-.73-3.73L18.6
              6H5.21l-.94-2H1v2h2l3.6 7.59-1.35
              2.45C4.52 16.37 5.48 18 7 18h12v-2H7l1.1-2h7.45c.75 0 1.41-.41 1.75-1.03z"
            ></path>
          </svg>
        `;
        wrapper.appendChild(link);
      }

      deviceCards.push(card);
      deviceContainer.appendChild(wrapper);
    });

    highlightCards(deviceCards, state.deviceId);
  };

  const renderVendorCards = () => {
    vendorContainer.innerHTML = "";
    vendorCards = [];
    vendorDevices.clear();

    if (vendorPlaceholder && vendorPlaceholder.isConnected) {
      vendorPlaceholder.remove();
    }

    if (vendorMap.size === 0) {
      const tile = document.createElement("div");
      tile.className = "tile";
      tile.textContent = "Manifest does not list any vendors.";
      vendorContainer.appendChild(tile);
      return;
    }

    const sortedVendors = Array.from(vendorMap.entries()).sort(([a], [b]) =>
      a.localeCompare(b)
    );

    sortedVendors.forEach(([vendorName, devices]) => {
      const visibleDevices = devices.filter((device) => shouldDisplayDevice(device));
      if (visibleDevices.length === 0) {
        return;
      }

      vendorDevices.set(vendorName, visibleDevices);

      const card = createSelectionCard({
        title: vendorName,
        subtitle: `${visibleDevices.length} device(s) available`,
        value: vendorName
      });

      card.addEventListener("click", () => {
        state.vendor = vendorName;
        state.deviceId = "";
        state.deviceName = "";
        highlightCards(vendorCards, state.vendor);
        renderDeviceCards(visibleDevices);
        setSummary();
        hideInstallButton();
        resetDeviceDetails("Select a device to continue.");
        scrollToSection(deviceSection);
      });

      vendorCards.push(card);
      vendorContainer.appendChild(card);
    });

    if (vendorCards.length === 0) {
      const tile = document.createElement("div");
      tile.className = "tile";
      tile.textContent = "No vendors with visible devices.";
      vendorContainer.appendChild(tile);
      return;
    }

    if (state.vendor && !vendorDevices.has(state.vendor)) {
      state.vendor = "";
      state.deviceId = "";
      state.deviceName = "";
    }

    highlightCards(vendorCards, state.vendor);

    if (state.vendor) {
      const devices = vendorDevices.get(state.vendor) ?? [];
      renderDeviceCards(devices);
    }
  };

  const renderVersionDropdown = () => {
    if (!versionContainer) return;

    if (state.releaseValue !== "Release" || !flasherData || flasherData.releases.length === 0) {
      versionContainer.style.display = "none";
      versionContainer.innerHTML = "";
      return;
    }

    versionContainer.innerHTML = "";
    versionContainer.style.display = "flex";
    versionContainer.style.alignItems = "center";
    versionContainer.style.gap = "10px";
    versionContainer.style.flexWrap = "wrap";

    const label = document.createElement("label");
    label.textContent = "Version:";
    label.style.cssText = "font-size:0.9rem;font-weight:600;white-space:nowrap;";

    const select = document.createElement("select");
    select.style.cssText =
      "background:rgba(28,32,36,0.9);color:inherit;border:1px solid rgba(0,221,0,0.3);border-radius:8px;padding:6px 12px;font-size:0.9rem;cursor:pointer;min-width:160px;";

    flasherData.releases.forEach((rel) => {
      const opt = document.createElement("option");
      opt.value = rel.tag;
      opt.textContent = rel.name || rel.tag;
      if (rel.prerelease) opt.textContent += " (pre-release)";
      select.appendChild(opt);
    });

    if (!state.selectedVersion || !flasherData.releases.find((r) => r.tag === state.selectedVersion)) {
      state.selectedVersion = flasherData.releases[0]?.tag ?? "";
    }
    select.value = state.selectedVersion;

    select.addEventListener("change", () => {
      state.selectedVersion = select.value;
      state.deviceId = "";
      state.deviceName = "";
      setSummary();
      renderVendorCards();
      hideInstallButton();
      resetDeviceDetails("Select a vendor to see the devices.");
    });

    versionContainer.appendChild(label);
    versionContainer.appendChild(select);
  };

  const renderReleaseCards = () => {
    releaseContainer.innerHTML = "";
    releaseCards = [];

    releaseOptions.forEach((option) => {
      const card = createSelectionCard({
        title: option.label,
        subtitle: option.description,
        value: option.value
      });

      card.addEventListener("click", () => {
        state.releaseValue = option.value;
        state.releaseLabel = option.label;
        if (option.value === "Release" && flasherData && flasherData.releases.length > 0) {
          if (!state.selectedVersion || !flasherData.releases.find((r) => r.tag === state.selectedVersion)) {
            state.selectedVersion = flasherData.releases[0]?.tag ?? "";
          }
        }
        highlightCards(releaseCards, state.releaseValue);
        renderVersionDropdown();
        setSummary();
        renderVendorCards();
        applySelection();
        scrollToSection(vendorSection);
      });

      releaseCards.push(card);
      releaseContainer.appendChild(card);
    });

    highlightCards(releaseCards, state.releaseValue);
    renderVersionDropdown();
  };

  setSummary();
  hideInstallButton();
  resetDeviceDetails("Select a vendor to see the devices.");

  renderReleaseCards();

  const fetchManifest = fetch("manifest.json")
    .then((r) => {
      if (!r.ok) throw new Error(`manifest ${r.status}`);
      return r.json() as Promise<ManifestData>;
    });

  const fetchFlasher = fetch("flasher.json")
    .then((r) => {
      if (!r.ok) return null;
      return r.json() as Promise<FlasherData>;
    })
    .catch(() => null);

  Promise.all([fetchManifest, fetchFlasher])
    .then(([manifest, fd]) => {
      vendorMap.clear();
      Object.entries(manifest).forEach(([vendorName, devices]) => {
        vendorMap.set(vendorName, devices);
      });
      if (fd) {
        flasherData = fd;
        if (state.releaseValue === "Release" && fd.releases.length > 0) {
          state.selectedVersion = fd.releases[0]?.tag ?? "";
        }
        renderReleaseCards();
      }
      renderVendorCards();
    })
    .catch(() => {
      manifestSummary.textContent = "Manifest load failed.";
      hideInstallButton();
      vendorContainer.innerHTML = "";
      deviceContainer.innerHTML = "";

      const vendorTile = document.createElement("div");
      vendorTile.className = "tile";
      vendorTile.textContent = "Unable to load the manifest.";
      vendorContainer.appendChild(vendorTile);

      const deviceTile = document.createElement("div");
      deviceTile.className = "tile";
      deviceTile.textContent = "Manifest unavailable.";
      deviceContainer.appendChild(deviceTile);
    });

  window.addEventListener("beforeunload", () => {
    revokeManifestUrl();
  });
});
