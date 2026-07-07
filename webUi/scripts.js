function _(e) { return document.getElementById(e); }
function toggleMenu(){_('menu').classList.toggle('open')}
function toggleRow(b){const r=b.closest('tr').nextElementSibling;r.style.display=r.style.display==='none'?'table-row':'none'}
const editableExts = new Set(['txt','ini','conf','c','cpp','h','hpp','js','css','htm','html','ts']);
function isEditable(name) { return editableExts.has(name.split('.').pop().toLowerCase()); }
let editingFile = '';
function editFile(path) {
    editingFile = path;
    _('editor-title').textContent = path;
    _('editor-content').value = 'Loading...';
    _('editor').style.display = 'block';
    const xhr = new XMLHttpRequest();
    xhr.open('GET', '/editfile?name=' + encodeURIComponent(path));
    xhr.onload = () => { _('editor-content').value = xhr.responseText; };
    xhr.onerror = () => { _('editor-content').value = 'Error loading file'; };
    xhr.send();
}
function saveFile() {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/editfile?name=' + encodeURIComponent(editingFile));
    xhr.setRequestHeader('Content-Type', 'text/plain');
    xhr.onload = () => { _('status').innerHTML = xhr.responseText === 'OK' ? 'File saved!' : xhr.responseText; };
    xhr.send(_('editor-content').value);
}

let _nvsData = null;
const _nvsInts = new Set(['u8','i8','u16','i16','u32','i32','u64','i64']);
function _nvsIsCheckbox(f) { return f.t === 'u8' && (f.v === 0 || f.v === 1); }
function _nvsId(ns, k) { return 'nvs__' + ns + '__' + k; }
function loadNvs() {
    _('nvs').style.display = 'block';
    _('nvs-body').innerHTML = 'Loading...';
    const x = new XMLHttpRequest();
    x.open('GET', '/nvs');
    x.onload = () => {
        _nvsData = JSON.parse(x.responseText);
        const inp = 'style="width:100px;background:#303134;color:#0d0;border:1px solid #0d0;padding:2px"';
        const inps = 'style="width:220px;background:#303134;color:#0d0;border:1px solid #0d0;padding:2px"';
        let h = '';
        for (const ns in _nvsData) {
            h += '<h3 style="margin:8px 0 4px;color:#0d0">' + ns + '</h3>';
            _nvsData[ns].forEach(f => {
                const id = _nvsId(ns, f.k);
                h += '<div style="margin:4px 0"><label style="display:inline-block;width:150px;font-size:0.9em">' + f.k + ':</label>';
                if (_nvsIsCheckbox(f))
                    h += '<input type="checkbox" id="' + id + '"' + (f.v ? ' checked' : '') + '>';
                else if (_nvsInts.has(f.t))
                    h += '<input type="number" id="' + id + '" value="' + f.v + '" ' + inp + '>';
                else
                    h += '<input type="text" id="' + id + '" value="' + f.v + '" ' + inps + '>';
                h += ' <small style="color:#888">' + f.t + '</small></div>';
            });
        }
        _('nvs-body').innerHTML = h;
    };
    x.send();
}
function saveNvs() {
    const out = {};
    for (const ns in _nvsData) {
        out[ns] = _nvsData[ns].map(f => {
            const el = document.getElementById(_nvsId(ns, f.k));
            if (!el) return f;
            let v = _nvsIsCheckbox(f) ? (el.checked ? 1 : 0) : _nvsInts.has(f.t) ? parseInt(el.value) : el.value;
            return {k: f.k, t: f.t, v};
        });
    }
    const x = new XMLHttpRequest();
    x.open('POST', '/nvs');
    x.setRequestHeader('Content-Type', 'application/json');
    x.onload = () => { _('status').innerHTML = x.responseText === 'OK' ? 'NVS saved!' : x.responseText; _('nvs').style.display = 'none'; };
    x.send(JSON.stringify(out));
}

function httpRequest(method, url, { async = true, body, headers = {}, onload, onerror } = {}) {
    const xhr = new XMLHttpRequest();
    if (typeof onload === "function") {
        xhr.onload = () => onload(xhr);
    }
    xhr.onerror = () => {
        if (typeof onerror === "function") {
            onerror(xhr);
        } else {
            console.error("Network error or request failure.");
        }
    };
    xhr.open(method, url, async);
    Object.keys(headers).forEach((header) => xhr.setRequestHeader(header, headers[header]));
    if (body !== undefined && body !== null) {
        xhr.send(body);
    } else {
        xhr.send();
    }
    return xhr;
}

function isNullOrEmpty(value) {
    return value === null || value === "";
}

function WifiConfig(target) {
    let wifiSsid;
    let wifiPwd;
    if (target === "usr") {
        wifiSsid = prompt("Username of access Launcher", "admin");
        wifiPwd = prompt("Password", "launcher");
    } else if (target === "ssid") {
        wifiSsid = prompt("SSID of your network", "");
        wifiPwd = prompt("Password of your network", "");
    }
    if (isNullOrEmpty(wifiSsid) || wifiPwd === null) {
        window.alert("Invalid " + target + " or password");
    } else {
        const xhr = httpRequest("GET", "/wifi?" + target + "=" + wifiSsid + "&pwd=" + wifiPwd, { async: false });
        _("status").innerHTML = xhr.responseText;
    }
}

function SDConfig() {
    const miso = prompt("MISO pin", "");
    const mosi = prompt("MOSI pin", "");
    const sck = prompt("SCK pin", "");
    const cs = prompt("CS pin", "");
    if ([miso, mosi, sck, cs].some(isNullOrEmpty)) {
        window.alert("Invalid pins");
    } else {
        const xhr = httpRequest("GET", "/sdpins?miso=" + miso + "&mosi=" + mosi + "&sck=" + sck + "&cs=" + cs, { async: false });
        _("status").innerHTML = xhr.responseText;
    }
}
function startUpdate(fileName) {
    const formdata4 = new FormData();
    formdata4.append("fileName", fileName);
    httpRequest("POST", "/UPDATE", { async: false, body: formdata4 });
}
function callOTA() {
    const formdata = new FormData();
    formdata.append("update", 1);
    httpRequest("POST", "/OTA", { async: false, body: formdata });
    _("detailsheader").innerHTML = "<h3>OTA Update</h3>";
    _("status").innerHTML = "";
    _("details").innerHTML = "";
    _("updetailsheader").innerHTML = "";
    _("updetails").innerHTML = "";
    _("OTAdetails").style.display = 'block';
    _("drop-area").style.display = 'none';
    _("fileInput").click();
}
function readLe32(bytes, offset) {
    return ((bytes[offset]) |
        (bytes[offset + 1] << 8) |
        (bytes[offset + 2] << 16) |
        (bytes[offset + 3] << 24)) >>> 0;
}
function readPartitionLabel(bytes, offset) {
    let label = '';
    for (let i = 0; i < 16; i++) {
        const value = bytes[offset + i];
        if (!value) break;
        label += String.fromCharCode(value);
    }
    return label;
}
function alignUp(value, alignment) {
    return Math.ceil(value / alignment) * alignment;
}
function measureEspImageSize(data, imageOffset) {
    if (imageOffset + 24 > data.length) return 0;
    const magic = data[imageOffset];
    const segmentCount = data[imageOffset + 1];
    const hashAppended = data[imageOffset + 23];
    if (magic !== 0xE9 || segmentCount === 0 || segmentCount > 16) return 0;

    let cursor = imageOffset + 24;
    for (let i = 0; i < segmentCount; i++) {
        if (cursor + 8 > data.length) return 0;
        const segmentSize = readLe32(data, cursor + 4);
        cursor += 8;
        if (segmentSize > data.length || cursor > data.length - segmentSize) return 0;
        cursor += segmentSize;
    }

    let end = alignUp(cursor, 16) + 1;
    if (hashAppended) end += 32;
    end = alignUp(end, 16);
    if (end <= imageOffset || end > data.length) return 0;
    return end - imageOffset;
}
function buildOtaManifest(file, partitions) {
    return {
        sourceName: file.name,
        parts: partitions.map(partition => ({
            kind: partition.kind,
            label: partition.label || '',
            subtype: partition.subtype,
            sourceOffset: partition.offset,
            copySize: partition.size,
            declaredSize: partition.declaredSize || partition.size
        }))
    };
}
function renderOtaActions(file, manifest) {
    const outputDiv = _('analysisOutput');
    const spiffsInfo = _('spiffsInfo');
    outputDiv.innerHTML = '';
    outputDiv.style.display = 'block';
    spiffsInfo.style.display = 'none';

    manifest.parts.forEach((partition) => {
        const meta = document.createElement('div');
        meta.style.fontSize = '0.85em';
        meta.style.opacity = '0.8';
        meta.textContent =
            `${partition.kind.toUpperCase()} offset 0x${partition.sourceOffset.toString(16)} size 0x${partition.copySize.toString(16)}` +
            (partition.label ? ` label ${partition.label}` : '');
        outputDiv.appendChild(meta);
    });

    if (manifest.parts.length === 0) {
        outputDiv.textContent = 'No installable partitions found in this file.';
        return;
    }

    const button = document.createElement('button');
    button.textContent = 'Start Update';
    button.onclick = () => uploadPackage(file, manifest);
    outputDiv.appendChild(button);

    if (manifest.parts.some(partition => partition.kind === 'data')) {
        spiffsInfo.style.display = 'block';
        spiffsInfo.innerHTML =
            '<p><b>Data partition</b>: this can be SPIFFS, LittleFS, or FAT depending on the firmware package.</p>';
    }
}
function analyzeFile() {
    const fileInput = _('fileInput');
    const outputDiv = _('analysisOutput');
    outputDiv.style.display = 'none';
    let pass = true;
    if (fileInput.files.length === 0) {
        window.alert('Please, select a file.');
        return;
    }
    if (fileInput.files[0].name.split('.').pop() !== "bin") {
        window.alert('File is not a .bin');
        return;
    }
    const file = fileInput.files[0];
    const reader = new FileReader();
    reader.onload = function (e) {
        const data = new Uint8Array(reader.result);
        let appOffset = 0;
        let appSize = 0;
        const partitions = [];
        const first_slice = data.slice(0x8000, 0x8000 + 32);
        const byte0 = first_slice[0];
        const byte1 = first_slice[1];
        const byte2 = first_slice[2];
        if (byte0 === 0xaa && byte1 === 0x50 && byte2 === 0x01 && pass === true) {
            pass = false;
            for (let i = 0; i < 0x1000; i += 0x20) {
                const pos = 0x8000 + i;
                if (pos + 32 > data.length) break;
                const slice = data.slice(pos, pos + 32);
                if ((slice[0] === 0xEB && slice[1] === 0xEB) || (slice[0] === 0xFF && slice[1] === 0xFF)) break;
                const type = slice[2];
                const subtype = slice[3];
                const offset = readLe32(slice, 4);
                const declaredSize = readLe32(slice, 8);
                const label = readPartitionLabel(slice, 12);
                if (type === 0x00 && [0x00, 0x10, 0x20].includes(subtype)) {
                    appOffset = offset || 0x10000;
                    const measuredAppSize = measureEspImageSize(data, appOffset);
                    appSize = declaredSize;
                    if (data.length < (appOffset + appSize)) appSize = data.length - appOffset;
                    if (measuredAppSize > 0 && (appSize === 0 || measuredAppSize < appSize)) {
                        appSize = measuredAppSize;
                    }
                }
                if (type === 0x01 && [0x81, 0x82, 0x83].includes(subtype) && offset < data.length) {
                    let size = declaredSize;
                    if (data.length < (offset + size)) size = data.length - offset;
                    if (size > 0) {
                        partitions.push({
                            kind: 'data',
                            subtype,
                            label,
                            offset,
                            size,
                            declaredSize
                        });
                    }
                }
            }
        }
        else if (pass === true) {
            pass = false;
            appOffset = 0x0;
            appSize = measureEspImageSize(data, 0) || data.length;
        }
        if (appSize > 0) {
            partitions.unshift({
                kind: 'app',
                subtype: 0,
                label: 'app',
                offset: appOffset,
                size: appSize,
                declaredSize: appSize
            });
        }
        renderOtaActions(file, buildOtaManifest(file, partitions));
    };
    reader.readAsArrayBuffer(file);
}
function uploadPackage(file, manifest) {
    _("updetails").innerHTML = "Preparing...";
    totalFiles = 1;
    completedFiles = 0;
    const ajax = new XMLHttpRequest();
    ajax.onload = function () {
        if (ajax.status === 200 && ajax.responseText === "OK") {
            const fileProgressDiv = document.createElement("div");
            fileProgressDiv.innerHTML = `<p>Updating...</p><p><progress id="otaprb" value="0" max="100" style="width:100%;"></progress></p>`;
            _("updetails").appendChild(fileProgressDiv);
            const formdata2 = new FormData();
            formdata2.append("file1", file, file.name);
            const ajax2 = new XMLHttpRequest();
            ajax2.open("POST", "/OTAFILE");
            ajax2.upload.addEventListener("progress", function (event) {
                const p = (event.loaded / event.total) * 100;
                _("otaprb").value = Math.round(p);
            }, false);
            ajax2.addEventListener("load", function () { _("status").innerHTML = "Instalation Complete, Restart your device!"; }, false);
            ajax2.addEventListener("error", function () { _("status").innerHTML = "Upload Failed"; }, false);
            ajax2.addEventListener("abort", function () { _("status").innerHTML = "Upload Aborted"; }, false);
            ajax2.send(formdata2);
        }
    };
    ajax.onerror = function () {
        console.error("Initial OTA request failed.");
    };
    const formdata = new FormData();
    formdata.append("command", 0);
    formdata.append("size", file.size);
    formdata.append("manifest", JSON.stringify(manifest));
    ajax.open("POST", "/OTA", true);
    ajax.send(formdata);
}
function logoutButton() {
    httpRequest("GET", "/logout");
    setTimeout(function () { window.open("/logged-out", "_self"); }, 500);
}
function rebootButton() {
    if (confirm("Confirm Restart?!")) {
        httpRequest("GET", "/reboot");
    }
}
function systemInfo() {
    httpRequest("GET", "/systeminfo", {
        onload: (xhr) => {
            if (xhr.status === 200) {
                try {
                    const data = JSON.parse(xhr.responseText);
                    _("firmwareVersion").innerHTML = data.VERSION;
                    _("freeSD").innerHTML = data.SD.free;
                    _("usedSD").innerHTML = data.SD.used;
                    _("totalSD").innerHTML = data.SD.total;
                } catch (error) {
                    console.error("JSON Parsing Error: ", error);
                }
            } else {
                console.error("Request Error: " + xhr.status);
            }
        }
    });
}
function listFilesButton(folders) {
    _("drop-area").style.display = 'block';
    _("actualFolder").value = folders;
    let previousFolder = folders.substring(0, folders.lastIndexOf('/'));
    if (previousFolder === "") { previousFolder = "/"; }
    httpRequest("GET", "/listfiles?folder=" + folders, {
        onload: (xhr) => {
            if (xhr.status === 200) {
                const responseText = xhr.responseText;
                const lines = responseText.split('\n');
                let tableContent = "<table><tr><th>Name</th><th class='sz'>Size</th><th class='ac'></th><th class='mb'></th></tr>\n";
                tableContent += "<tr><td colspan='4'><a onclick=\"listFilesButton('" + previousFolder + "')\" href='javascript:void(0);'>&#8592; ..</a></td></tr>\n";
                let folder = "";
                const foldersArray = [];
                const filesArray = [];
                lines.forEach((line) => {
                    if (line) {
                        const type = line.substring(0, 2);
                        const path = line.substring(3, line.lastIndexOf(':'));
                        const filename = line.substring(3, line.lastIndexOf(':'));
                        const size = line.substring(line.lastIndexOf(':') + 1);
                        if (type === "pa") {
                            if (path !== "" && folder !== "/") folder = path + (path.endsWith("/") ? "" : "/");
                        } else if (type === "Fo") {
                            foldersArray.push({ path: folder + path, name: filename });
                        } else if (type === "Fi") {
                            filesArray.push({ path: folder + path, name: filename, size });
                        }
                    }
                });
                foldersArray.sort((a, b) => a.name.localeCompare(b.name));
                filesArray.sort((a, b) => a.name.localeCompare(b.name));
                foldersArray.forEach((item) => {
                    const ac = "<span style='cursor:pointer;color:#e0d204' onclick=\"listFilesButton('" + item.path + "')\">&#128193;</span>&nbsp" +
                               "<span style='cursor:pointer' onclick=\"renameFile('" + item.path + "', '" + item.name + "')\">&#9999;</span>&nbsp" +
                               "<span style='cursor:pointer' onclick=\"downloadDeleteButton('" + item.path + "', 'delete')\">&#128465;</span>";
                    tableContent += "<tr><td><a onclick=\"listFilesButton('" + item.path + "')\" href='javascript:void(0);'>" + item.name + "</a></td>" +
                                    "<td class='sz'></td><td class='ac'>" + ac + "</td>" +
                                    "<td class='mb'><button onclick='toggleRow(this)'>&#8942;</button></td></tr>\n" +
                                    "<tr class='mrow' style='display:none'><td colspan='4'>" + ac + "</td></tr>\n";
                });
                filesArray.forEach((item) => {
                    const isBin = item.name.split('.').pop().toLowerCase() === "bin";
                    const fname = item.name + (isBin ? "&nbsp<span style='cursor:pointer' onclick=\"startUpdate('" + item.path + "')\">&#128640;</span>" : "");
                    const ac = (isEditable(item.name) ? "<span style='cursor:pointer' onclick=\"editFile('" + item.path + "')\">&#9998;</span>&nbsp" : "") +
                               "<span style='cursor:pointer' onclick=\"downloadDeleteButton('" + item.path + "', 'download')\">&#11015;</span>&nbsp" +
                               "<span style='cursor:pointer' onclick=\"renameFile('" + item.path + "', '" + item.name + "')\">&#9999;</span>&nbsp" +
                               "<span style='cursor:pointer' onclick=\"downloadDeleteButton('" + item.path + "', 'delete')\">&#128465;</span>";
                    tableContent += "<tr><td>" + fname + "</td>" +
                                    "<td class='sz'>" + item.size + "</td><td class='ac'>" + ac + "</td>" +
                                    "<td class='mb'><button onclick='toggleRow(this)'>&#8942;</button></td></tr>\n" +
                                    "<tr class='mrow' style='display:none'><td colspan='4'><span style='color:var(--dim);font-size:.75rem'>" + item.size + "</span>&nbsp;&nbsp;" + ac + "</td></tr>\n";
                });
                tableContent += "</table>";
                _("details").innerHTML = tableContent;
            } else {
                console.error("Request Error: " + xhr.status);
            }
        },
        onerror: () => {
            console.error("Network error while fetching file list.");
        }
    });
    _("detailsheader").innerHTML = "<h3>Files</h3>";
    _("updetailsheader").innerHTML =
        "<input type='file' id='fa' multiple style='display:none'>" +
        "<input type='file' id='fol' webkitdirectory directory multiple style='display:none'>" +
        "<div class='row' style='margin:6px 0'><button onclick=\"_('fa').click()\">&#8679; Files</button>" +
        "<button onclick=\"_('fol').click()\">&#128193; Folder</button>" +
        "<button onclick=\"CreateFolder('" + folders + "')\">+ New Folder</button></div>";
    _("fa").onchange = e => handleFileForm(e.target.files, folders);
    _("fol").onchange = e => handleFileForm(e.target.files, folders);
    _("updetails").innerHTML = "";
    _("OTAdetails").style.display = 'none';
    _("analysisOutput").style.display = 'none';
    _("spiffsInfo").style.display = 'none';
}
function renameFile(filePath, oldName) {
    const actualFolder = _("actualFolder").value;
    const fileName = prompt("Enter the new name: ", oldName);
    if (isNullOrEmpty(fileName)) {
        window.alert("Invalid Name");
    } else {
        const formdata5 = new FormData();
        formdata5.append("filePath", filePath);
        formdata5.append("fileName", fileName);
        const xhr = httpRequest("POST", "/rename", { async: false, body: formdata5 });
        _("status").innerHTML = xhr.responseText;
        listFilesButton(actualFolder);
    }
}
function downloadDeleteButton(filename, action) {
    const urltocall = "/file?name=" + filename + "&action=" + action;
    const actualFolder = _("actualFolder").value;
    const isDelete = action === "delete";
    if (isDelete || action === "create") {
        if (!isDelete || confirm("Do you really want to DELETE the file: " + filename + " ?\n\nThis action can't be undone!")) {
            const xhr = httpRequest("GET", urltocall, { async: false });
            _("status").innerHTML = xhr.responseText;
            listFilesButton(actualFolder);
        }
        return;
    }
    if (action === "download") {
        _("status").innerHTML = "";
        window.open(urltocall, "_blank");
    }
}
function CreateFolder(folders) {
    const folderName = prompt("Folder Name", "");
    if (isNullOrEmpty(folderName)) {
        window.alert("Invalid Folder Name");
    } else {
        downloadDeleteButton(_("actualFolder").value + "/" + folderName, 'create');
    }
}
const addHighlight = (event) => {
    event.preventDefault();
    event.currentTarget.classList.add("highlight");
};
const removeHighlight = (event) => {
    event.preventDefault();
    event.currentTarget.classList.remove("highlight");
};
window.addEventListener("load", () => {
    const dropArea = _("drop-area");
    dropArea.addEventListener("dragenter", addHighlight, false);
    dropArea.addEventListener("dragover", addHighlight, false);
    dropArea.addEventListener("dragleave", removeHighlight, false);
    dropArea.addEventListener("drop", drop, false);
});
let totalFiles = 0;
let completedFiles = 0;
let uploadIdx = 0;
function writeSendForm() {
    _('uplist').innerHTML = '';
    _('upmodal').classList.add('open');
}
async function drop(event) {
    event.preventDefault();
    _("drop-area").classList.remove("highlight");
    const items = event.dataTransfer.items;
    const filesQ = [];
    const promises = [];
    for (let i = 0; i < items.length; i++) {
        const entry = items[i].webkitGetAsEntry();
        if (entry) {
            promises.push(FileTree(entry, "", filesQ));
        }
    }
    await Promise.all(promises);
    handleFileForm(filesQ, _("actualFolder").value);
}
function FileTree(item, path = "", filesQ) {
    return new Promise((resolve) => {
        if (item.isFile) {
            item.file(function (file) {
                const fileWithPath = new File([file], path + file.name, { type: file.type });
                filesQ.push(fileWithPath);
                resolve();
            });
        } else if (item.isDirectory) {
            const dirReader = item.createReader();
            dirReader.readEntries((entries) => {
                const entryPromises = [];
                for (let i = 0; i < entries.length; i++) {
                    entryPromises.push(FileTree(entries[i], path + item.name + "/", filesQ));
                } Promise.all(entryPromises).then(resolve);
            });
        } else {
            resolve();
        }
    });
}
window.addEventListener("load", () => {
    listFilesButton("/");
    systemInfo();
});
let fileQueue = [];
let activeUploads = 0;
const maxConcurrentUploads = 2;
function handleFileForm(files, folder) {
    uploadIdx = 0;
    writeSendForm();
    fileQueue = Array.from(files);
    totalFiles = fileQueue.length;
    completedFiles = 0;
    activeUploads = 0;
    for (let i = 0; i < maxConcurrentUploads; i++) {
        processNextUpload(folder);
    }
}
function processNextUpload(folder) {
    if (fileQueue.length === 0) {
        if (activeUploads === 0) {
            _('upmodal').classList.remove('open');
            _("status").innerHTML = "Upload Complete";
            const actualFolder = _("actualFolder").value;
            listFilesButton(actualFolder);
        }
        return;
    }
    if (activeUploads >= maxConcurrentUploads) return;
    const file = fileQueue.shift();
    activeUploads++;
    uploadFile(folder, file)
        .then(() => {
            activeUploads--;
            completedFiles++;
            _("status").innerHTML = `Uploaded ${completedFiles} of ${totalFiles} files.`;
            processNextUpload(folder);
        })
        .catch((error) => {
            activeUploads--;
            _("status").innerHTML = error || "Upload Failed";
            processNextUpload(folder);
        });
}
function uploadFile(folder, file) {
    return new Promise((resolve, reject) => {
        const id = 'upfill' + (uploadIdx++);
        const row = document.createElement('div');
        row.className = 'upl';
        row.innerHTML = `<div class="upl-fill" id="${id}"></div><div class="upl-lbl">${file.webkitRelativePath || file.name}</div>`;
        _('uplist').appendChild(row);
        const formdata = new FormData();
        formdata.append("folder", folder);
        formdata.append("file", file, file.webkitRelativePath || file.name);
        const ajax = new XMLHttpRequest();
        ajax.upload.addEventListener("progress", (event) => {
            if (event.lengthComputable)
                _(id).style.width = Math.round(event.loaded / event.total * 100) + '%';
        }, false);
        ajax.addEventListener("load", () => {
            if (ajax.status === 200 && ajax.responseText === "OK") {
                resolve();
            } else {
                reject(ajax.responseText || "Upload failed");
            }
        }, false);
        ajax.addEventListener("error", () => reject(), false);
        ajax.addEventListener("abort", () => reject(), false);
        ajax.open("POST", "/");
        ajax.send(formdata);
    });
}
