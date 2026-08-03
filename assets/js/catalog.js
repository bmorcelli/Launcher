import { Transport, ESPLoader } from "esptool-js";
const getPublishedTimestamp = (value) => {
    if (!value) {
        return Number.NEGATIVE_INFINITY;
    }
    const timestamp = Date.parse(value);
    return Number.isNaN(timestamp) ? Number.NEGATIVE_INFINITY : timestamp;
};
const sortVersionsByPublishedDate = (versions) => {
    return [...versions].sort((a, b) => {
        const aTime = getPublishedTimestamp(a.published_at);
        const bTime = getPublishedTimestamp(b.published_at);
        if (aTime !== bTime) {
            return bTime - aTime;
        }
        return 0;
    });
};
const API_URL = "https://api.launcherhub.net/giveMeTheList";
const DEVICES_API_URL = "https://api.launcherhub.net/devices";
const CDN_COVER = "https://m5burner-cdn.m5stack.com/cover/";
const CDN_FIRMWARE = "https://m5burner-cdn.m5stack.com/firmware/";
const PROXY_URL = "https://api.launcherhub.net/proxy";
const PROXY_HWID = "FA:DA:DA:B0:C3:74";
// Reuses the same fid/HWID-whitelisted proxy the firmware itself uses to
// download from the manifest (see Launcher-API's /proxy). Only files that
// belong to this firmware's manifest entries are allowed through.
const proxiedUrl = (url, fid) => `${PROXY_URL}?fid=${encodeURIComponent(fid)}&file=${encodeURIComponent(url)}`;
const proxiedFetch = (url, fid) => fetch(proxiedUrl(url, fid), { headers: { HWID: PROXY_HWID } });
const isSameOrigin = (url) => {
    try {
        return new URL(url, window.location.href).origin === window.location.origin;
    }
    catch {
        return false;
    }
};
const SAMPLE_CARDPUTER_COVER = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='320' height='200'%3E%3Crect width='320' height='200' fill='%2300dd00'/%3E%3Ctext x='160' y='110' font-family='Inter,Arial,sans-serif' font-size='32' fill='%2301110b' text-anchor='middle'%3ENo Image%3C/text%3E%3C/svg%3E";
const SAMPLE_TDISPLAY_COVER = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='320' height='200'%3E%3Crect width='320' height='200' fill='%23202124'/%3E%3Crect x='48' y='40' width='224' height='120' rx='16' fill='%2300dd00' opacity='0.9'/%3E%3Ctext x='160' y='112' font-family='Inter,Arial,sans-serif' font-size='28' fill='%2301110b' text-anchor='middle'%3ET-Display%3C/text%3E%3C/svg%3E";
const SAMPLE_FIRMWARE = [
    {
        name: "Launcher Cardputer",
        author: "Launcher Team",
        category: "cardputer",
        description: "Curated Launcher experience tailored for the M5Stack Cardputer with quick navigation presets.",
        cover: SAMPLE_CARDPUTER_COVER,
        download: 1200,
        versions: [
            { version: "v1.0.0", file: "cardputer_v1.bin", published: true },
            { version: "v0.9.0", file: "cardputer_v0_9.bin", published: true }
        ]
    },
    {
        name: "Launcher LilyGO T-Display",
        author: "Launcher Team",
        category: "tdisplay",
        description: "Optimized visuals for compact screens plus Wi-Fi provisioning shortcuts for workshops.",
        cover: SAMPLE_TDISPLAY_COVER,
        download: 980,
        versions: [{ version: "v1.1.0", file: "tdisplay_v1_1.bin", published: true }]
    }
];
const resolveCover = (cover) => {
    const value = cover ?? "";
    if (value.length === 0) {
        return SAMPLE_CARDPUTER_COVER;
    }
    if (value.startsWith("data:") || value.startsWith("./")) {
        return value;
    }
    if (/^(https?:|\/)/.test(value)) {
        return value;
    }
    return `${CDN_COVER}${value}`;
};
const resolveFirmwareUrl = (file) => {
    const value = file ?? "";
    if (value.length === 0) {
        return "";
    }
    if (/^https?:\/\//i.test(value)) {
        return value;
    }
    return `${CDN_FIRMWARE}${value}`;
};
const DESCRIPTION_COLLAPSED_HEIGHT = 160;
const TRANSPARENT_PIXEL = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw==";
const makeDownloadName = (entry, versionLabel) => {
    const normalize = (value) => value
        .normalize("NFD")
        .replace(/[\u0300-\u036f]/g, "")
        .replace(/[^a-z0-9]+/gi, "-")
        .replace(/^-+|-+$/g, "")
        .toLowerCase();
    const name = normalize(entry.name) || "launcher-firmware";
    const version = normalize(versionLabel);
    return version ? `${name}.${version}.bin` : `${name}.bin`;
};
const formatPublishedDate = (value) => {
    const timestamp = getPublishedTimestamp(value);
    if (!Number.isFinite(timestamp)) {
        return null;
    }
    return new Intl.DateTimeFormat("en-US", {
        year: "numeric",
        month: "short",
        day: "2-digit"
    }).format(new Date(timestamp));
};
// ---------------------------------------------------------------------------
// Flash Device support
// ---------------------------------------------------------------------------
const ESP_CHIP_FAMILY = {
    "32": "ESP32",
    s3: "ESP32-S3",
    c3: "ESP32-C3",
    s2: "ESP32-S2",
    c5: "ESP32-C5",
    c6: "ESP32-C6",
    h2: "ESP32-H2",
    p4: "ESP32-P4",
};
const BOOTLOADER_OFFSET = {
    "32": 0x1000,
    s2: 0x1000,
    s3: 0x0,
    c3: 0x0,
    c6: 0x0,
    h2: 0x0,
    c5: 0x2000,
    p4: 0x2000,
};
const ESP_ENV_NAME = {
    "32": "esp32",
    s3: "esp32s3",
    s2: "esp32s2",
    c3: "esp32c3",
    c5: "esp32c5",
    c6: "esp32c6",
    h2: "esp32h2",
    p4: "esp32p4",
};
const CATALOG_FLASH_STYLE_ID = "catalog-flash-dialog-style";
const ensureCatalogFlashStyles = () => {
    if (document.getElementById(CATALOG_FLASH_STYLE_ID))
        return;
    const style = document.createElement("style");
    style.id = CATALOG_FLASH_STYLE_ID;
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
    .wf-dialog__body {
      padding: 20px 24px;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 20px;
    }
    .wf-dialog__status {
      text-align: center;
      line-height: 1.5;
      font-size: 1rem;
      color: var(--text, #f5f8f2);
      text-shadow: 0 0 18px rgba(0, 221, 0, 0.25);
    }
    .wf-dialog__status--error { color: #ff6f6f; }
    .wf-dialog__status--success { color: var(--primary, #00dd00); }
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
    .wf-btn--warning {
      background: linear-gradient(135deg, rgba(255,140,0,0.92) 0%, rgba(255,80,0,0.88) 100%);
      color: #1a0800;
      box-shadow: 0 8px 24px rgba(255,140,0,0.22);
    }
    .wf-btn--warning:not(:disabled):hover { transform: translateY(-1px); box-shadow: 0 12px 32px rgba(255,140,0,0.32); }
    .wf-dialog__warning {
      font-size: 0.8rem;
      color: rgba(255,140,0,0.85);
      text-align: center;
      line-height: 1.4;
      border: 1px solid rgba(255,140,0,0.25);
      border-radius: 8px;
      padding: 8px 12px;
      background: rgba(255,140,0,0.06);
    }
    .rocket-progress {
      position: relative;
      width: min(360px, 82vw);
      padding: 0px 28px 0px;
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
    .rocket-progress__rocket::before { left: -18px; transform: skewY(-16deg); }
    .rocket-progress__rocket::after { right: -18px; transform: scaleX(-1) skewY(-16deg); }
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
    document.head.appendChild(style);
};
const ROCKET_SCENE_HTML = `
  <div class="rocket-progress__earth"></div>
  <div class="rocket-progress__moon"></div>
  <div class="rocket-progress__rocket">
    <div class="rocket-progress__rocket-body"></div>
    <div class="rocket-progress__trail"></div>
  </div>
`;
const catalogSleep = (ms) => new Promise((r) => setTimeout(r, ms));
const flashFirmwareParts = async (port, parts, expectedChipFamily, fid, eraseFirst, onState) => {
    const fire = (phase, message, progress = null) => onState({ phase, message, progress });
    const transport = new Transport(port);
    const esploader = new ESPLoader({
        transport,
        baudrate: 115200,
        serialOptions: { bufferSize: 8192 },
        enableTracing: false,
    });
    const hardReset = async () => {
        await transport.setRTS(true).catch(() => { });
        await catalogSleep(100);
        await esploader.after().catch(() => { });
        await transport.disconnect().catch(() => { });
    };
    fire("connecting", "Connecting to device...");
    try {
        await esploader.main();
        await esploader.flashId();
    }
    catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        fire("error", `Failed to initialize. Try resetting your device or holding BOOT.\n${msg}`);
        await hardReset();
        return;
    }
    const chipFamily = esploader.chip.CHIP_NAME;
    fire("connecting", `Connected. Found ${chipFamily}.`);
    if (expectedChipFamily && chipFamily !== expectedChipFamily) {
        fire("error", `Expected ${expectedChipFamily} but found ${chipFamily}. Wrong firmware for this device.`);
        await hardReset();
        return;
    }
    const fileArray = [];
    for (let i = 0; i < parts.length; i++) {
        const label = parts.length === 1 ? "firmware" : `part ${i + 1} of ${parts.length}`;
        fire("preparing", `Downloading ${label}...`);
        try {
            const resp = isSameOrigin(parts[i].url)
                ? await fetch(parts[i].url)
                : await proxiedFetch(parts[i].url, fid);
            if (!resp.ok)
                throw new Error(`HTTP ${resp.status}`);
            fileArray.push({ data: new Uint8Array(await resp.arrayBuffer()), address: parts[i].address });
        }
        catch (err) {
            const msg = err instanceof Error ? err.message : String(err);
            fire("error", `Failed to download ${label}: ${msg}`);
            await hardReset();
            return;
        }
    }
    fire("preparing", "Downloads complete. Preparing to flash...", 0);
    if (eraseFirst) {
        fire("erasing", "Erasing device flash...");
        try {
            await esploader.eraseFlash();
        }
        catch (err) {
            const msg = err instanceof Error ? err.message : String(err);
            fire("error", `Erase failed: ${msg}`);
            await hardReset();
            return;
        }
    }
    fire("writing", "Writing: 0%", 0);
    const totalBytes = fileArray.reduce((sum, f) => sum + f.data.byteLength, 0);
    const bytesPerPart = fileArray.map((f) => f.data.byteLength);
    let partBytesWritten = 0;
    try {
        await esploader.writeFlash({
            fileArray,
            flashSize: "keep",
            flashMode: "keep",
            flashFreq: "keep",
            eraseAll: false,
            compress: true,
            reportProgress: (fileIndex, written, total) => {
                const globalWritten = partBytesWritten + written;
                const pct = Math.floor((globalWritten / totalBytes) * 100);
                fire("writing", `Writing: ${pct}%`, pct);
                if (written >= total && fileIndex < bytesPerPart.length - 1) {
                    partBytesWritten += bytesPerPart[fileIndex];
                }
            },
        });
    }
    catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        fire("error", `Write failed: ${msg}`);
        await hardReset();
        return;
    }
    fire("writing", "Writing complete.", 100);
    await hardReset();
    fire("done", "Firmware installed successfully!", 100);
};
const openCatalogFlashDialog = (parts, expectedChipFamily, firmwareName, fid, isApp = false) => {
    ensureCatalogFlashStyles();
    const backdrop = document.createElement("div");
    backdrop.className = "wf-dialog-backdrop";
    backdrop.setAttribute("role", "dialog");
    backdrop.setAttribute("aria-modal", "true");
    backdrop.setAttribute("aria-label", `Flash ${firmwareName}`);
    const warningHtml = isApp
        ? `<p class="wf-dialog__warning">⚠ This firmware is best installed via Launcher. This installation may fail.</p>`
        : "";
    const dialog = document.createElement("div");
    dialog.className = "wf-dialog";
    dialog.innerHTML = `
    <div class="wf-dialog__body">
      <div class="rocket-progress" data-indeterminate="true">
        <div class="rocket-progress__scene">${ROCKET_SCENE_HTML}</div>
        <div class="rocket-progress__counter">0%</div>
      </div>
      ${warningHtml}
      <p class="wf-dialog__status" data-wf-status>Click Flash to connect and deploy.</p>
    </div>
    <div class="wf-dialog__footer">
      <label class="wf-dialog__erase-row">
        <input type="checkbox" data-wf-erase />
        Erase before flashing
      </label>
      <button class="wf-btn wf-btn--ghost" data-wf-close>Cancel</button>
      <button class="wf-btn ${isApp ? "wf-btn--warning" : "wf-btn--primary"}" data-wf-install>Flash</button>
    </div>
  `;
    backdrop.appendChild(dialog);
    document.body.appendChild(backdrop);
    const rocketWrapper = dialog.querySelector(".rocket-progress");
    const counter = dialog.querySelector(".rocket-progress__counter");
    const statusEl = dialog.querySelector("[data-wf-status]");
    const eraseCheckbox = dialog.querySelector("[data-wf-erase]");
    const installBtn = dialog.querySelector("[data-wf-install]");
    const closeBtns = dialog.querySelectorAll("[data-wf-close]");
    let isFlashing = false;
    const close = () => { if (!isFlashing)
        backdrop.remove(); };
    closeBtns.forEach((btn) => btn.addEventListener("click", close));
    backdrop.addEventListener("click", (e) => { if (e.target === backdrop)
        close(); });
    const setLocked = (locked) => {
        isFlashing = locked;
        installBtn.disabled = locked;
        eraseCheckbox.disabled = locked;
        closeBtns.forEach((btn) => (btn.disabled = locked));
    };
    const updateUI = (s) => {
        const pct = s.progress ?? 0;
        rocketWrapper.style.setProperty("--rocket-progress", (pct / 100).toFixed(4));
        counter.textContent = `${pct.toFixed(0)}%`;
        rocketWrapper.toggleAttribute("data-indeterminate", s.progress === null);
        statusEl.textContent = s.message;
        statusEl.className = "wf-dialog__status";
        if (s.phase === "error") {
            statusEl.classList.add("wf-dialog__status--error");
            setLocked(false);
            installBtn.textContent = "Retry";
        }
        else if (s.phase === "done") {
            statusEl.classList.add("wf-dialog__status--success");
            setLocked(false);
            installBtn.textContent = "Close";
            installBtn.dataset.wfDone = "true";
        }
    };
    const startFlash = async () => {
        let port;
        try {
            port = await navigator.serial.requestPort();
        }
        catch (err) {
            if (err?.name === "NotFoundError")
                return;
            const msg = err instanceof Error ? err.message : String(err);
            statusEl.textContent = `Serial error: ${msg}`;
            statusEl.className = "wf-dialog__status wf-dialog__status--error";
            return;
        }
        setLocked(true);
        rocketWrapper.setAttribute("data-indeterminate", "true");
        counter.textContent = "0%";
        rocketWrapper.style.setProperty("--rocket-progress", "0");
        statusEl.textContent = "Starting...";
        statusEl.className = "wf-dialog__status";
        await flashFirmwareParts(port, parts, expectedChipFamily, fid, eraseCheckbox.checked, updateUI);
    };
    installBtn.addEventListener("click", () => {
        if (installBtn.dataset.wfDone === "true") {
            backdrop.remove();
            return;
        }
        if (!isFlashing)
            void startFlash();
    });
};
document.addEventListener("DOMContentLoaded", () => {
    const list = document.querySelector("[data-catalog-list]");
    const emptyState = document.querySelector("[data-catalog-empty]");
    const searchInput = document.querySelector("[data-catalog-search]");
    const categorySelect = document.querySelector("[data-catalog-category]");
    const orderSelect = document.querySelector("[data-catalog-order]");
    const counter = document.querySelector("[data-catalog-count]");
    const status = document.querySelector("[data-catalog-status]");
    let currentOrder = "downloads";
    if (!list || !emptyState || !searchInput || !categorySelect || !counter || !status) {
        return;
    }
    if (!orderSelect) {
        return;
    }
    currentOrder =
        orderSelect.value === "name"
            ? "name"
            : orderSelect.value === "published_at"
                ? "published_at"
                : "downloads";
    const offlineMode = new URLSearchParams(window.location.search).has("offline");
    let firmware = [];
    let filtered = [];
    let totalFirmwareCount = 0;
    let initialCategory = "cardputer";
    const pendingImages = new Set();
    let imageObserver = null;
    const loadImage = (image) => {
        const src = image.dataset.src;
        if (!src) {
            return;
        }
        image.src = src;
        image.removeAttribute("data-src");
        pendingImages.delete(image);
    };
    const ensureImageObserver = () => {
        if (imageObserver || !("IntersectionObserver" in window)) {
            return;
        }
        imageObserver = new IntersectionObserver((entries) => {
            entries.forEach((entry) => {
                if (entry.isIntersecting) {
                    const target = entry.target;
                    loadImage(target);
                    imageObserver?.unobserve(target);
                }
            });
        }, { rootMargin: "200px 0px" });
    };
    const registerLazyImage = (image, source) => {
        image.dataset.src = source;
        image.src = TRANSPARENT_PIXEL;
        pendingImages.add(image);
        if ("IntersectionObserver" in window) {
            ensureImageObserver();
            imageObserver?.observe(image);
        }
        else {
            loadImage(image);
        }
    };
    const flushPendingImages = () => {
        if (!("IntersectionObserver" in window)) {
            pendingImages.forEach((image) => loadImage(image));
            return;
        }
        pendingImages.forEach((image) => {
            if (imageObserver) {
                imageObserver.observe(image);
            }
        });
    };
    const renderCounter = () => {
        counter.textContent = `${filtered.length} of ${totalFirmwareCount} firmwares`;
    };
    const getLatestVersionTimestamp = (entry) => {
        return (entry.versions ?? []).reduce((latest, version) => {
            return Math.max(latest, getPublishedTimestamp(version.published_at));
        }, Number.NEGATIVE_INFINITY);
    };
    const getLatestVersion = (entry) => {
        return (entry.versions ?? []).reduce((latest, version) => {
            if (!latest) {
                return version;
            }
            return getPublishedTimestamp(version.published_at) > getPublishedTimestamp(latest.published_at)
                ? version
                : latest;
        }, null);
    };
    const buildCard = (entry) => {
        const article = document.createElement("article");
        article.className = "card reveal-on-scroll";
        article.classList.add("is-visible");
        article.dataset.filterValue = [entry.name, entry.author, entry.category, entry.description]
            .filter(Boolean)
            .join(" ");
        article.setAttribute("data-filter-item", "");
        article.style.display = "grid";
        article.style.gridTemplateColumns = "minmax(180px, 250px) 1fr";
        article.style.alignItems = "center";
        article.style.gap = "24px";
        const figure = document.createElement("figure");
        figure.style.margin = "0";
        figure.style.display = "flex";
        figure.style.alignItems = "center";
        figure.style.justifyContent = "center";
        figure.style.height = "100%";
        const image = document.createElement("img");
        image.decoding = "async";
        image.loading = "lazy";
        image.alt = `${entry.name} cover`;
        image.style.maxWidth = "250px";
        image.style.width = "100%";
        image.style.height = "auto";
        image.style.maxHeight = "260px";
        image.style.objectFit = "contain";
        image.style.borderRadius = "12px";
        image.style.border = "1px solid rgba(0, 221, 0, 0.2)";
        registerLazyImage(image, resolveCover(entry.cover));
        figure.append(image);
        const body = document.createElement("div");
        body.style.display = "flex";
        body.style.flexDirection = "column";
        body.style.gap = "12px";
        body.style.height = "100%";
        body.style.justifyContent = "flex-start";
        const title = document.createElement("h3");
        title.className = "card__title";
        title.style.margin = "0";
        title.style.textAlign = "center";
        title.style.display = "flex";
        title.style.alignItems = "center";
        title.style.justifyContent = "center";
        title.style.gap = "8px";
        const titleText = document.createElement("span");
        titleText.textContent = entry.author ? `${entry.name} (${entry.author})` : entry.name;
        title.append(titleText);
        if (entry.github) {
            const githubLink = document.createElement("a");
            githubLink.href = entry.github;
            githubLink.target = "_blank";
            githubLink.rel = "noopener";
            githubLink.title = "Open GitHub repository";
            githubLink.setAttribute("aria-label", "Open GitHub repository");
            githubLink.style.display = "inline-flex";
            githubLink.style.alignItems = "center";
            githubLink.style.color = "var(--accent, #00dd00)";
            githubLink.style.textDecoration = "none";
            githubLink.style.flex = "0 0 auto";
            const githubIcon = document.createElementNS("http://www.w3.org/2000/svg", "svg");
            githubIcon.setAttribute("viewBox", "0 0 16 16");
            githubIcon.setAttribute("width", "18");
            githubIcon.setAttribute("height", "18");
            githubIcon.setAttribute("aria-hidden", "true");
            githubIcon.style.fill = "currentColor";
            const githubPath = document.createElementNS("http://www.w3.org/2000/svg", "path");
            githubPath.setAttribute("d", "M8 0C3.58 0 0 3.58 0 8a8 8 0 0 0 5.47 7.59c.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.5-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82a7.7 7.7 0 0 1 4 0c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8 8 0 0 0 16 8c0-4.42-3.58-8-8-8Z");
            githubIcon.append(githubPath);
            githubLink.append(githubIcon);
            title.append(githubLink);
        }
        const latestVersion = getLatestVersion(entry);
        const publishedDateLabel = formatPublishedDate(latestVersion?.published_at);
        const metaRow = document.createElement("div");
        metaRow.style.display = "flex";
        metaRow.style.flexWrap = "wrap";
        metaRow.style.alignItems = "center";
        metaRow.style.justifyContent = "center";
        metaRow.style.gap = "10px 14px";
        metaRow.style.fontSize = "0.9rem";
        metaRow.style.color = "rgba(245, 248, 242, 0.78)";
        if ((entry.download ?? 0) > 0) {
            const downloadsMeta = document.createElement("span");
            downloadsMeta.textContent = `${entry.download} downloads`;
            metaRow.append(downloadsMeta);
        }
        if (publishedDateLabel) {
            const publishedMeta = document.createElement("span");
            publishedMeta.textContent = `Published ${publishedDateLabel}`;
            metaRow.append(publishedMeta);
        }
        const descriptionWrapper = document.createElement("div");
        descriptionWrapper.style.position = "relative";
        descriptionWrapper.style.maxHeight = `${DESCRIPTION_COLLAPSED_HEIGHT}px`;
        descriptionWrapper.style.overflow = "hidden";
        const description = document.createElement("p");
        description.className = "card__description";
        description.textContent = entry.description;
        description.style.margin = "0";
        description.style.textAlign = "justify";
        description.style.flex = "1 1 auto";
        description.style.overflowWrap = "anywhere";
        descriptionWrapper.append(description);
        const readMoreButton = document.createElement("button");
        readMoreButton.type = "button";
        readMoreButton.className = "button button--ghost";
        readMoreButton.textContent = "Read more";
        readMoreButton.style.alignSelf = "center";
        readMoreButton.style.display = "none";
        readMoreButton.style.margin = "0 auto";
        readMoreButton.style.marginTop = "4px";
        const controls = document.createElement("div");
        controls.className = "card__actions";
        controls.style.justifyContent = "center";
        controls.style.flexWrap = "wrap";
        controls.style.marginTop = "auto";
        const select = document.createElement("select");
        select.className = "catalog__search";
        select.style.background = "rgba(0, 221, 0, 0.12)";
        select.style.borderRadius = "12px";
        select.style.fontSize = "0.95rem";
        select.style.border = "1px solid rgba(0, 221, 0, 0.25)";
        select.style.color = "var(--text, #f5f8f2)";
        select.style.minWidth = "220px";
        entry.versions.forEach((version, index) => {
            if (!version.file) {
                return;
            }
            const option = document.createElement("option");
            option.value = resolveFirmwareUrl(version.file);
            option.textContent = version.version || `Build ${index + 1}`;
            option.dataset.published = String(version.published ?? false);
            option.dataset.versionLabel = version.version || `build-${index + 1}`;
            option.dataset.format = version.install?.format ?? "";
            select.append(option);
        });
        if (select.options.length === 0) {
            const option = document.createElement("option");
            option.value = "";
            option.textContent = "No downloads available";
            option.dataset.versionLabel = "unavailable";
            select.append(option);
            select.disabled = true;
        }
        const downloadButton = document.createElement("button");
        downloadButton.type = "button";
        downloadButton.className = "button button--ghost";
        downloadButton.textContent = "Download";
        downloadButton.disabled = select.value.length === 0 || !entry.fid;
        const getSelectedFilename = () => {
            const versionLabel = select.selectedOptions[0]?.dataset.versionLabel ?? "";
            return makeDownloadName(entry, versionLabel);
        };
        const flashButton = document.createElement("button");
        flashButton.type = "button";
        flashButton.className = "button button--ghost";
        flashButton.textContent = "Flash";
        flashButton.style.display = "none";
        const appFlashButton = document.createElement("button");
        appFlashButton.type = "button";
        appFlashButton.className = "button button--warning";
        appFlashButton.style.display = "none";
        appFlashButton.textContent = "Flash";
        appFlashButton.title = "This firmware is best installed via Launcher. This installation may fail.";
        const isWebSerialAvailable = "serial" in navigator;
        const updateFlashButtonVisibility = () => {
            const format = select.selectedOptions[0]?.dataset.format ?? "";
            const showMerged = format === "merged" && !!entry.esp && !!entry.fid && isWebSerialAvailable;
            const showApp = format === "app" && !!entry.esp && !!entry.fid && isWebSerialAvailable;
            flashButton.style.display = showMerged ? "" : "none";
            appFlashButton.style.display = showApp ? "" : "none";
        };
        flashButton.addEventListener("click", () => {
            const url = select.value;
            if (!url || !entry.esp || !entry.fid)
                return;
            const chipFamily = ESP_CHIP_FAMILY[entry.esp] ?? "";
            openCatalogFlashDialog([{ url, address: 0x0 }], chipFamily, getSelectedFilename().replace(/\.bin$/, ""), entry.fid);
        });
        appFlashButton.addEventListener("click", () => {
            const url = select.value;
            if (!url || !entry.esp || !entry.fid)
                return;
            const chipFamily = ESP_CHIP_FAMILY[entry.esp] ?? "";
            const envName = ESP_ENV_NAME[entry.esp] ?? "";
            const bootloaderOffset = BOOTLOADER_OFFSET[entry.esp] ?? 0x1000;
            const baseUrl = new URL("assets/", window.location.href).href;
            const parts = [
                { url: `${baseUrl}${envName}/bootloader.bin`, address: bootloaderOffset },
                { url: `${baseUrl}${envName}/partitions.bin`, address: 0x8000 },
                { url, address: 0x10000 },
            ];
            openCatalogFlashDialog(parts, chipFamily, getSelectedFilename().replace(/\.bin$/, ""), entry.fid, true);
        });
        downloadButton.addEventListener("click", async () => {
            const url = select.value;
            if (!url || !entry.fid)
                return;
            const filename = getSelectedFilename();
            const originalText = downloadButton.textContent;
            downloadButton.disabled = true;
            downloadButton.textContent = "Downloading…";
            try {
                const response = await proxiedFetch(url, entry.fid);
                if (!response.ok)
                    throw new Error(`HTTP ${response.status}`);
                const blob = await response.blob();
                const blobUrl = URL.createObjectURL(blob);
                const link = document.createElement("a");
                link.href = blobUrl;
                link.download = filename;
                link.click();
                URL.revokeObjectURL(blobUrl);
            }
            catch {
                window.open(url, "_blank");
            }
            finally {
                downloadButton.textContent = originalText;
                downloadButton.disabled = select.value.length === 0;
            }
        });
        updateFlashButtonVisibility();
        select.addEventListener("change", () => {
            downloadButton.disabled = select.value.length === 0 || !entry.fid;
            updateFlashButtonVisibility();
        });
        controls.append(select, downloadButton, flashButton, appFlashButton);
        let expanded = false;
        const updateReadMoreState = () => {
            const needsToggle = description.scrollHeight > DESCRIPTION_COLLAPSED_HEIGHT + 10;
            if (!needsToggle) {
                descriptionWrapper.style.maxHeight = "";
                descriptionWrapper.style.overflow = "visible";
                readMoreButton.style.display = "none";
                expanded = false;
                return;
            }
            readMoreButton.style.display = "";
            if (expanded) {
                descriptionWrapper.style.maxHeight = "";
                descriptionWrapper.style.overflow = "visible";
                readMoreButton.textContent = "Read less";
            }
            else {
                descriptionWrapper.style.maxHeight = `${DESCRIPTION_COLLAPSED_HEIGHT}px`;
                descriptionWrapper.style.overflow = "hidden";
                readMoreButton.textContent = "Read more";
            }
        };
        readMoreButton.addEventListener("click", () => {
            expanded = !expanded;
            updateReadMoreState();
        });
        setTimeout(updateReadMoreState, 0);
        body.append(title, metaRow, descriptionWrapper, readMoreButton, controls);
        article.append(figure, body);
        return article;
    };
    const renderList = () => {
        list.innerHTML = "";
        if (imageObserver) {
            imageObserver.disconnect();
        }
        pendingImages.clear();
        filtered.forEach((entry) => {
            list.append(buildCard(entry));
        });
        flushPendingImages();
        emptyState.toggleAttribute("hidden", filtered.length !== 0);
        renderCounter();
    };
    const sortFiltered = () => {
        if (currentOrder === "name") {
            filtered.sort((a, b) => a.name.localeCompare(b.name));
            return;
        }
        if (currentOrder === "published_at") {
            filtered.sort((a, b) => {
                const aTime = getLatestVersionTimestamp(a);
                const bTime = getLatestVersionTimestamp(b);
                if (aTime !== bTime) {
                    return bTime - aTime;
                }
                return a.name.localeCompare(b.name);
            });
            return;
        }
        filtered.sort((a, b) => (b.download ?? 0) - (a.download ?? 0));
    };
    const applyFilters = () => {
        const term = searchInput.value.trim().toLowerCase();
        const categoryValue = categorySelect.value;
        filtered = firmware.filter((entry) => {
            const matchesCategory = categoryValue === "all" || entry.category === categoryValue;
            const haystack = `${entry.name} ${entry.author} ${entry.category} ${entry.description}`.toLowerCase();
            const matchesTerm = haystack.includes(term);
            return matchesCategory && matchesTerm;
        });
        sortFiltered();
        renderList();
    };
    const populateCategories = (inputCategories) => {
        const categories = new Set(["all"]);
        (inputCategories ?? []).forEach((category) => {
            if (category) {
                categories.add(category);
            }
        });
        if (!inputCategories) {
            firmware.forEach((entry) => {
                if (entry.category) {
                    categories.add(entry.category);
                }
            });
        }
        const orderedCategories = [
            "all",
            ...Array.from(categories)
                .filter((category) => category !== "all")
                .sort((a, b) => a.localeCompare(b))
        ];
        categorySelect.innerHTML = "";
        orderedCategories.forEach((category) => {
            const option = document.createElement("option");
            option.value = category;
            option.textContent = category === "all" ? "All categories" : category;
            categorySelect.append(option);
        });
        if (initialCategory && orderedCategories.includes(initialCategory)) {
            categorySelect.value = initialCategory;
        }
        else {
            categorySelect.value = "all";
        }
    };
    const hydrate = (entries) => {
        const sortedEntries = entries.map((item) => ({
            ...item,
            versions: sortVersionsByPublishedDate(item.versions ?? [])
        }));
        firmware = sortedEntries.filter((item) => Array.isArray(item.versions) && item.versions.some((version) => Boolean(version.file)));
        totalFirmwareCount = firmware.length;
        filtered = [...firmware];
        if (categorySelect.options.length === 0) {
            populateCategories();
        }
        else {
            const availableCategories = firmware
                .map((entry) => entry.category)
                .filter((category) => Boolean(category));
            populateCategories(availableCategories);
        }
        applyFilters();
    };
    const fetchCategories = async () => {
        const response = await fetch(DEVICES_API_URL);
        if (!response.ok) {
            throw new Error(`Device request failed with status ${response.status}`);
        }
        const payload = (await response.json());
        const categories = payload
            .map((item) => item.category?.trim())
            .filter((category) => Boolean(category));
        populateCategories(categories);
    };
    const fetchData = async () => {
        try {
            status.textContent = "Loading catalog...";
            const categoryPromise = fetchCategories().catch((error) => {
                console.error(error);
            });
            const response = await fetch(API_URL);
            if (!response.ok) {
                throw new Error(`Request failed with status ${response.status}`);
            }
            const payload = (await response.json());
            await categoryPromise;
            hydrate(payload);
            status.textContent = "";
        }
        catch (error) {
            console.error(error);
            status.textContent = "Unable to load live firmware data. Showing sample entries.";
            hydrate(SAMPLE_FIRMWARE);
        }
    };
    searchInput.addEventListener("input", () => {
        applyFilters();
    });
    categorySelect.addEventListener("change", () => {
        applyFilters();
    });
    orderSelect.addEventListener("change", () => {
        const value = orderSelect.value === "name"
            ? "name"
            : orderSelect.value === "published_at"
                ? "published_at"
                : "downloads";
        currentOrder = value;
        sortFiltered();
        renderList();
    });
    if (offlineMode) {
        status.textContent = "Offline preview mode active. Live data not requested.";
        hydrate(SAMPLE_FIRMWARE);
        return;
    }
    fetchData();
});
