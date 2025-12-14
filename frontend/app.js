/**
 * RoboCup Control Panel - With Debug Mode
 * Controles de simulación, estado de ESPs y modo debugging
 */

class RoboCupApp {
    constructor() {
        this.socket = null;
        this.selectedScenario = 'striker';
        this.devices = new Map([['ESP_01', { connected: false, role: null }]]);
        this.isRunning = false;
        this.startTime = null;
        this.timerInterval = null;
        this.debugMode = false;
        this.maxLogs = 100;

        this.init();
    }

    init() {
        this.bindElements();
        this.bindEvents();
        this.connectSocket();
    }

    bindElements() {
        this.serverStatus = document.getElementById('serverStatus');
        this.scenarioGrid = document.getElementById('scenarioGrid');
        this.devicesList = document.getElementById('devicesList');
        this.addDeviceBtn = document.getElementById('addDeviceBtn');
        this.startBtn = document.getElementById('startBtn');
        this.stopBtn = document.getElementById('stopBtn');
        this.simStatus = document.getElementById('simStatus');
        this.simTime = document.getElementById('simTime');

        // Debug elements
        this.debugToggle = document.getElementById('debugToggle');
        this.debugPanel = document.getElementById('debugPanel');
        this.debugDeviceSelect = document.getElementById('debugDeviceSelect');
        this.debugLogs = document.getElementById('debugLogs');
        this.clearLogsBtn = document.getElementById('clearLogsBtn');
    }

    bindEvents() {
        this.scenarioGrid.addEventListener('click', e => {
            const btn = e.target.closest('.scenario-btn');
            if (btn) this.selectScenario(btn.dataset.scenario);
        });

        this.addDeviceBtn.addEventListener('click', () => this.addDevice());
        this.startBtn.addEventListener('click', () => this.startSimulation());
        this.stopBtn.addEventListener('click', () => this.stopSimulation());

        // Debug events
        this.debugToggle.addEventListener('click', () => this.toggleDebugMode());
        this.clearLogsBtn.addEventListener('click', () => this.clearLogs());

        // Command buttons
        document.querySelectorAll('.cmd-btn').forEach(btn => {
            btn.addEventListener('click', () => this.sendDebugCommand(btn.dataset.cmd));
        });
    }

    connectSocket() {
        const url = window.location.hostname === 'localhost'
            ? 'http://localhost:5001'
            : `http://${window.location.hostname}:5001`;

        this.socket = io(url, { reconnection: true });

        this.socket.on('connect', () => {
            this.serverStatus.classList.add('connected');
            this.serverStatus.querySelector('span:last-child').textContent = 'Server: Connected';
            this.addLog('Connected to server', 'info');
        });

        this.socket.on('disconnect', () => {
            this.serverStatus.classList.remove('connected');
            this.serverStatus.querySelector('span:last-child').textContent = 'Server: Disconnected';
            this.addLog('Disconnected from server', 'error');
        });

        this.socket.on('game/status', data => this.handleGameStatus(data));
        this.socket.on('device/status', data => this.handleDeviceStatus(data));

        // Debug events
        this.socket.on('player/log', data => this.handlePlayerLog(data));
        this.socket.on('system/log', data => this.addLog(data.msg, data.level?.toLowerCase() || 'info'));
    }

    // ============ Debug Mode ============

    toggleDebugMode() {
        this.debugMode = !this.debugMode;
        this.debugToggle.classList.toggle('active', this.debugMode);
        this.debugPanel.classList.toggle('visible', this.debugMode);

        if (this.debugMode) {
            this.addLog('Debug mode enabled', 'info');
        }
    }

    sendDebugCommand(cmd) {
        const deviceId = this.debugDeviceSelect.value;
        let params = [];

        switch (cmd) {
            case 'dash':
                params = [
                    parseFloat(document.getElementById('dashPower').value) || 100,
                    parseFloat(document.getElementById('dashDir').value) || 0
                ];
                break;
            case 'turn':
                params = [parseFloat(document.getElementById('turnAngle').value) || 45];
                break;
            case 'kick':
                params = [
                    parseFloat(document.getElementById('kickPower').value) || 100,
                    parseFloat(document.getElementById('kickDir').value) || 0
                ];
                break;
            case 'move':
                params = [
                    parseFloat(document.getElementById('moveX').value) || 0,
                    parseFloat(document.getElementById('moveY').value) || 0
                ];
                break;
        }

        const command = { device_id: deviceId, action: cmd, params };
        this.socket.emit('debug/command', command);
        this.addLog(`→ ${cmd.toUpperCase()}(${params.join(', ')}) → ${deviceId}`, 'cmd');
    }

    handlePlayerLog(data) {
        const type = data.type || 'info';
        const deviceId = data.device_id || '';
        const message = data.message || '';

        // Format based on type
        let prefix = deviceId ? `[${deviceId}] ` : '';
        this.addLog(`${prefix}${message}`, type);
    }

    addLog(message, type = 'info') {
        if (!this.debugLogs) return;

        // Remove "waiting" message on first log
        const waiting = this.debugLogs.querySelector('.log-entry:only-child');
        if (waiting && waiting.textContent === 'Waiting for logs...') {
            waiting.remove();
        }

        // Add timestamp
        const time = new Date().toLocaleTimeString('en-US', { hour12: false });

        const entry = document.createElement('div');
        entry.className = `log-entry log-${type}`;
        entry.textContent = `[${time}] ${message}`;

        this.debugLogs.appendChild(entry);

        // Limit log entries
        while (this.debugLogs.children.length > this.maxLogs) {
            this.debugLogs.removeChild(this.debugLogs.firstChild);
        }

        // Auto-scroll to bottom
        this.debugLogs.scrollTop = this.debugLogs.scrollHeight;
    }

    clearLogs() {
        if (this.debugLogs) {
            this.debugLogs.innerHTML = '<div class="log-entry log-info">Logs cleared</div>';
        }
    }

    updateDebugDeviceSelect() {
        if (!this.debugDeviceSelect) return;

        this.debugDeviceSelect.innerHTML = '';
        this.devices.forEach((_, id) => {
            const option = document.createElement('option');
            option.value = id;
            option.textContent = id;
            this.debugDeviceSelect.appendChild(option);
        });
    }

    // ============ Existing Methods ============

    selectScenario(scenario) {
        this.selectedScenario = scenario;
        this.scenarioGrid.querySelectorAll('.scenario-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.scenario === scenario);
        });

        // Auto-add second device for multi-player scenarios
        if ((scenario === 'passing' || scenario === 'goalkeeper') && this.devices.size < 2) {
            this.addDevice();
        }
    }

    addDevice() {
        const id = `ESP_${String(this.devices.size + 1).padStart(2, '0')}`;
        this.devices.set(id, { connected: false, role: null });
        this.renderDevices();
        this.updateDebugDeviceSelect();
    }

    renderDevices() {
        this.devicesList.innerHTML = '';
        this.devices.forEach((info, id) => {
            const el = document.createElement('div');
            el.className = 'device-item';
            el.dataset.device = id;
            el.innerHTML = `
                <span class="dot ${info.connected ? 'online' : 'offline'}"></span>
                <span class="device-name">${id}</span>
                <span class="device-role">${info.role || '-'}</span>
            `;
            this.devicesList.appendChild(el);
        });
    }

    startSimulation() {
        const roles = {};
        const roleMap = {
            'striker': 'STRIKER', 'dribbling': 'DRIBBLER', 'passing': 'PASSER',
            'goalkeeper': 'GOALKEEPER', 'defense': 'DEFENDER'
        };

        let i = 0;
        this.devices.forEach((_, id) => {
            if (this.selectedScenario === 'passing') {
                roles[id] = i === 0 ? 'PASSER' : 'RECEIVER';
            } else if (this.selectedScenario === 'goalkeeper') {
                roles[id] = i === 0 ? 'STRIKER' : 'GOALKEEPER';
            } else {
                roles[id] = roleMap[this.selectedScenario];
            }
            i++;
        });

        this.socket.emit('simulation/start', { type: this.selectedScenario, roles });

        this.isRunning = true;
        this.startBtn.disabled = true;
        this.stopBtn.disabled = false;
        this.simStatus.textContent = 'RUNNING';
        this.startTimer();
        this.addLog('Simulation started', 'info');
    }

    stopSimulation() {
        this.socket.emit('simulation/stop', {});
        this.isRunning = false;
        this.startBtn.disabled = false;
        this.stopBtn.disabled = true;
        this.simStatus.textContent = 'STOPPED';
        this.stopTimer();
        this.addLog('Simulation stopped', 'info');
    }

    handleGameStatus(data) {
        this.simStatus.textContent = data.state || 'UNKNOWN';
        if (data.state === 'FINISHED' || data.state === 'STOPPED') {
            this.isRunning = false;
            this.startBtn.disabled = false;
            this.stopBtn.disabled = true;
            this.stopTimer();
        }
    }

    handleDeviceStatus(data) {
        if (data.device_id && this.devices.has(data.device_id)) {
            this.devices.set(data.device_id, {
                connected: data.connected,
                role: data.role || null
            });
            this.renderDevices();
        }
    }

    startTimer() {
        this.startTime = Date.now();
        this.timerInterval = setInterval(() => {
            const s = Math.floor((Date.now() - this.startTime) / 1000);
            this.simTime.textContent = `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
        }, 1000);
    }

    stopTimer() {
        if (this.timerInterval) clearInterval(this.timerInterval);
    }
}

document.addEventListener('DOMContentLoaded', () => new RoboCupApp());

