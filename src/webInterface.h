#ifndef __WEBINTERFACE_H
#define __WEBINTERFACE_H

#include <SD_MMC.h>
#include <SD.h>

#include <SPI.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <webFiles.h>


// function defaults
String humanReadableSize(uint64_t bytes);
String listFiles(String folder);
String processor(const String& var);
String readLineFromFile(File myFile);

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void notFound(AsyncWebServerRequest *request);

void configureWebServer();
void startWebUi(String ssid, int encryptation, bool mode_ap = false);


void webUIMyNet();

void loopOptionsWebUi() ;

#if 0
#ifndef PART_04MB
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="UTF-8">

<!-- CSS sample thanks to @im.nix (Discord) -->
<style>
.rocket {
background-image: url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAUCAYAAACNiR0NAAAAAXNSR0IArs4c6QAAA3VJREFUOE+tlG1Mk1cUx//3eW9LoZShhMVNNzWM6tD48sG9wD6AOonMN9ToCG4Gs82gwCaaSNbxBV2Mkm24DGPmS6biCxh1C6JREbvInFtYmMRNbFGGDdppR7un7fPc5y5tAhlzVsx2v91z7/2dc+75n0PwPy/yX3nOlq3pzrzKvkHOEwO39xxwfHmmoVNnKsA4ECbC4hewMX9N+qLMOXdGDGSMkWU7117qFryzfKYglBQzVEohchYk3CNYOHpGQdXc9SdGBHQzt7JiR9XvEYWaWDAIQ2a3BxLZGGY3IwADis9AxeT540unF3c/Evhz/3dpcqrd4MHbV2xd1xWxW5Ax8QXsz67iCCHsrbbqrFZ3R5tuk63C3RC6326MsYaA0SjO/taxb+/5xiXee3eREOYwK3smXO52GAbAeikaKptlByGRwQJMrVt2UbOQV5Qg/fr79w7nDwH9zG/f3FDja/N2IJJqQYBqmJc9B64rbehnd5BssQLXVZxYv1OZQCaEB4GOLQXUalK4y+sahgIjjDHlUGvjtdoLu8axUTI0HXg1N/cTV9ePpffpADT+T8giDyEMjLc+g/K8ctMojDUqWsoP93k8BQ4+/djB1XWLh2TDGJMu3fhh/3lPa2FGztST127+2nPmiutdHx/mNJlCUggCwQEIHAEf5iAFJKgPgjDbJKRQBRffOTKsDrFNNEoV6lMe9NOln5b2aWkSeFEGZRHoXASUarCIJoTUMAyDg00Xkdpv4HRF00NFHWZgjPGvH19z43qod6wgCCA8QYjTIUsmCKoOnnDQOSDxDw7tRUf/VSEPGatvfl54uefqm123u/KJ1QJVNEGAAHOEgvqDsJlTkP3czLPbppfNJoQY/2zdYcCs0teYNSPJSMsck+M3HnBun/eCZkqOyUb2B5CelALH5Gkfntv7zUdNG3ZVZpqf/TguMHo4pXYu0xMYOlc3k5yTq5Z6/PcPSbyI0byEzElZE09ddf3Ce8O49UFz/JRLtpQk1W+s90eh0z5boAoDWnX7plM184+tfclzq3fGT2XHa1+se4Np9kQkh0R8u2r34/9widMpHXE6Y52Q9/6CSS3bmjr/ntKU7cuZwEnYULTSVmjPjTl/bMrx5uPLNcXM8fTzX31RVLXyUfdGNG0GH+84vaekbHZxfTynTwSM6pQQQuMB/wLJw2AkLJLNsAAAAABJRU5ErkJggg==');
background-repeat: no-repeat;
background-size: 20px;
background-position: center;
width: 20px;
height: 16px;
display: inline-block;
}
.gg-rename {
box-sizing: border-box;
position: relative;
display: inline-block;
width: 20px;
height: 16px;
transform: scale(var(--ggs,1));
background:
linear-gradient(
to left,currentColor 22px,
transparent 0)
no-repeat 6px center/2px 22px
}

.gg-rename::after,
.gg-rename::before {
content: "";
display: block;
box-sizing: border-box;
position: absolute;
width: 6px;
height: 12px;
border: 2px solid;
top: 2px
}

.gg-rename::before {
border-right: 0;
border-top-left-radius: 3px;
border-bottom-left-radius: 3px
}

.gg-rename::after {
width: 10px;
border-left: 0;
border-top-right-radius: 3px;
border-bottom-right-radius: 3px;
right: 0
} 
.gg-folder {
cursor: pointer;
transform: scale(var(--ggs,1))
}
.gg-folder,
.gg-folder::after {
box-sizing: border-box;
position: relative;
display: inline-block;
width: 22px;
height: 16px;
border: 2px solid;
border-radius: 3px
}
.gg-folder::after {
content: "";
position: absolute;
width: 10px;
height: 4px;
border-bottom: 0;
border-top-left-radius: 2px;
border-top-right-radius: 4px;
border-bottom-left-radius: 0;
border-bottom-right-radius: 0;
top: -5px
}
.gg-trash {
box-sizing: border-box;
position: relative;
display: inline-block;
transform: scale(var(--ggs,1));
width: 10px;
height: 12px;
border: 2px solid transparent;
box-shadow:
0 0 0 2px,
inset -2px 0 0,
inset 2px 0 0;
border-bottom-left-radius: 1px;
border-bottom-right-radius: 1px;
margin-top: 4px;
margin-bottom: 2px;
cursor: pointer;
}
.gg-trash::after,
.gg-trash::before {
content: "";
display: block;
box-sizing: border-box;
position: absolute
}
.gg-trash::after {
background: currentColor;
border-radius: 3px;
width: 16px;
height: 2px;
top: -4px;
left: -5px
}
.gg-trash::before {
width: 10px;
height: 4px;
border: 2px solid;
border-bottom: transparent;
border-top-left-radius: 2px;
border-top-right-radius: 2px;
top: -7px;
left: -2px
}
.gg-arrow-down-r {
box-sizing: border-box;
position: relative;
display: inline-block;
width: 22px;
height: 22px;
border: 2px solid;
transform: scale(var(--ggs,1));
cursor: pointer;
border-radius: 4px
}
.gg-arrow-down-r::after,
.gg-arrow-down-r::before {
content: "";
display: block;
box-sizing: border-box;
position: absolute;
bottom: 4px
}
.gg-arrow-down-r::after {
width: 6px;
height: 6px;
border-bottom: 2px solid;
border-left: 2px solid;
transform: rotate(-45deg);
left: 6px
}
.gg-arrow-down-r::before {
width: 2px;
height: 10px;
left: 8px;
background: currentColor
}
body {
font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
margin: 0;
padding: 5px;
color: #00dd00;
background-color: #202124;
}

.container {
max-width: 800px;
margin: 5px auto;
padding: 0 5px;
}

h3 {
margin: 0;
padding: 10px 0;
border-bottom: 1px solid #00cc00;
}
 
table {
width: 100%%;
border-collapse: collapse;
border-bottom: 1px solid #00cc00;
}
 
th, td {
padding: 5px;
border-bottom: 1px solid #00cc00;
}
 
th {
text-align: left;
}
 
a {
color: #e0d204;
text-decoration: none;
}
 
a:hover {
text-decoration: underline;
cursor: pointer;
}
 
button {
background-color: #303134;
color: #00dd00;
border: 2px solid;
padding: 4px 8px;
border-radius: 4px;
border-color: #00dd00;
cursor: pointer;
margin: 5px;
}
 
button:hover {
background-color: #292a2c;
}
 
#detailsheader, #updetailsheader {
display: flex;
justify-content: space-between;
}
 
@media (max-width: 768px) {
body {
font-size: 14px;
}
 
table {
font-size: 12px;
}
 
th, td {
padding: 5px;
}
 
button {
font-size: 12px;
padding: 6px 12px;
}
}
th:first-child, td:first-child {
width: 65%%;
}
th:last-child, td:last-child {
width: 100px;
text-align: center;
}
.float-element {
position: absolute;
top: 10px; /* Ajuste conforme necessário */
right: 10px; /* Ajuste conforme necessário */
font-size: 16px;
}

.drop-area {
border: 2px dashed #00dd00;
padding: 100px;
margin-top: 50px;
display: none;
}

.highlight {
background-color: #303134;
color: #ad007c65;
}

</style>
</head>
<body>
<div class="container">
<div class="float-element"><a onclick="logoutButton()" href='javascript:void(0);'>[X]</a></div>
<h1 align="center">-= Launcher =-</h1>
<p>Firmware version: %FIRMWARE%</p>
<p>Free Storage: <span id="freeSD">%FREESD%</span> | Used: <span id="usedSD">%USEDSD%</span> | Total: <span id="totalSD">%TOTALSD%</span></p>
<p><a href="https://bmorcelli.github.io/Launcher/m5lurner.html" target="_blank" rel="noopener noreferrer">Online Firmware list from M5Burner (Need Internet)</a></p>
<p>
<form id="save" enctype="multipart/form-data" method="post"><input type="hidden" id="actualFolder" name="actualFolder" value="/"></form>
<button onclick="rebootButton()">Reboot</button>
<button onclick="SDConfig()">SD pins</button>
<button onclick="WifiConfig('ssid')">WiFi</button>
<button onclick="WifiConfig('usr')">Usr/Pass</button>
<button onclick="callOTA()">OTA Update</button>
<button onclick="listFilesButton('/')">SD Files</button>

</p>
<p id="detailsheader"></p>
<p id="status"></p>
<p id="details"></p>
<p id="OTAdetails" style="display:none;">
Select a .bin file to install <br><br>
<input type="file" id="fileInput" onchange="analyzeFile()">
<div id="analysisOutput"></div>

<!-- Botões para upload das faixas de arquivo -->
<br><br>
<button id="uploadApp" style="display:none;">Start Update</button>
<button id="uploadSpiffs" style="display:none;">SPIFFS Update</button>
<div id="spiffsInfo"style="display:none;">
<br>
<p>
<b>SPIFFS</b>: The SPIFFS (Serial Peripheral Interface Flash File System) is a file system designed to be 
used with NOR flash memory devices and microcontrollers that support the Serial Peripheral Interface 
(SPI) protocol such as the ESP32
<br>
<br>
Most of the apps over the internet don't need SPIFFS to work, but there are some exceptions like <b>OrcaOne</b>.
</p>
</div>
</p>
<p id="updetailsheader"></p>
<p id="updetails"></p>
<div id="drop-area" class="drop-area" ondrop="drop(event, document.getElementById('actualFolder').value)">
<p style="text-align: center;">Drag and drop files here</p>
</div>
</div>

<script>
function WifiConfig(target) {
let wifiSsid;
let wifiPwd;
if (target == "usr") {
  wifiSsid = prompt("Please enter the Username of your network", "admin");
  wifiPwd = prompt("Please enter the Password of your network", "launcher");
  }
if (target == "ssid") {
  wifiSsid = prompt("Please enter the ssid of your network", "");
  wifiPwd = prompt("Please enter the Password of your network", "");
}

if (wifiSsid == null || wifiSsid == "" || wifiPwd == null) { 
window.alert("Invalid " + target + " or password");
} else {
xmlhttp=new XMLHttpRequest();
xmlhttp.open("GET", "/wifi?" + target + "=" + wifiSsid + "&pwd=" + wifiPwd, false);
xmlhttp.send();
document.getElementById("status").innerHTML = xmlhttp.responseText;
}
}

function SDConfig() {
let miso = prompt("MISO pin", "");
let mosi = prompt("MOSI pin", "");
let sck = prompt("SCK pin", "");
let cs = prompt("CS pin", "");

if (miso == "" || mosi == "" || sck == "" || cs == "" || miso == null || mosi == null || sck == null || cs == null) { 
window.alert("Invalid pins");
} else {
xmlhttp=new XMLHttpRequest();
xmlhttp.open("GET", "/sdpins?miso=" + miso + "&mosi=" + mosi + "&sck=" + sck + "&cs=" + cs, false);
xmlhttp.send();
document.getElementById("status").innerHTML = xmlhttp.responseText;
}
}

function startUpdate(fileName) {
const ajax4 = new XMLHttpRequest();
const formdata4 = new FormData();
formdata4.append("fileName", fileName);
ajax4.open("POST", "/UPDATE", false);
ajax4.send(formdata4);
}
function callOTA() {
const ajax3 = new XMLHttpRequest();
const formdata = new FormData();
formdata.append("update", 1);
ajax3.open("POST", "/OTA", false);
ajax3.send(formdata);
document.getElementById("detailsheader").innerHTML = "<h3>OTA Update<h3>";
document.getElementById("status").innerHTML = "";
document.getElementById("details").innerHTML = "";
document.getElementById("updetailsheader").innerHTML = "";
document.getElementById("updetails").innerHTML = "";
document.getElementById("OTAdetails").style.display = 'block';
document.getElementById("drop-area").style.display = 'none';
}

function analyzeFile() {
const fileInput = document.getElementById('fileInput');
const outputDiv = document.getElementById('analysisOutput');
const uploadAppBtn = document.getElementById('uploadApp');
const uploadSpiffsBtn = document.getElementById('uploadSpiffs');

outputDiv.style.display = 'none';
uploadAppBtn.style.display = 'none';
uploadSpiffsBtn.style.display = 'none';

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

reader.onload = function(e) {
const data = new Uint8Array(reader.result);
let start_point = 0;
let spiffs_offset = 0;
let spiffs_size = 0;
let app_size = 0;
let spiffs = false;
const MAX_APP = 0x470000;
const MAX_SPIFFS = 0x100000;
const first_slice = data.slice(0x8000, 0x8000 + 16);
const byte0 = first_slice[0];
const byte1 = first_slice[1];
const byte2 = first_slice[2];

if (byte0 === 0xaa && byte1 === 0x50 && byte2 === 0x01&& pass === true) {
pass = false;
start_point = 0x10000;
for (let i = 0; i < 0xA0; i += 0x20) {
const pos = 0x8000 + i;
if (pos + 16 > data.length) break;

const slice = data.slice(pos, pos + 16);
const byte3 = slice[3];
const byte6 = slice[6];

if ([0x00, 0x10, 0x20].includes(byte3) && byte6 === 0x01) {
				
app_size = (slice[10] << 16) | (slice[11] << 8) | 0x00;
if (data.length < (app_size + 0x10000)) app_size = data.length - 0x10000;
if (app_size > MAX_APP) app_size = MAX_APP;
}
if (byte3 === 0x82) {
spiffs_offset = (slice[6] << 16) | (slice[7] << 8) | slice[8];
spiffs_size = (slice[10] << 16) | (slice[11] << 8);
if (data.length < spiffs_offset) spiffs = false;
else if (spiffs_size > MAX_SPIFFS) {
spiffs_size = MAX_SPIFFS;
spiffs = true;
}
if (spiffs && data.length < (spiffs_offset + spiffs_size)) spiffs_size = data.length - spiffs_offset;
}}}
else if (pass === true){
		pass = false;
start_point = 0x0;
app_size = data.length;
spiffs = false;			
}
const appBlob = new Blob([data.slice(start_point, start_point + app_size)], { type: 'application/octet-stream' });
const spiffsBlob = spiffs ? new Blob([data.slice(spiffs_offset, spiffs_offset + spiffs_size)], { type: 'application/octet-stream' }) : null;

if (app_size > 0) {
uploadAppBtn.style.display = 'inline';
uploadAppBtn.onclick = () => uploadSlice(appBlob, app_size, file.name + '-app.bin', 0);
}

if (spiffs) {
uploadSpiffsBtn.style.display = 'inline';
document.getElementById("spiffsInfo").style.display = 'block';
uploadSpiffsBtn.onclick = () => uploadSlice(spiffsBlob, spiffs_size, file.name + '-spiffs.bin', 100);
}
};
reader.readAsArrayBuffer(file);
}
function uploadSlice(blobData, c_size, fileName, comm) {
var uploadForm = "Preparing...";
document.getElementById("updetails").innerHTML = uploadForm;
const ajax = new XMLHttpRequest();
ajax.onload = function() {
if (ajax.status === 200 && ajax.responseText === "OK") {
const formdata2 = new FormData();
formdata2.append("file1", blobData, fileName);
const ajax2 = new XMLHttpRequest();
ajax2.open("POST", "/OTAFILE");
ajax2.upload.addEventListener("progress", progressHandler, false);
ajax2.addEventListener("load", completeHandler, false);
ajax2.addEventListener("error", errorHandler, false);
ajax2.addEventListener("abort", abortHandler, false);
uploadForm =
"<p>Updating...</p>" +
"<progress id='progressBar' value='0' max='100' style='width:100%%;'></progress>" +
"<h3 id='status'></h3>" +
"<p id='loaded_n_total'></p>";
document.getElementById("updetails").innerHTML = uploadForm;
ajax2.send(formdata2);
}
};
ajax.onerror = function() {
console.error("Erro na requisição inicial GET.");
};
const formdata = new FormData();
formdata.append("command", comm);
formdata.append("size", c_size);
ajax.open("POST", "/OTA", true);
ajax.send(formdata);
}
function logoutButton() {
var xhr = new XMLHttpRequest();
xhr.open("GET", "/logout", true);
xhr.send();
setTimeout(function(){ window.open("/logged-out","_self"); }, 500);
}
function rebootButton() {
if(confirm("Confirm Restart?!")) {
xmlhttp=new XMLHttpRequest();
var xhr = new XMLHttpRequest();
xhr.open("GET", "/reboot", true);
xhr.send();
}
}

function listFilesButton(folders) {
var xmlhttp = new XMLHttpRequest();
document.getElementById("drop-area").style.display = 'block';
document.getElementById("actualFolder").value = folders;
var PreFolder = folders.substring(0,folders.lastIndexOf('/'));
if (PreFolder == "") { PreFolder = "/"; }

xmlhttp.onload = function() {
if (xmlhttp.status === 200) {
var responseText = xmlhttp.responseText;
var lines = responseText.split('\n');
var tableContent = "<table><tr><th align='left'>Name</th><th style=\"text-align=center;\">Size</th><th></th></tr>\n";
	tableContent+="<tr><th align='left'><a onclick=\"listFilesButton('"+ PreFolder + "')\" href='javascript:void(0);'>... </a></th><th align='left'></th><th></th></tr>\n"

var folder = "";

lines.forEach(function(line) {
if (line) {
var type = line.substring(0, 2);
var path = line.substring(3,line.lastIndexOf(':'));
var filename = line.substring(3, line.lastIndexOf(':'));
var size = line.substring(line.lastIndexOf(':')+1);
if (type === "pa") {
      if (path !== "") folder = path + "/"; 
} else if (type === "Fo") {
			tableContent += "<tr align='left'><td><a onclick=\"listFilesButton('"+ folder + path + "')\" href='javascript:void(0);'>\n" + filename + "</a></td>";
			tableContent += "<td></td>\n";
			tableContent += "<td><i style=\"color: #e0d204;\" class=\"gg-folder\" onclick=\"listFilesButton('" + folder + path + "')\"></i>&nbsp&nbsp";
			tableContent += "<i style=\"color: #e0d204;\" class=\"gg-rename\"onclick=\"renameFile(\'" + folder + path + "\', \'" + filename + "\')\"></i>&nbsp&nbsp\n";
			tableContent += "<i style=\"color: #e0d204;\" class=\"gg-trash\"onclick=\"downloadDeleteButton(\'" + folder + path + "\', \'delete\')\"></i></td></tr>\n\n";
} else if (type === "Fi") {
			tableContent += "<tr align='left'><td>" + filename;
			if (filename.substring(filename.lastIndexOf('.') + 1).toLowerCase() === "bin") {
				tableContent+= "&nbsp<i class=\"rocket\" onclick=\"startUpdate(\'" + folder + path + "\')\"></i>";
			}
			tableContent += "</td>\n";
			tableContent += "<td style=\"font-size: 10px; text-align=center;\">" + size + "</td>\n";
			tableContent += "<td><i class=\"gg-arrow-down-r\" onclick=\"downloadDeleteButton(\'" + folder + path + "\', \'download\')\"></i>&nbsp&nbsp\n";
			tableContent += "<i class=\"gg-rename\"onclick=\"renameFile(\'" + folder + path + "\', \'" + filename + "\')\"></i>&nbsp&nbsp\n";
			tableContent += "<i class=\"gg-trash\"onclick=\"downloadDeleteButton(\'" + folder + path + "\', \'delete\')\"></i></td></tr>\n\n";			
}
}
});

tableContent += "</table>";
document.getElementById("details").innerHTML = tableContent;
} else {
console.error('Erro na requisição: ' + xmlhttp.status);
}
};

xmlhttp.onerror = function() {
console.error('Erro na rede ou falha na requisição.');
};

xmlhttp.open("GET", "/listfiles?folder=" + folders, true);
xmlhttp.send();

document.getElementById("detailsheader").innerHTML = "<h3>Files<h3>";
document.getElementById("updetailsheader").innerHTML = "<h3>Folder Actions: <button onclick=\"showUploadButtonFancy('" + folders + "')\">Upload File</button><button onclick=\"showCreateFolder('" + folders + "')\">Create Folder</button><h3>"
document.getElementById("updetails").innerHTML = "";

document.getElementById("OTAdetails").style.display = 'none';
document.getElementById("analysisOutput").style.display = 'none';
document.getElementById("spiffsInfo").style.display = 'none';
document.getElementById("uploadApp").style.display = 'none';
document.getElementById("uploadSpiffs").style.display = 'none';
}

function renameFile(filePath, oldName) {
var actualFolder = document.getElementById("actualFolder").value;
let fileName = prompt("Enter the new name: ", oldName);
if (fileName == null || fileName == "") {
window.alert("Invalid Name");
} else {
const ajax5 = new XMLHttpRequest();
const formdata5 = new FormData();
formdata5.append("filePath", filePath);
formdata5.append("fileName", fileName);
ajax5.open("POST", "/rename", false);
ajax5.send(formdata5);
document.getElementById("status").innerHTML = ajax5.responseText;

listFilesButton(actualFolder);
}
}

function downloadDeleteButton(filename, action) {
var urltocall = "/file?name=" + filename + "&action=" + action;
var actualFolder = document.getElementById("actualFolder").value;
var option;
if (action == "delete") {
option = confirm("Do you really want to DELETE the file: " + filename + " ?\n\nThis action can't be undone!");
}

xmlhttp=new XMLHttpRequest();
if (option == true || action=="create") {
xmlhttp.open("GET", urltocall, false);
xmlhttp.send();
document.getElementById("status").innerHTML = xmlhttp.responseText;
listFilesButton(actualFolder);
}
if (action == "download") {
document.getElementById("status").innerHTML = "";
window.open(urltocall,"_blank");
}
}
function showCreateFolder(folders) {
document.getElementById("status").innerHTML = "";
var uploadform =
"<p>Creating folder at: <b>" + folders + "</b>"+
"<form id=\"create_form\" enctype=\"multipart/form-data\" method=\"post\">" +
"<input type=\"hidden\" id=\"folder\" name=\"folder\" value=\"" + folders + "\">" + 
"<input type=\"text\" name=\"foldername\" id=\"foldername\">" +
"<button onclick=\"CreateFolder()\">Create Folder</button>" +
"</form></p>";
document.getElementById("updetails").innerHTML = uploadform;
}

function CreateFolder() {
var folderName = ""; 
folderName = document.getElementById("folder").value + "/" + document.getElementById("foldername").value;
downloadDeleteButton(folderName, 'create');
}

function showUploadButtonFancy(folders) {
document.getElementById("status").innerHTML = "";
var uploadform =
"<p>Send file to " + folders + "</p>"+
"<form id=\"upload_form\" enctype=\"multipart/form-data\" method=\"post\">" +
"<input type=\"hidden\" id=\"folder\" name=\"folder\" value=\"" + folders + "\">" + 
"<input type=\"file\" name=\"file1\" id=\"file1\" onchange=\"uploadFile('" + folders + "')\"><br>" +
"<progress id=\"progressBar\" value=\"0\" max=\"100\" style=\"width:100%%;\"></progress>" +
"<h3 id=\"status\"></h3>" +
"<p id=\"loaded_n_total\"></p>" +
"</form>";
document.getElementById("updetails").innerHTML = uploadform;
}
function _(el) {
return document.getElementById(el);
}
function uploadFile(folder) {
var folder = _("folder").value;
var files = _("file1").files;
var formdata = new FormData();
for (var i = 0; i < files.length; i++) {
formdata.append("files[]", files[i]);
}
formdata.append("folder", folder);
var ajax = new XMLHttpRequest();
ajax.upload.addEventListener("progress", progressHandler, false);
ajax.addEventListener("load", completeHandler, false);
ajax.addEventListener("error", errorHandler, false);
ajax.addEventListener("abort", abortHandler, false);
ajax.open("POST", "/");
ajax.send(formdata);
}
window.addEventListener("load", function() {
var dropArea = _("drop-area");
dropArea.addEventListener("dragenter", dragEnter, false);
dropArea.addEventListener("dragover", dragOver, false);
dropArea.addEventListener("dragleave", dragLeave, false);
dropArea.addEventListener("drop", drop, false);
});
function dragEnter(event) {
event.stopPropagation();
event.preventDefault();
this.classList.add("highlight");
}
function dragOver(event) {
event.stopPropagation();
event.preventDefault();
this.classList.add("highlight");
}
function dragLeave(event) {
event.stopPropagation();
event.preventDefault();
this.classList.remove("highlight");
}
var fileQueue = [];
var currentFileIndex = 0;
function drop(event, folder) {
event.stopPropagation();
event.preventDefault();
_("drop-area").classList.remove("highlight");
fileQueue = event.dataTransfer.files;
currentFileIndex = 0;
var uploadform =
"<p>Send file to " + folder + "</p>"+
"<form id=\"upload_form\" enctype=\"multipart/form-data\" method=\"post\">" +
"<progress id=\"progressBar\" value=\"0\" max=\"100\" style=\"width:100%%;\"></progress>" +
"<h3 id=\"status\"></h3>" +
"<p id=\"loaded_n_total\"></p>" +
"</form>";
document.getElementById("updetails").innerHTML = uploadform;    
if (fileQueue.length > 0) {
uploadNextFile(folder);
}
}
function uploadNextFile(folder) {
if (currentFileIndex >= fileQueue.length) {
console.log("Todos os arquivos foram enviados.");
listFilesButton(folder);
return;
}
var file = fileQueue[currentFileIndex];
var formdata = new FormData();
formdata.append("file", file);
formdata.append("folder", folder);
var ajax = new XMLHttpRequest();
ajax.upload.addEventListener("progress", progressHandler, false);
ajax.addEventListener("load", completeHandler, false);
ajax.addEventListener("error", errorHandler, false);
ajax.addEventListener("abort", abortHandler, false);
ajax.open("POST", "/");
ajax.send(formdata);
}
function progressHandler(event) {
_("loaded_n_total").innerHTML = "Uploaded " + event.loaded + " bytes";
var percent = (event.loaded / event.total) * 100;
_("progressBar").value = Math.round(percent);
if (percent >= 100) {
_("status").innerHTML = "Please wait, writing file to filesystem";
}
}
function completeHandler(event) {
_("progressBar").value = 0;
if (fileQueue.length > 0) {
currentFileIndex++;
if (currentFileIndex <= fileQueue.length) {
document.getElementById("status").innerHTML = "Uploaded " + currentFileIndex + " of " + fileQueue.length + " files.";
}
uploadNextFile(document.getElementById("actualFolder").value);
}
else {
_("status").innerHTML = "Upload Complete";
var actualFolder = document.getElementById("actualFolder").value
document.getElementById("status").innerHTML = "File Uploaded";
listFilesButton(actualFolder);
}
}
function errorHandler(event) {
_("status").innerHTML = "Upload Failed";
if (fileQueue.length > 0) {
currentFileIndex++;
document.getElementById("status").innerHTML = "Uploaded " + i + " of " + files.length + " files, please wait.";
uploadNextFile(document.getElementById("actualFolder").value);
}
}
function abortHandler(event) {
_("status").innerHTML = "inUpload Aborted";
if (fileQueue.length > 0) {
currentFileIndex++;
document.getElementById("status").innerHTML = "Uploaded " + i + " of " + files.length + " files, please wait.";
uploadNextFile(document.getElementById("actualFolder").value);
}
}

window.addEventListener("load", function() {
listFilesButton("/");
});

</script>
</body>
</html>
)rawliteral";

#else

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="UTF-8">

<!-- CSS sample thanks to @im.nix (Discord) -->
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;	margin: 0; padding: 5px; color: #00dd00; background-color: #202124; }
.container { max-width: 800px; margin: 5px auto; padding: 0 5px;	}
h3 { margin: 0; padding: 10px 0; border-bottom: 1px solid #00cc00;}
table {	width: 100%%; border-collapse: collapse; border-bottom: 1px solid #00cc00;}
th, td {padding: 5px; border-bottom: 1px solid #00cc00;}
th { text-align: left; }
a {	color: #e0d204;	text-decoration: none;}
a:hover { text-decoration: underline; cursor:pointer; }
button { background-color: #303134; color: #00dd00; border: 2px solid; padding: 4px 8px; border-radius: 4px; border-color: #00dd00; cursor: pointer; margin: 5px; }
button:hover { background-color: #292a2c; }
#detailsheader, #updetailsheader { display: flex; justify-content: space-between; }
@media (max-width: 768px) { body { font-size: 14px; }  table { font-size: 12px; } th, td { padding: 5px; } button { font-size: 12px; padding: 6px 12px; } }
th:first-child, td:first-child { width: 65%%; }
th:last-child, td:last-child { width: 100px; text-align: center; }
.float-element { position: absolute; top: 10px; right: 10px; font-size: 16px; }
.drop-area { border: 2px dashed #00dd00; padding: 100px; margin-top: 50px; display: none; }
.highlight { background-color: #303134; color: #ad007c65; }

</style>
</head>
<body>
<div class="container">
<div class="float-element"><a onclick="logoutButton()" href='javascript:void(0);'>[X]</a></div>
<h1 align="center">-= Launcher =-</h1>
<p>Firmware version: %FIRMWARE%</p>
<p>Free Storage: <span id="freeSD">%FREESD%</span> | Used: <span id="usedSD">%USEDSD%</span> | Total: <span id="totalSD">%TOTALSD%</span></p>
<p><a href="https://bmorcelli.github.io/Launcher/m5lurner.html" target="_blank" rel="noopener noreferrer">Online Firmware list from M5Burner (Need Internet)</a></p>
<p>
<form id="save" enctype="multipart/form-data" method="post"><input type="hidden" id="actualFolder" name="actualFolder" value="/"></form>
<button onclick="rebootButton()">Reboot</button>
<button onclick="SDConfig()">SD pins</button>
<button onclick="WifiConfig('ssid')">WiFi</button>
<button onclick="WifiConfig('usr')">Usr/Pass</button>
<button onclick="callOTA()">OTA Update</button>
<button onclick="listFilesButton('/')">SD Files</button>

</p>
<p id="detailsheader"></p>
<p id="status"></p>
<p id="details"></p>
<p id="OTAdetails" style="display:none;">
Select a .bin file to install <br><br>
<input type="file" id="fileInput" onchange="analyzeFile()">
<div id="analysisOutput"></div>

<!-- Botões para upload das faixas de arquivo -->
<br><br>
<button id="uploadApp" style="display:none;">Start Update</button>
<button id="uploadSpiffs" style="display:none;">SPIFFS Update</button>
<div id="spiffsInfo"style="display:none;">
<br>
<p>
<b>SPIFFS</b>: The SPIFFS (Serial Peripheral Interface Flash File System) is a file system designed to be 
used with NOR flash memory devices and microcontrollers that support the Serial Peripheral Interface 
(SPI) protocol such as the ESP32
<br>
<br>
Most of the apps over the internet don't need SPIFFS to work, but there are some exceptions like <b>OrcaOne</b>.
</p>
</div>
</p>
<p id="updetailsheader"></p>
<p id="updetails"></p>
<div id="drop-area" class="drop-area" ondrop="drop(event, document.getElementById('actualFolder').value)">
<p style="text-align: center;">Drag and drop files here</p>
</div>
</div>

<script>
function WifiConfig(target) {
let wifiSsid;
let wifiPwd;
if (target == "usr") {
  wifiSsid = prompt("Please enter the Username of your network", "admin");
  wifiPwd = prompt("Please enter the Password of your network", "launcher");
  }
if (target == "ssid") {
  wifiSsid = prompt("Please enter the ssid of your network", "");
  wifiPwd = prompt("Please enter the Password of your network", "");
}

if (wifiSsid == null || wifiSsid == "" || wifiPwd == null) { 
window.alert("Invalid " + target + " or password");
} else {
xmlhttp=new XMLHttpRequest();
xmlhttp.open("GET", "/wifi?" + target + "=" + wifiSsid + "&pwd=" + wifiPwd, false);
xmlhttp.send();
document.getElementById("status").innerHTML = xmlhttp.responseText;
}
}

function SDConfig() {
let miso = prompt("MISO pin", "");
let mosi = prompt("MOSI pin", "");
let sck = prompt("SCK pin", "");
let cs = prompt("CS pin", "");

if (miso == "" || mosi == "" || sck == "" || cs == "" || miso == null || mosi == null || sck == null || cs == null) { 
window.alert("Invalid pins");
} else {
xmlhttp=new XMLHttpRequest();
xmlhttp.open("GET", "/sdpins?miso=" + miso + "&mosi=" + mosi + "&sck=" + sck + "&cs=" + cs, false);
xmlhttp.send();
document.getElementById("status").innerHTML = xmlhttp.responseText;
}
}

function startUpdate(fileName) {
const ajax4 = new XMLHttpRequest();
const formdata4 = new FormData();
formdata4.append("fileName", fileName);
ajax4.open("POST", "/UPDATE", false);
ajax4.send(formdata4);
}
function callOTA() {
const ajax3 = new XMLHttpRequest();
const formdata = new FormData();
formdata.append("update", 1);
ajax3.open("POST", "/OTA", false);
ajax3.send(formdata);
document.getElementById("detailsheader").innerHTML = "<h3>OTA Update<h3>";
document.getElementById("status").innerHTML = "";
document.getElementById("details").innerHTML = "";
document.getElementById("updetailsheader").innerHTML = "";
document.getElementById("updetails").innerHTML = "";
document.getElementById("OTAdetails").style.display = 'block';
document.getElementById("drop-area").style.display = 'none';
}

function analyzeFile() {
const fileInput = document.getElementById('fileInput');
const outputDiv = document.getElementById('analysisOutput');
const uploadAppBtn = document.getElementById('uploadApp');
const uploadSpiffsBtn = document.getElementById('uploadSpiffs');

outputDiv.style.display = 'none';
uploadAppBtn.style.display = 'none';
uploadSpiffsBtn.style.display = 'none';

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

reader.onload = function(e) {
const data = new Uint8Array(reader.result);
let start_point = 0;
let spiffs_offset = 0;
let spiffs_size = 0;
let app_size = 0;
let spiffs = false;
const MAX_APP = 0x470000;
const MAX_SPIFFS = 0x100000;
const first_slice = data.slice(0x8000, 0x8000 + 16);
const byte0 = first_slice[0];
const byte1 = first_slice[1];
const byte2 = first_slice[2];

if (byte0 === 0xaa && byte1 === 0x50 && byte2 === 0x01&& pass === true) {
pass = false;
start_point = 0x10000;
for (let i = 0; i < 0xA0; i += 0x20) {
const pos = 0x8000 + i;
if (pos + 16 > data.length) break;

const slice = data.slice(pos, pos + 16);
const byte3 = slice[3];
const byte6 = slice[6];

if ([0x00, 0x10, 0x20].includes(byte3) && byte6 === 0x01) {
				
app_size = (slice[10] << 16) | (slice[11] << 8) | 0x00;
if (data.length < (app_size + 0x10000)) app_size = data.length - 0x10000;
if (app_size > MAX_APP) app_size = MAX_APP;
}
if (byte3 === 0x82) {
spiffs_offset = (slice[6] << 16) | (slice[7] << 8) | slice[8];
spiffs_size = (slice[10] << 16) | (slice[11] << 8);
if (data.length < spiffs_offset) spiffs = false;
else if (spiffs_size > MAX_SPIFFS) {
spiffs_size = MAX_SPIFFS;
spiffs = true;
}
if (spiffs && data.length < (spiffs_offset + spiffs_size)) spiffs_size = data.length - spiffs_offset;
}}}
else if (pass === true){
		pass = false;
start_point = 0x0;
app_size = data.length;
spiffs = false;			
}
const appBlob = new Blob([data.slice(start_point, start_point + app_size)], { type: 'application/octet-stream' });
const spiffsBlob = spiffs ? new Blob([data.slice(spiffs_offset, spiffs_offset + spiffs_size)], { type: 'application/octet-stream' }) : null;

if (app_size > 0) {
uploadAppBtn.style.display = 'inline';
uploadAppBtn.onclick = () => uploadSlice(appBlob, app_size, file.name + '-app.bin', 0);
}

if (spiffs) {
uploadSpiffsBtn.style.display = 'inline';
document.getElementById("spiffsInfo").style.display = 'block';
uploadSpiffsBtn.onclick = () => uploadSlice(spiffsBlob, spiffs_size, file.name + '-spiffs.bin', 100);
}
};
reader.readAsArrayBuffer(file);
}
function uploadSlice(blobData, c_size, fileName, comm) {
var uploadForm = "Preparing...";
document.getElementById("updetails").innerHTML = uploadForm;
const ajax = new XMLHttpRequest();
ajax.onload = function() {
if (ajax.status === 200 && ajax.responseText === "OK") {
const formdata2 = new FormData();
formdata2.append("file1", blobData, fileName);
const ajax2 = new XMLHttpRequest();
ajax2.open("POST", "/OTAFILE");
ajax2.upload.addEventListener("progress", progressHandler, false);
ajax2.addEventListener("load", completeHandler, false);
ajax2.addEventListener("error", errorHandler, false);
ajax2.addEventListener("abort", abortHandler, false);
uploadForm =
"<p>Updating...</p>" +
"<progress id='progressBar' value='0' max='100' style='width:100%%;'></progress>" +
"<h3 id='status'></h3>" +
"<p id='loaded_n_total'></p>";
document.getElementById("updetails").innerHTML = uploadForm;
ajax2.send(formdata2);
}
};
ajax.onerror = function() {
console.error("Erro na requisição inicial GET.");
};
const formdata = new FormData();
formdata.append("command", comm);
formdata.append("size", c_size);
ajax.open("POST", "/OTA", true);
ajax.send(formdata);
}
function logoutButton() {
var xhr = new XMLHttpRequest();
xhr.open("GET", "/logout", true);
xhr.send();
setTimeout(function(){ window.open("/logged-out","_self"); }, 500);
}
function rebootButton() {
if(confirm("Confirm Restart?!")) {
xmlhttp=new XMLHttpRequest();
var xhr = new XMLHttpRequest();
xhr.open("GET", "/reboot", true);
xhr.send();
}
}

function listFilesButton(folders) {
var xmlhttp = new XMLHttpRequest();
document.getElementById("drop-area").style.display = 'block';
document.getElementById("actualFolder").value = folders;
var PreFolder = folders.substring(0,folders.lastIndexOf('/'));
if (PreFolder == "") { PreFolder = "/"; }

xmlhttp.onload = function() {
if (xmlhttp.status === 200) {
var responseText = xmlhttp.responseText;
var lines = responseText.split('\n');
var tableContent = "<table><tr><th align='left'>Name</th><th style=\"text-align=center;\">Size</th><th></th></tr>\n";
	tableContent+="<tr><th align='left'><a onclick=\"listFilesButton('"+ PreFolder + "')\" href='javascript:void(0);'>... </a></th><th align='left'></th><th></th></tr>\n"

var folder = "";

lines.forEach(function(line) {
if (line) {
var type = line.substring(0, 2);
var path = line.substring(3,line.lastIndexOf(':'));
var filename = line.substring(3, line.lastIndexOf(':'));
var size = line.substring(line.lastIndexOf(':')+1);
if (type === "pa") {
	  if (path !== "") folder = path + "/"; 
} else if (type === "Fo") {
			tableContent += "<tr align='left'><td><a onclick=\"listFilesButton('"+ folder + path + "')\" href='javascript:void(0);'>" + filename + "</a></td>";
			tableContent += "<td></td>";
			tableContent += "<td><a style=\"color: #e0d204;\" onclick=\"listFilesButton('" + folder + path + "')\">Opn</a>&nbsp";
			tableContent += "<a style=\"color: #e0d204;\" class=\"gg-rename\"onclick=\"renameFile(\'" + folder + path + "\', \'" + filename + "\')\">Ren</a>&nbsp";
			tableContent += "<a style=\"color: #e0d204;\" class=\"gg-trash\"onclick=\"downloadDeleteButton(\'" + folder + path + "\', \'delete\')\">Del</a></td></tr>";
} else if (type === "Fi") {
			tableContent += "<tr align='left'><td>" + filename;
			if (filename.substring(filename.lastIndexOf('.') + 1).toLowerCase() === "bin") {
				tableContent+= "&nbsp<a onclick=\"startUpdate(\'" + folder + path + "\')\">(Install)</a>";
			}
			tableContent += "</td>";
			tableContent += "<td style=\"font-size: 10px; text-align=center;\">" + size + "</td>";
			tableContent += "<td><a onclick=\"downloadDeleteButton(\'" + folder + path + "\', \'download\')\">Dwl</a>&nbsp";
			tableContent += "<a onclick=\"renameFile(\'" + folder + path + "\', \'" + filename + "\')\">RN</a>&nbsp";
			tableContent += "<a onclick=\"downloadDeleteButton(\'" + folder + path + "\', \'delete\')\">Del</a></td></tr>";			
}
}
});

tableContent += "</table>";
document.getElementById("details").innerHTML = tableContent;
} else {
console.error('Erro na requisição: ' + xmlhttp.status);
}
};

xmlhttp.onerror = function() {
console.error('Erro na rede ou falha na requisição.');
};

xmlhttp.open("GET", "/listfiles?folder=" + folders, true);
xmlhttp.send();

document.getElementById("detailsheader").innerHTML = "<h3>Files<h3>";
document.getElementById("updetailsheader").innerHTML = "<h3>Folder Actions: <button onclick=\"showUploadButtonFancy('" + folders + "')\">Upload File</button><button onclick=\"showCreateFolder('" + folders + "')\">Create Folder</button><h3>"
document.getElementById("updetails").innerHTML = "";

document.getElementById("OTAdetails").style.display = 'none';
document.getElementById("analysisOutput").style.display = 'none';
document.getElementById("spiffsInfo").style.display = 'none';
document.getElementById("uploadApp").style.display = 'none';
document.getElementById("uploadSpiffs").style.display = 'none';
}

function renameFile(filePath, oldName) {
var actualFolder = document.getElementById("actualFolder").value;
let fileName = prompt("Enter the new name: ", oldName);
if (fileName == null || fileName == "") {
window.alert("Invalid Name");
} else {
const ajax5 = new XMLHttpRequest();
const formdata5 = new FormData();
formdata5.append("filePath", filePath);
formdata5.append("fileName", fileName);
ajax5.open("POST", "/rename", false);
ajax5.send(formdata5);
document.getElementById("status").innerHTML = ajax5.responseText;

listFilesButton(actualFolder);
}
}

function downloadDeleteButton(filename, action) {
var urltocall = "/file?name=" + filename + "&action=" + action;
var actualFolder = document.getElementById("actualFolder").value;
var option;
if (action == "delete") {
option = confirm("Do you really want to DELETE the file: " + filename + " ?\n\nThis action can't be undone!");
}

xmlhttp=new XMLHttpRequest();
if (option == true || action=="create") {
xmlhttp.open("GET", urltocall, false);
xmlhttp.send();
document.getElementById("status").innerHTML = xmlhttp.responseText;
listFilesButton(actualFolder);
}
if (action == "download") {
document.getElementById("status").innerHTML = "";
window.open(urltocall,"_blank");
}
}
function showCreateFolder(folders) {
document.getElementById("status").innerHTML = "";
var uploadform =
"<p>Creating folder at: <b>" + folders + "</b>"+
"<form id=\"create_form\" enctype=\"multipart/form-data\" method=\"post\">" +
"<input type=\"hidden\" id=\"folder\" name=\"folder\" value=\"" + folders + "\">" + 
"<input type=\"text\" name=\"foldername\" id=\"foldername\">" +
"<button onclick=\"CreateFolder()\">Create Folder</button>" +
"</form></p>";
document.getElementById("updetails").innerHTML = uploadform;
}

function CreateFolder() {
var folderName = ""; 
folderName = document.getElementById("folder").value + "/" + document.getElementById("foldername").value;
downloadDeleteButton(folderName, 'create');
}

function showUploadButtonFancy(folders) {
document.getElementById("status").innerHTML = "";
var uploadform =
"<p>Send file to " + folders + "</p>"+
"<form id=\"upload_form\" enctype=\"multipart/form-data\" method=\"post\">" +
"<input type=\"hidden\" id=\"folder\" name=\"folder\" value=\"" + folders + "\">" + 
"<input type=\"file\" name=\"file1\" id=\"file1\" onchange=\"uploadFile('" + folders + "')\"><br>" +
"<progress id=\"progressBar\" value=\"0\" max=\"100\" style=\"width:100%%;\"></progress>" +
"<h3 id=\"status\"></h3>" +
"<p id=\"loaded_n_total\"></p>" +
"</form>";
document.getElementById("updetails").innerHTML = uploadform;
}
function _(el) {
return document.getElementById(el);
}
function uploadFile(folder) {
var folder = _("folder").value;
var files = _("file1").files;
var formdata = new FormData();
for (var i = 0; i < files.length; i++) {
formdata.append("files[]", files[i]);
}
formdata.append("folder", folder);
var ajax = new XMLHttpRequest();
ajax.upload.addEventListener("progress", progressHandler, false);
ajax.addEventListener("load", completeHandler, false);
ajax.addEventListener("error", errorHandler, false);
ajax.addEventListener("abort", abortHandler, false);
ajax.open("POST", "/");
ajax.send(formdata);
}
window.addEventListener("load", function() {
var dropArea = _("drop-area");
dropArea.addEventListener("dragenter", dragEnter, false);
dropArea.addEventListener("dragover", dragOver, false);
dropArea.addEventListener("dragleave", dragLeave, false);
dropArea.addEventListener("drop", drop, false);
});
function dragEnter(event) {
event.stopPropagation();
event.preventDefault();
this.classList.add("highlight");
}
function dragOver(event) {
event.stopPropagation();
event.preventDefault();
this.classList.add("highlight");
}
function dragLeave(event) {
event.stopPropagation();
event.preventDefault();
this.classList.remove("highlight");
}
var fileQueue = [];
var currentFileIndex = 0;
function drop(event, folder) {
event.stopPropagation();
event.preventDefault();
_("drop-area").classList.remove("highlight");
fileQueue = event.dataTransfer.files;
currentFileIndex = 0;
var uploadform =
"<p>Send file to " + folder + "</p>"+
"<form id=\"upload_form\" enctype=\"multipart/form-data\" method=\"post\">" +
"<progress id=\"progressBar\" value=\"0\" max=\"100\" style=\"width:100%%;\"></progress>" +
"<h3 id=\"status\"></h3>" +
"<p id=\"loaded_n_total\"></p>" +
"</form>";
document.getElementById("updetails").innerHTML = uploadform;    
if (fileQueue.length > 0) {
uploadNextFile(folder);
}
}
function uploadNextFile(folder) {
if (currentFileIndex >= fileQueue.length) {
console.log("Todos os arquivos foram enviados.");
listFilesButton(folder);
return;
}
var file = fileQueue[currentFileIndex];
var formdata = new FormData();
formdata.append("file", file);
formdata.append("folder", folder);
var ajax = new XMLHttpRequest();
ajax.upload.addEventListener("progress", progressHandler, false);
ajax.addEventListener("load", completeHandler, false);
ajax.addEventListener("error", errorHandler, false);
ajax.addEventListener("abort", abortHandler, false);
ajax.open("POST", "/");
ajax.send(formdata);
}
function progressHandler(event) {
_("loaded_n_total").innerHTML = "Uploaded " + event.loaded + " bytes";
var percent = (event.loaded / event.total) * 100;
_("progressBar").value = Math.round(percent);
if (percent >= 100) {
_("status").innerHTML = "Please wait, writing file to filesystem";
}
}
function completeHandler(event) {
_("progressBar").value = 0;
if (fileQueue.length > 0) {
currentFileIndex++;
if (currentFileIndex <= fileQueue.length) {
document.getElementById("status").innerHTML = "Uploaded " + currentFileIndex + " of " + fileQueue.length + " files.";
}
uploadNextFile(document.getElementById("actualFolder").value);
}
else {
_("status").innerHTML = "Upload Complete";
var actualFolder = document.getElementById("actualFolder").value
document.getElementById("status").innerHTML = "File Uploaded";
listFilesButton(actualFolder);
}
}
function errorHandler(event) {
_("status").innerHTML = "Upload Failed";
if (fileQueue.length > 0) {
currentFileIndex++;
document.getElementById("status").innerHTML = "Uploaded " + i + " of " + files.length + " files, please wait.";
uploadNextFile(document.getElementById("actualFolder").value);
}
}
function abortHandler(event) {
_("status").innerHTML = "inUpload Aborted";
if (fileQueue.length > 0) {
currentFileIndex++;
document.getElementById("status").innerHTML = "Uploaded " + i + " of " + files.length + " files, please wait.";
uploadNextFile(document.getElementById("actualFolder").value);
}
}

window.addEventListener("load", function() {
listFilesButton("/");
});

</script>
</body>
</html>
)rawliteral";
	



#endif

#if defined(STICK_C_PLUS) || defined(STICK_C_PLUS2)
const char logout_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML></HTML>)rawliteral";
#else 
const char logout_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="UTF-8">
<style>
body {
font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
margin: 0;
padding: 20px;
color: #00dd00;
background-color: #202124;
}
 
h3 {
margin: 0;
padding: 10px 0;
border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}
</style>
</head>
<body>
<h3><a href="/">Log Back In</a></h3>
</body>
</html>
)rawliteral";
#endif

#endif
#endif /*__WEBINTERFACE_H*/