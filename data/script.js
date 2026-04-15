/**
 * @file script.js
 * @brief Web interface logic for the Ringclock Ultimate.
 */

console.log("script.js loaded.");

let ws;
let settingsRetryTimer = null;
let settingsFetchPending = false;
let settingsRetryCount = 0;
let autoHomingActive  = false; // tracks motorAutoHoming from server (true = auto homing enabled at runtime)
let _nightActive      = false;
let _nightFeatures    = 0;

/* ---------------------------------------------------------
   Time helpers
---------------------------------------------------------- */
function timeStringToMinutes(timeStr) {
    if (!timeStr) return 0;
    const parts = timeStr.split(':');
    if (parts.length === 2)
        return parseInt(parts[0], 10) * 60 + parseInt(parts[1], 10);
    return 0;
}

function minutesToTimeString(minutes) {
    const h = Math.floor(minutes / 60).toString().padStart(2, '0');
    const m = (minutes % 60).toString().padStart(2, '0');
    return `${h}:${m}`;
}

// Auto-range formatter for a duration given in milliseconds
function formatMs(ms) {
    if (ms < 1000)        return ms + ' ms';
    if (ms < 60000)       return (ms / 1000) + ' s';
    if (ms < 3600000)     return (ms / 60000) + ' min';
    return (ms / 3600000) + ' h';
}

// Auto-range formatter for an uptime given in seconds
function formatUptime(s) {
    s = Math.floor(s);
    if (s < 60)    return s + ' s';
    if (s < 3600)  { const m = Math.floor(s / 60);  const rs = s % 60;  return rs  ? m + ' min ' + rs + ' s'  : m + ' min'; }
    if (s < 86400) { const h = Math.floor(s / 3600); const rm = Math.floor((s % 3600) / 60); return rm ? h + ' h ' + rm + ' min' : h + ' h'; }
    const d = Math.floor(s / 86400); const rh = Math.floor((s % 86400) / 3600); return rh ? d + ' d ' + rh + ' h' : d + ' d';
}

/* ---------------------------------------------------------
   Motor helpers
---------------------------------------------------------- */

// Shows/hides motor panels and the correct motorGrid select
// Rule 1: motorMode == 0  → hide everything below
// Rule 2: motorMode == 4  → show homingPanel only
// Rule 3: motorGrid    == 0   → hide Speed + Acceleration (analog mode)
function updateMotorGridUI(mode, stepsValue) {
    const enabledPanel = document.getElementById('motorEnabledPanel');
    const homingPanel  = document.getElementById('homingPanel');
    const optionsPanel = document.getElementById('motorGridOptionsPanel');
    const selH = document.getElementById('motorGridH');
    const selM = document.getElementById('motorGridM');
    const selS = document.getElementById('motorGridS');

    [selH, selM, selS].forEach(s => s.style.display = 'none');
    homingPanel.style.display  = 'none';

    const mainInfoBox        = document.getElementById('motorMainInfoBox');
    const homingInfoSensor   = document.getElementById('homingInfoSensor');
    const homingInfoNoSensor = document.getElementById('homingInfoNoSensor');

    if (mainInfoBox)        mainInfoBox.style.display        = 'none';
    if (homingInfoSensor)   homingInfoSensor.style.display   = 'none';
    if (homingInfoNoSensor) homingInfoNoSensor.style.display = 'none';

    if (mode == 0) {
        enabledPanel.style.display = 'none';
        return;
    }

    if (mode == 4) {
        enabledPanel.style.display = 'none';
        homingPanel.style.display  = 'block';
        // Show the appropriate calibration info box based on sensor compiled in AND enabled at runtime
        if (autoHomingActive) {
            if (homingInfoSensor) homingInfoSensor.style.display = 'block';
        } else {
            if (homingInfoNoSensor) homingInfoNoSensor.style.display = 'block';
            // Without active auto-homing there is no homing to wait for — show controls immediately
            const statusRow  = document.getElementById('homingStatusRow');
            const controlsEl = document.getElementById('homingControlsPanel');
            if (statusRow)  statusRow.style.display  = 'none';
            if (controlsEl) controlsEl.style.display = 'block';
        }
        return;
    }

    enabledPanel.style.display = 'block';
    if (mainInfoBox) mainInfoBox.style.display = 'block';
    const gridParamsInfo = document.getElementById('motorInfoGridParams');
    if (gridParamsInfo) gridParamsInfo.style.display = stepsValue > 0 ? 'list-item' : 'none';
    const active = mode == 1 ? selH : mode == 2 ? selM : selS;
    active.style.display = 'block';
    if (stepsValue !== undefined) active.value = stepsValue;

    optionsPanel.style.display = stepsValue > 0 ? 'block' : 'none';
}

// Returns the grid interval [ms] for the current mode + grid setting
function calcGridIntervalMs(mode, steps) {
    const gridIntervals = {
        1: [null, 15 * 60 * 1000, 30 * 60 * 1000, 1 * 60 * 60 * 1000, 3 * 60 * 60 * 1000], // hours
        2: [null, 15 * 1000, 30 * 1000, 60 * 1000, 5 * 60 * 1000, 15 * 60 * 1000],   // minutes
        3: [null, 250, 500, 1000, 5000, 15000],                                      // seconds
    };
    return (gridIntervals[mode] && gridIntervals[mode][steps]) || null;
}

// Returns the value of the currently visible motorGrid select
function getActiveMotorGrid() {
    const mode = parseInt(document.getElementById('motorMode')?.value || '0');
    const selId = mode == 1 ? 'motorGridH' : mode == 2 ? 'motorGridM' : 'motorGridS';
    return parseInt(document.getElementById(selId)?.value || '0');
}

/* ---------------------------------------------------------
   Ring mask helpers
---------------------------------------------------------- */
function updateRingMasks(handPrefix, maskValue) {
    document.querySelectorAll(`.ring-mask-check[data-hand="${handPrefix}"]`).forEach(cb => {
        cb.checked = (maskValue & (1 << parseInt(cb.dataset.bit))) !== 0;
    });
}

function getRingMaskFromUI(handPrefix) {
    let mask = 0;
    document.querySelectorAll(`.ring-mask-check[data-hand="${handPrefix}"]`).forEach(cb => {
        if (cb.checked) mask |= (1 << parseInt(cb.dataset.bit));
    });
    return mask;
}

/* ---------------------------------------------------------
   MQTT status badge helper
---------------------------------------------------------- */
function updateMqttStatus(connected) {
    const badge = document.getElementById('mqttStatusBadge');
    const system = document.getElementById('mqttStatusSystem');
    if (badge) {
        badge.textContent = connected ? 'Online' : 'Offline';
        badge.className = 'status-badge ' + (connected ? 'status-online' : 'status-offline');
    }
    if (system) system.textContent = connected ? '✓ Connected' : '✗ Disconnected';
}

/* ---------------------------------------------------------
   WebSocket
---------------------------------------------------------- */
function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

function loadSettingsHttp() {
    if (settingsFetchPending) return;
    settingsFetchPending = true;
    settingsRetryCount = 0;
    // TODO: For HTML debugging with LiveServer use the ESP IP directly
    //fetch('http://192.168.178.130/settings')    // LiveServer
    fetch('/settings')                             // production
        .then(r => r.json())
        .then(data => { settingsRetryCount = 0; clearTimeout(settingsRetryTimer); updateUI(data); console.log("Settings loaded via HTTP"); })
        .catch(e => { if (++settingsRetryCount <= 3) { console.warn("Settings fetch failed, retry", settingsRetryCount); settingsRetryTimer = setTimeout(loadSettingsHttp, 2000); } else { console.error("Settings fetch gave up after 3 retries:", e); } })
        .finally(() => { settingsFetchPending = false; });
}

function reconnect() {
    const activeTab = document.querySelector(".tab.active");
    if (activeTab) activeTab.click();
}

function connectWS() {
    // TODO: For HTML debugging with LiveServer use the IP address of the ESP
    //ws = new WebSocket(`ws://192.168.178.130/ws`);
    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen  = () => { console.log("WS connected"); };
    ws.onclose = () => { console.warn("WS disconnected, retrying..."); setTimeout(connectWS, 2000); };
    ws.onmessage = (msg) => {
        const data = JSON.parse(msg.data);
        updateUI(data);
    };
}

/* ---------------------------------------------------------
   UI sync
---------------------------------------------------------- */
const syncColor = (id, hex) => {
    const el = document.getElementById(id);
    if (el && hex) el.value = hex;
};

function minutesToTime(minutes) {
    const h = Math.floor(minutes / 60);
    const m = minutes % 60;
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
}

function timeToMinutes(t) {
    if (!t) return 0;
    const [h, m] = t.split(':').map(Number);
    return h * 60 + m;
}

function updateNightOverrides() {
    const mark = (ids, active) => {
        ids.forEach(id => {
            const el = document.getElementById(id);
            if (!el) return;
            el.classList.toggle('night-override', active);
            el.title = active ? 'Currently overridden by Night Mode' : '';
        });
    };
    mark(['autoBrightnessRow'],
        _nightActive && !!(_nightFeatures & 0x01));
    mark(['sfxShortCircuitRow', 'sfxRadarRow', 'sfxShootingStarRow', 'sfxHeartbeatRow'],
        _nightActive && !!(_nightFeatures & 0x02));
    mark(['sHandRingSelection'],
        _nightActive && !!(_nightFeatures & 0x04));
    mark(['motorModeRow'],
        _nightActive && !!(_nightFeatures & 0x08));
    mark(['hourMarksRow', 'quarterMarksRow'],
        _nightActive && !!(_nightFeatures & 0x10));
}

function updateUI(data) {
    // Brightness
    if (data.autoBrightness !== undefined) {
        autoBrightness.checked = data.autoBrightness;
        autoPanel.style.display = data.autoBrightness ? "block" : "none";
        manualPanel.style.display = data.autoBrightness ? "none" : "block";
        document.getElementById('autoBrightnessMonitor').style.display = data.autoBrightness ? "inline" : "none";
    }

    const syncSlider = (id, value) => {
        const input = document.getElementById(id);
        const display = document.getElementById(id + "Val");
        if (input) input.value = value;
        if (display) display.textContent = value;
    };
    const syncIntervalSlider = (id, value) => {
        const input = document.getElementById(id);
        if (input) input.value = minutesToTime(value);
    };

    if (data.manualBrightness !== undefined) syncSlider("manualBrightness", data.manualBrightness);
    if (data.autoMin !== undefined) syncSlider("autoMin", data.autoMin);
    if (data.autoMax !== undefined) syncSlider("autoMax", data.autoMax);
    if (data.autoLuxMax !== undefined) syncSlider("autoLuxMax", data.autoLuxMax);
    if (data.nightBrightness !== undefined) syncSlider("nightBrightness", data.nightBrightness);

    // Ambient
    if (data.ambientEnabled !== undefined) ambientEnabled.checked = data.ambientEnabled;
    if (data.hourMarksEnabled !== undefined) hourMarksEnabled.checked = data.hourMarksEnabled;
    if (data.quarterMarksEnabled !== undefined) quarterMarksEnabled.checked = data.quarterMarksEnabled;

    syncColor("ambientColorPicker", data.ambientColor);
    syncColor("hourMarkColorPicker", data.hourMarkColor);
    syncColor("quarterMarkColorPicker", data.quarterMarkColor);

    // Time hands
    ["s", "m", "h"].forEach(p => updateHandUI(p, data));

    // SFX
    if (data.sfxShortCircuitInterval !== undefined) syncIntervalSlider("sfxShortCircuitInterval", data.sfxShortCircuitInterval);
    if (data.sfxRadarInterval !== undefined) syncIntervalSlider("sfxRadarInterval", data.sfxRadarInterval);
    if (data.sfxShootingStarInterval !== undefined) syncIntervalSlider("sfxShootingStarInterval", data.sfxShootingStarInterval);
    if (data.sfxHeartbeatInterval !== undefined) syncIntervalSlider("sfxHeartbeatInterval", data.sfxHeartbeatInterval);
    if (data.sfxHeartbeatIntensity !== undefined) syncSlider("sfxHeartbeatIntensity", data.sfxHeartbeatIntensity);

    // Motor
    if (data.motorMode !== undefined || data.motorGrid !== undefined) {
        const mode = data.motorMode !== undefined ? data.motorMode : parseInt(motorMode.value);
        const steps = data.motorGrid !== undefined ? data.motorGrid : 0;
        motorMode.value = mode;
        updateMotorGridUI(mode, steps);
    }
    if (data.motorSpeed !== undefined) syncSlider("motorSpeed", data.motorSpeed);
    if (data.motorAccel !== undefined) syncSlider("motorAccel", data.motorAccel);

    // Motor ramp monitor
    if (data.rampDurationMs !== undefined) {
        const dur = data.rampDurationMs;
        const steps = data.rampStepsTotal;
        const missed = data.rampMissedSteps;
        const mode = parseInt(motorMode.value);
        const interval = calcGridIntervalMs(mode, getActiveMotorGrid());

        document.getElementById('gridInterval').textContent = interval ? formatMs(interval) : '–';
        document.getElementById('rampDurationVal').textContent = dur + ' ms';
        document.getElementById('rampStepsVal').textContent = steps + ' steps';
        document.getElementById('rampMissedVal').textContent = missed !== 0 ? missed + ' ⚠️' : '0 ✓';

        const warning = document.getElementById('motorWarning');
        if (interval && dur > interval * 0.8) {
            warning.style.display = 'block';
            document.getElementById('warnDuration').textContent = dur;
            document.getElementById('warnInterval').textContent = interval;
        } else {
            warning.style.display = 'none';
        }
    }

    // Motor homing — field only present when MOTOR_AH_EN=1
    if (data.motorAutoHoming !== undefined) {
        autoHomingActive = data.motorAutoHoming;
        const cb = document.getElementById('motorAutoHoming');
        if (cb) cb.checked = autoHomingActive;
        const rowAutoHoming = document.getElementById('rowMotorAutoHoming');
        if (rowAutoHoming) rowAutoHoming.style.display = 'flex'; // show because MOTOR_AH_EN=1
        const btn = document.getElementById('btnStartHoming');
        if (btn) btn.style.display = autoHomingActive ? 'inline-block' : 'none';
        const statusRow = document.getElementById('homingStatusRow');
        if (statusRow) statusRow.style.display = autoHomingActive ? 'flex' : 'none';
        const mode = parseInt(document.getElementById('motorMode')?.value || '0');
        const stepsValue = parseInt(document.getElementById('motorGrid')?.value || '0');
        updateMotorGridUI(mode, stepsValue);
    }
    if (data.homingState !== undefined) {
        const stateLabels = { idle: '–', travel: '⏳ Travel…', measure: '⏳ Measure…', done: '✓ Ready', error: '⚠ Timeout – no sensor signal' };
        const statusEl = document.getElementById('homingStatusText');
        if (statusEl) statusEl.textContent = stateLabels[data.homingState] || data.homingState;

        const homingRunning = (data.homingState === 'travel' || data.homingState === 'measure');

        // Show the homing panel whenever mode 4 is active or homing is running
        const mode = parseInt(document.getElementById('motorMode')?.value || '0');
        const homingPanel = document.getElementById('homingPanel');
        if (homingPanel && (mode == 4 || homingRunning))
            homingPanel.style.display = 'block';

        // Jog/accept controls are only useful after homing has completed (not during travel/measure)
        const controls = document.getElementById('homingControlsPanel');
        if (controls) controls.style.display = (mode == 4 && !homingRunning) ? 'block' : 'none';
    }

    // Night mode
    if (data.nightModeEnabled !== undefined) nightModeEnabled.checked = data.nightModeEnabled;
    if (data.nightStart       !== undefined) nightStart.value         = minutesToTimeString(data.nightStart);
    if (data.nightEnd         !== undefined) nightEnd.value           = minutesToTimeString(data.nightEnd);
    if (data.nightActive !== undefined) {
        _nightActive = data.nightActive;
        nightActiveStatus.textContent = data.nightActive ? 'Active' : 'Inactive';
    }
    if (data.nightFeatures !== undefined) {
        _nightFeatures = data.nightFeatures;
        const f = data.nightFeatures;
        nightDimLeds.checked       = !!(f & 0x01);
        nightSfxOff.checked        = !!(f & 0x02);
        nightSecondHandOff.checked = !!(f & 0x04);
        nightMotorHome.checked     = !!(f & 0x08);
        nightMarkersOff.checked    = !!(f & 0x10);
        document.getElementById('nightDimGroup').classList.toggle('visible', nightDimLeds.checked);
    }
    if (data.nightActive !== undefined || data.nightFeatures !== undefined) {
        updateNightOverrides();
    }

    // MQTT settings
    const syncText = (id, value) => {
        const el = document.getElementById(id);
        if (el && value !== undefined) el.value = value;
    };
    if (data.mqttEnabled !== undefined) {
        const el = document.getElementById('mqttEnabled');
        if (el) el.checked = data.mqttEnabled;
    }
    syncText('mqttBroker', data.mqttBroker);
    syncText('mqttPort', data.mqttPort);
    syncText('mqttUser', data.mqttUser);
    syncText('mqttClientId', data.mqttClientId);
    syncText('mqttTopicBase', data.mqttTopicBase);
    // Password is never sent from the server — only written to server if changed by user

    // MQTT connection status (in monitoring and settings payload)
    if (data.mqttConnected !== undefined)
        updateMqttStatus(data.mqttConnected);

    // System
    if (data.lux        !== undefined) luxVal.textContent = Number(data.lux).toFixed(1);
    if (data.brightness !== undefined) document.getElementById('autoBrightnessVal').textContent = data.brightness;
    if (data.current_mA !== undefined) currentVal.textContent = data.current_mA + " mA";
    if (data.rssi !== undefined) rssiVal.textContent = data.rssi + " dBm";
    if (data.ip !== undefined) ipVal.textContent = data.ip;
    if (data.uptime     !== undefined) uptimeVal.textContent    = formatUptime(data.uptime);
    if (data.localTime  !== undefined) document.getElementById('localTimeVal').textContent = data.localTime;
    if (data.powerLimit !== undefined) powerLimit.value = data.powerLimit;
    if (data.logLevel   !== undefined) logLevel.value   = data.logLevel;
    if (data.timezone !== undefined) {
        const sel = document.getElementById('timezone');
        if (sel) sel.value = data.timezone;
    }
}

function updateHandUI(p, data) {
    syncColor(`${p}HandCol`, data[`${p}HandCol`]);
    syncColor(`${p}TailStartCol`, data[`${p}TailStartCol`]);
    syncColor(`${p}TailFwdEndCol`, data[`${p}TailFwdEndCol`]);
    syncColor(`${p}TailBackEndCol`, data[`${p}TailBackEndCol`]);

    if (data[`${p}TailFwdLen`] !== undefined) {
        const el = document.getElementById(`${p}TailFwdLen`);
        const val = document.getElementById(`${p}TailFwdLenVal`);
        if (el) el.value = data[`${p}TailFwdLen`];
        if (val) val.textContent = data[`${p}TailFwdLen`];
    }
    if (data[`${p}TailBackLen`] !== undefined) {
        const el = document.getElementById(`${p}TailBackLen`);
        const val = document.getElementById(`${p}TailBackLenVal`);
        if (el) el.value = data[`${p}TailBackLen`];
        if (val) val.textContent = data[`${p}TailBackLen`];
    }
    if (data[`${p}RingMask`] !== undefined) updateRingMasks(p, data[`${p}RingMask`]);
}

/* ---------------------------------------------------------
   Event bindings
---------------------------------------------------------- */
window.onload = () => {

    // Tab switching
    document.querySelectorAll(".tab").forEach(btn => {
        btn.onclick = () => {
            document.querySelectorAll(".tab, .tab-content").forEach(el => el.classList.remove("active"));
            btn.classList.add("active");
            document.getElementById(btn.dataset.tab).classList.add("active");
            loadSettingsHttp();
        };
    });

    // Brightness
    autoBrightness.onchange = e => {
        autoPanel.style.display = e.target.checked ? "block" : "none";
        manualPanel.style.display = e.target.checked ? "none" : "block";
        document.getElementById('autoBrightnessMonitor').style.display = e.target.checked ? "inline" : "none";
        send({ autoBrightness: e.target.checked });
    };

    const bindRange = (id) => {
        const el = document.getElementById(id);
        const valEl = document.getElementById(id + "Val");
        if (!el) return;
        el.oninput = () => { if (valEl) valEl.textContent = el.value; };
        el.onchange = () => { send({ [id]: Number(el.value) }); };
    };
    const bindIntervalRange = (id) => {
        const el = document.getElementById(id);
        if (!el) return;
        el.onchange = () => { send({ [id]: timeToMinutes(el.value) }); };
    };
    ["sfxShortCircuitInterval", "sfxRadarInterval",
        "sfxShootingStarInterval", "sfxHeartbeatInterval",
    ].forEach(bindIntervalRange);
    ["manualBrightness", "autoMin", "autoMax", "autoLuxMax",
        "nightBrightness",
        "sfxHeartbeatIntensity",
        "sTailBackLen", "sTailFwdLen",
        "mTailBackLen", "mTailFwdLen",
        "hTailBackLen", "hTailFwdLen",
        "motorSpeed", "motorAccel"
    ].forEach(bindRange);

    const bindColor = (id, key) => {
        const el = document.getElementById(id);
        if (el) el.onchange = () => send({ [key]: el.value });
    };

    // Ambient
    ambientEnabled.onchange = e => send({ ambientEnabled: e.target.checked });
    hourMarksEnabled.onchange = e => send({ hourMarksEnabled: e.target.checked });
    quarterMarksEnabled.onchange = e => send({ quarterMarksEnabled: e.target.checked });
    bindColor("ambientColorPicker", "ambientColor");
    bindColor("hourMarkColorPicker", "hourMarkColor");
    bindColor("quarterMarkColorPicker", "quarterMarkColor");

    // Time hands
    ["s", "m", "h"].forEach(p => {
        bindColor(`${p}HandCol`, `${p}HandCol`);
        bindColor(`${p}TailStartCol`, `${p}TailStartCol`);
        bindColor(`${p}TailFwdEndCol`, `${p}TailFwdEndCol`);
        bindColor(`${p}TailBackEndCol`, `${p}TailBackEndCol`);

        document.getElementById(`${p}TailFwdLen`).onchange = e => send({ [`${p}TailFwdLen`]: Number(e.target.value) });
        document.getElementById(`${p}TailBackLen`).onchange = e => send({ [`${p}TailBackLen`]: Number(e.target.value) });

        document.querySelectorAll(`.ring-mask-check[data-hand="${p}"]`).forEach(cb => {
            cb.onchange = () => send({ [`${p}RingMask`]: getRingMaskFromUI(p) });
        });
    });

    // Motor
    motorMode.onchange = e => {
        const mode = Number(e.target.value);
        updateMotorGridUI(mode, 0);
        send({ motorMode: mode, motorGrid: 0 });
    };
    ['motorGridH', 'motorGridM', 'motorGridS'].forEach(id => {
        document.getElementById(id).onchange = e => {
            const steps = Number(e.target.value);
            document.getElementById('motorGridOptionsPanel').style.display = steps > 0 ? 'block' : 'none';
            const gridParamsInfo = document.getElementById('motorInfoGridParams');
            if (gridParamsInfo) gridParamsInfo.style.display = steps > 0 ? 'list-item' : 'none';
            send({ motorGrid: steps });
        };
    });
    // Motor homing: jog, accept position, manual re-homing
    const bindJog = (id, steps) => {
        const btn = document.getElementById(id);
        if (btn) btn.onclick = () => send({ command: 'motorJog', value: steps });
    };
    bindJog('btnJogMinus100', -100);
    bindJog('btnJogMinus10',   -10);
    bindJog('btnJogMinus1',     -1);
    bindJog('btnJogPlus1',       1);
    bindJog('btnJogPlus10',     10);
    bindJog('btnJogPlus100',   100);
    const btnAccept = document.getElementById('btnAcceptPosition');
    if (btnAccept) btnAccept.onclick = () => send({ command: 'motorAcceptPosition' });
    const btnHoming = document.getElementById('btnStartHoming');
    if (btnHoming) btnHoming.onclick = () => send({ command: 'motorStartHoming' });

    // Night mode
    nightModeEnabled.onchange = e => send({ nightModeEnabled: e.target.checked });
    nightStart.onchange       = e => send({ nightStart:       timeStringToMinutes(e.target.value) });
    nightEnd.onchange         = e => send({ nightEnd:         timeStringToMinutes(e.target.value) });

    function getNightFeatures() {
        return (nightDimLeds.checked       ? 0x01 : 0) |
               (nightSfxOff.checked        ? 0x02 : 0) |
               (nightSecondHandOff.checked ? 0x04 : 0) |
               (nightMotorHome.checked     ? 0x08 : 0) |
               (nightMarkersOff.checked    ? 0x10 : 0);
    }
    const sendNightFeatures = () => send({ nightFeatures: getNightFeatures() });
    nightDimLeds.onchange = () => {
        document.getElementById('nightDimGroup').classList.toggle('visible', nightDimLeds.checked);
        sendNightFeatures();
    };
    nightSfxOff.onchange        = sendNightFeatures;
    nightSecondHandOff.onchange = sendNightFeatures;
    nightMotorHome.onchange     = sendNightFeatures;
    nightMarkersOff.onchange    = sendNightFeatures;

    // MQTT — send all fields together on "Apply & Save"
    document.getElementById('btnMqttSave').onclick = () => {
        const payload = {
            mqttEnabled: document.getElementById('mqttEnabled').checked,
            mqttBroker: document.getElementById('mqttBroker').value,
            mqttPort: Number(document.getElementById('mqttPort').value),
            mqttUser: document.getElementById('mqttUser').value,
            mqttClientId: document.getElementById('mqttClientId').value,
            mqttTopicBase: document.getElementById('mqttTopicBase').value,
        };
        // Only include password if the user actually typed something
        const pw = document.getElementById('mqttPassword').value;
        if (pw.length > 0) payload.mqttPassword = pw;
        send(payload);
    };

    // System
    const autoHomingCb = document.getElementById('motorAutoHoming');
    if (autoHomingCb) autoHomingCb.onchange = e => {
        send({ motorAutoHoming: e.target.checked });
        autoHomingActive = e.target.checked;
        const btn = document.getElementById('btnStartHoming');
        if (btn) btn.style.display = autoHomingActive ? 'inline-block' : 'none';
        const statusRow = document.getElementById('homingStatusRow');
        if (statusRow) statusRow.style.display = autoHomingActive ? 'flex' : 'none';
        const mode = parseInt(document.getElementById('motorMode')?.value || '0');
        const stepsValue = parseInt(document.getElementById('motorGrid')?.value || '0');
        updateMotorGridUI(mode, stepsValue);
    };
    logLevel.onchange   = e => send({ logLevel:   Number(e.target.value) });
    powerLimit.onchange = e => send({ powerLimit: Number(e.target.value) });
    document.getElementById('timezone').onchange = e => send({ timezone: e.target.value });

    btnSave.onclick = () => { if (confirm("Save settings to flash?")) send({ command: "save" }); };
    btnReboot.onclick = () => { if (confirm("Reboot device?")) send({ command: "reboot" }); };
    btnWifiErase.onclick = () => { if (confirm("Erase WiFi credentials and reboot?")) send({ command: "eraseWifi" }); };
    document.getElementById('btnOtaFirmware').onclick = () => window.open(`http://${location.hostname}:8080/update`, '_blank');
    

    // SFX triggers
    const bindTrigger = (id, cmd) => {
        const btn = document.getElementById(id);
        if (btn) btn.onclick = () => send({ command: cmd });
    };
    bindTrigger("btnSfxShortCircuitTrigger", "sfxShortCircuitTrigger");
    bindTrigger("btnSfxRadarTrigger", "sfxRadarTrigger");
    bindTrigger("btnSfxShootingStarTrigger", "sfxShootingStarTrigger");
    bindTrigger("btnSfxHeartbeatTrigger", "sfxHeartbeatTrigger");

    loadSettingsHttp();

    connectWS();
};
