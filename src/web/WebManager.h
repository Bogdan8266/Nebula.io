#pragma once
/**
 * WebManager.h — Nebula OS Web-based File Manager
 * 
 * Features:
 * - Cyberpunk Terminal Aesthetic
 * - Streaming Upload (Supports huge files without RAM crashes)
 * - File Listing & Deletion
 * - OTA Firmware Updates
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <FS.h>

class WebManager {
public:
    WebManager(fs::FS& sd) : _sd(sd), _server(80) {}

    void begin(const char* ssid, const char* pass) {
        WiFi.begin(ssid, pass);
        Serial.printf("[WEB] Connecting to %s...", ssid);
        
        // We handle connection in non-blocking way in loop()
    }

    void initDirectories() {
        const char* dirs[] = {
            "/Config",
            "/Config/AOD",
            "/Config/Backup",
            "/Music",
            "/Bitmaps",
            "/Logs",
            "/NebulaOS"
        };
        for (const char* dir : dirs) {
            if (!_sd.exists(dir)) {
                if (_sd.mkdir(dir)) {
                    Serial.printf("[WEB] Created directory: %s\n", dir);
                } else {
                    Serial.printf("[WEB] Failed to create: %s\n", dir);
                }
            }
        }
    }

    // Контроль WiFi - повне вимкнення
    void setWiFiEnabled(bool enabled) {
        _wifiEnabled = enabled;
        if (!enabled) {
            // Вимикаємо WiFi
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            _connected = false;
            Serial.println("[WEB] WiFi disabled");
        }
    }

    // Контроль веб-інтерфейсу
    void setWebEnabled(bool enabled) {
        _webEnabled = enabled;
        if (!enabled && _connected) {
            // Зупиняємо веб-сервер
            _server.stop();
            Serial.println("[WEB] Web UI disabled");
        } else if (enabled && WiFi.status() == WL_CONNECTED && !_connected) {
            // Перезапускаємо сервер
            _setupRoutes();
            _server.begin();
            Serial.println("[WEB] Web UI enabled");
        }
    }

    // Контроль OTA
    void setOTAEnabled(bool enabled) {
        _otaEnabled = enabled;
        if (enabled && WiFi.status() == WL_CONNECTED) {
            _setupOTA();
            Serial.println("[WEB] OTA enabled");
        }
    }

    void loop() {
        // Якщо WiFi вимкнено - нічого не робимо
        if (!_wifiEnabled) return;
        
        if (WiFi.status() == WL_CONNECTED && !_connected) {
            _connected = true;
            Serial.println("\n[WEB] Connected!");
            Serial.print("[WEB] IP: "); Serial.println(WiFi.localIP());
            
            if (_webEnabled) {
                _setupRoutes();
                _server.begin();
            }
            
            if (_otaEnabled) {
                _setupOTA();
            }
        }
        
        if (_connected) {
            if (_webEnabled) {
                _server.handleClient();
            }
            if (_otaEnabled) {
                ArduinoOTA.handle();
            }
        }
    }

    String getIP() { return WiFi.localIP().toString(); }

    char getRemoteKey() {
        if (_commandBuffer == 0) return 0;
        char k = _commandBuffer;
        _commandBuffer = 0;
        return k;
    }

private:
    fs::FS&   _sd;
    WebServer _server;
    bool      _connected = false;
    bool      _wifiEnabled = true;
    bool      _webEnabled = true;
    bool      _otaEnabled = false;
    char      _commandBuffer = 0;

    void _setupOTA() {
        ArduinoOTA.onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH) {
                type = "sketch";
            } else {
                type = "filesystem";
            }
            Serial.println("[OTA] Start updating " + type);
        });
        
        ArduinoOTA.onEnd([]() {
            Serial.println("\n[OTA] Update complete - rebooting");
        });
        
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
        });
        
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("[OTA] Error[%u]: ", error);
            switch (error) {
                case OTA_AUTH_ERROR: Serial.println("Auth Failed"); break;
                case OTA_BEGIN_ERROR: Serial.println("Begin Failed"); break;
                case OTA_CONNECT_ERROR: Serial.println("Connect Failed"); break;
                case OTA_RECEIVE_ERROR: Serial.println("Receive Failed"); break;
                case OTA_END_ERROR: Serial.println("End Failed"); break;
            }
        });
        
        ArduinoOTA.begin();
    }

    void _setupRoutes() {
        _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
        _server.on("/remote", HTTP_GET, [this]() { _handleRemote(); });
        _server.on("/api/cmd", HTTP_GET, [this]() {
            if (_server.hasArg("key")) {
                String k = _server.arg("key");
                if (k.length() > 0) _commandBuffer = k[0];
            }
            _server.send(200, "text/plain", "OK");
        });
        _server.on("/delete", HTTP_POST, [this]() { _handleDelete(); });
        _server.on("/mkdir", HTTP_POST, [this]() { _handleCreateDir(); });
        
        // Upload handler
        _server.on("/upload", HTTP_POST, [this]() {
            _server.send(200, "text/plain", "Upload Success");
        }, [this]() { _handleUpload(); });
    }

    void _handleRoot() {
        String currentDir = "/";
        if (_server.hasArg("dir")) {
            currentDir = _server.arg("dir");
        }
        if (!currentDir.startsWith("/")) currentDir = "/" + currentDir;
        if (!currentDir.endsWith("/")) currentDir += "/";

        String html = _getHtmlHeader();
        html += "<h3>FILES (" + currentDir + ")</h3><ul class='file-list'>";
        
        if (currentDir != "/") {
            String parentDir = currentDir.substring(0, currentDir.lastIndexOf('/', currentDir.length() - 2));
            if (parentDir == "") parentDir = "/";
            html += "<li><a href='/?dir=" + parentDir + "' class='name'>[..] UP</a></li>";
        }

        fs::File root = _sd.open(currentDir);
        if (root && root.isDirectory()) {
            fs::File file = root.openNextFile();
            while (file) {
                const char* name = file.name();
                String fullPath = currentDir + String(name);
                
                if (file.isDirectory()) {
                    html += "<li><a href='/?dir=" + fullPath + "' class='name'>[" + String(name) + "]</a> ";
                    html += "<button onclick=\"deletePath('" + fullPath + "')\" class='del'>[DELETE]</button></li>";
                } else {
                    uint32_t size = file.size();
                    String sizeStr = (size > 1024 * 1024) ? String(size / (1024.0 * 1024.0), 1) + "MB" : String(size / 1024.0, 1) + "KB";
                    html += "<li><span class='name'>" + String(name) + "</span> <span class='size'>[" + sizeStr + "]</span> ";
                    html += "<button onclick=\"deletePath('" + fullPath + "')\" class='del'>[DELETE]</button></li>";
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
        } else {
            html += "<li><span class='name' style='color:red;'>Failed to open directory</span></li>";
        }
        
        html += "</ul><hr><h3>UPLOAD FILES TO " + currentDir + "</h3>";
        html += "<input type='hidden' id='upload-dir' value='" + currentDir + "'>";
        html += "<input type='file' id='file-input' multiple><br><br>";
        html += "<button onclick='uploadFiles()' class='btn'>UPLOAD TO SD</button>";
        html += "<div id='progress-container' style='display:none; margin-top:20px;'>";
        html += "<div style='background:#333; width:100%; height:20px;'><div id='progress-bar' style='background:#0f0; width:0%; height:100%;'></div></div>";
        html += "<div id='progress-text' class='info' style='margin-top:5px;'>0%</div>";
        html += "<div id='speed-text' class='info' style='color:#888;'></div>";
        html += "</div>";
        
        html += "<hr><h3>QUICK ACTIONS</h3>";
        html += "<a href='/remote' class='btn' style='text-decoration:none;'>OPEN REMOTE CONTROL</a>";

        html += _getHtmlFooter();
        _server.send(200, "text/html", html);
    }

    void _handleRemote() {
        String html = _getHtmlHeader();
        html += R"raw(
<style>
.remote-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 15px; max-width: 400px; margin: 20px auto; }
.remote-btn { background: #000; color: #0f0; border: 2px solid #0f0; padding: 25px; font-size: 24px; font-weight: bold; cursor: pointer; text-align: center; }
.remote-btn:active { background: #0f0; color: #000; }
.remote-btn.wide { grid-column: span 2; }
.spec-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; max-width: 400px; margin: 0 auto; }
.status-msg { text-align: center; margin-top: 20px; font-size: 14px; color: #888; }
</style>

<div style="text-align:center;">
    <h2>NEBULA WEB REMOTE</h2>
    <div class="remote-grid">
        <div class="remote-btn" onclick="sendCmd('1')">1</div>
        <div class="remote-btn" onclick="sendCmd('2')">2</div>
        <div class="remote-btn" onclick="sendCmd('3')">3</div>
        <div class="remote-btn wide" onclick="sendCmd('4')">4 (BACK)</div>
        <div class="remote-btn" onclick="sendCmd('5')">5</div>
    </div>
    <div class="spec-grid">
        <div class="remote-btn" style="color:yellow; border-color:yellow;" onclick="sendCmd('r')">RESCAN [r]</div>
        <div class="remote-btn" style="color:cyan; border-color:cyan;" onclick="sendCmd('d')">DUMP [d]</div>
    </div>
    <p class="status-msg">Hint: Keyboard keys 1-5, R, D are supported.</p>
    <a href="/" class="name" style="display:block; margin-top:30px;">[ BACK TO FILE MANAGER ]</a>
</div>

<script>
function sendCmd(key) {
    fetch('/api/cmd?key=' + key).then(() => {
        // Subtle feedback
        console.log("Cmd sent: " + key);
    });
}
document.onkeydown = function(e) {
    let k = e.key.toLowerCase();
    if (['1','2','3','4','5','r','d'].includes(k)) {
        sendCmd(k);
        // Visual feedback for keyboard
        let btns = document.getElementsByClassName('remote-btn');
        for(let b of btns) {
            if(b.innerText.startsWith(k.toUpperCase()) || b.innerText == k) {
                b.style.background = "#0f0"; b.style.color = "#000";
                setTimeout(() => { b.style.background = ""; b.style.color = ""; }, 100);
            }
        }
    }
};
</script>
)raw";
        html += _getHtmlFooter();
        _server.send(200, "text/html", html);
    }

    void _handleDelete() {
        if (!_server.hasArg("path")) {
            _server.send(400, "text/plain", "Missing path");
            return;
        }
        String path = _server.arg("path");
        Serial.printf("[WEB] Deleting: %s\n", path.c_str());
        
        fs::File file = _sd.open(path, "r");
        if (file) {
            bool isDir = file.isDirectory();
            file.close();
            
            bool success = false;
            if (isDir) {
                success = _sd.rmdir(path.c_str());
            } else {
                success = _sd.remove(path.c_str());
            }
            
            if (success) {
                _server.send(200, "text/plain", "OK");
            } else {
                _server.send(500, "text/plain", "Delete failed (folder must be empty)");
            }
        } else {
            _server.send(404, "text/plain", "File not found");
        }
    }

    void _handleCreateDir() {
        if (!_server.hasArg("path")) {
            _server.send(400, "text/plain", "Missing path");
            return;
        }
        String path = _server.arg("path");
        if (_sd.mkdir(path.c_str())) {
            _server.send(200, "text/plain", "OK");
        } else {
            _server.send(500, "text/plain", "Failed to create directory");
        }
    }

    void _handleUpload() {
        HTTPUpload& upload = _server.upload();
        static fs::File uploadFile;

        if (upload.status == UPLOAD_FILE_START) {
            String dir = _server.hasArg("dir") ? _server.arg("dir") : "/";
            if (!dir.endsWith("/")) dir += "/";
            
            String filename = upload.filename;
            String fullPath = dir + filename;
            
            Serial.printf("[WEB] Uploading: %s\n", fullPath.c_str());
            
            // Delete if exists
            if (_sd.exists(fullPath.c_str())) _sd.remove(fullPath.c_str());
            
            uploadFile = _sd.open(fullPath, "w");
            if (!uploadFile) {
                Serial.println("[WEB] Open fail!");
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) {
                uploadFile.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) {
                uploadFile.close();
                Serial.printf("[WEB] Success: %lu bytes\n", upload.totalSize);
            }
        }
    }

    String _getHtmlHeader() {
        return R"raw(
<!DOCTYPE html><html><head>
<title>NEBULA OS | FILE MANAGER</title>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<style>
body { background: #000; color: #0f0; font-family: 'Courier New', monospace; padding: 20px; }
h1, h2, h3 { border-bottom: 2px solid #0f0; padding-bottom: 5px; }
.info { color: #fff; margin: 10px 0; font-weight: bold; }
.file-list { list-style: none; padding: 0; }
.file-list li { padding: 8px 0; border-bottom: 1px solid #333; display: flex; align-items: center; }
.name { flex-grow: 1; color: #0a0; text-decoration: none; }
.name:hover { color: #fff; }
.size { color: #888; margin: 0 15px; }
.del { background: #000; color: #f00; font-weight: bold; border: 1px solid #f00; padding: 2px 5px; cursor: pointer; }
.del:hover { background: #f00; color: #000; }
.btn { background: #0f0; color: #000; border: none; padding: 10px 20px; font-weight: bold; cursor: pointer; }
.btn:hover { background: #fff; }
hr { border: 0; border-top: 1px solid #333; margin: 20px 0; }
input[type=file], input[type=text] { color: #fff; background: #222; border: 1px solid #555; padding: 5px; }
</style>
<script>
function deletePath(path) {
    if(confirm("Delete " + path + "?")) {
        fetch('/delete?path=' + encodeURIComponent(path), {method: 'POST'})
        .then(res => {
            if(res.ok) window.location.reload();
            else alert("Delete failed");
        });
    }
}
function createFolder() {
    let base = document.getElementById('upload-dir').value;
    let name = document.getElementById('folder-name').value;
    if(!name) return;
    let full = base + name;
    fetch('/mkdir?path=' + encodeURIComponent(full), {method: 'POST'})
    .then(res => {
        if(res.ok) window.location.reload();
        else alert("Failed to create folder");
    });
}
async function uploadFiles() {
    const input = document.getElementById('file-input');
    const dir = document.getElementById('upload-dir').value;
    const progressContainer = document.getElementById('progress-container');
    const progressBar = document.getElementById('progress-bar');
    const progressText = document.getElementById('progress-text');
    const speedText = document.getElementById('speed-text');
    
    if (input.files.length === 0) { alert("Select files first."); return; }
    
    progressContainer.style.display = 'block';
    
    for (let i = 0; i < input.files.length; i++) {
        let file = input.files[i];
        let startTime = Date.now();
        
        await new Promise((resolve, reject) => {
            let xhr = new XMLHttpRequest();
            xhr.open("POST", "/upload?dir=" + encodeURIComponent(dir), true);
            
            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    let percent = (e.loaded / e.total) * 100;
                    progressBar.style.width = percent + "%";
					progressText.innerText = "File " + (i+1) + "/" + input.files.length + ": " + Math.round(percent) + "%";
					
					let duration = (Date.now() - startTime) / 1000;
					if(duration > 0) {
						let bps = e.loaded / duration;
						let kbps = bps / 1024;
						if (kbps > 1024) {
							speedText.innerText = (kbps / 1024).toFixed(2) + " MB/s";
						} else {
							speedText.innerText = kbps.toFixed(2) + " KB/s";
						}
					}
                }
            };
            
            xhr.onload = function() {
                if (xhr.status == 200) { resolve(); }
                else { alert("Upload failed: " + file.name); reject(); }
            };
            
            let formData = new FormData();
            formData.append("upload", file);
            xhr.send(formData);
        });
    }
    
    progressText.innerText = "All files uploaded!";
    setTimeout(() => { window.location.reload(); }, 1000);
}
</script>
</head><body>
<h1>NEBULA OS // TERMINAL_V0.1</h1>
)raw";
    }

    String _getHtmlFooter() {
        return "</body></html>";
    }
};
