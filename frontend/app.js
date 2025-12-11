/**
 * RoboCup Control Panel - Simplified
 * Solo controles de simulación y estado de ESPs
 */

class RoboCupApp {
    constructor() {
        this.socket = null;
        this.selectedScenario = 'striker';
        this.devices = new Map([['ESP_01', { connected: false, role: null }]]);
        this.isRunning = false;
        this.startTime = null;
        this.timerInterval = null;

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
    }

    bindEvents() {
        this.scenarioGrid.addEventListener('click', e => {
            const btn = e.target.closest('.scenario-btn');
            if (btn) this.selectScenario(btn.dataset.scenario);
        });

        this.addDeviceBtn.addEventListener('click', () => this.addDevice());
        this.startBtn.addEventListener('click', () => this.startSimulation());
        this.stopBtn.addEventListener('click', () => this.stopSimulation());
    }

    connectSocket() {
        const url = window.location.hostname === 'localhost'
            ? 'http://localhost:5001'
            : `http://${window.location.hostname}:5001`;

        this.socket = io(url, { reconnection: true });

        this.socket.on('connect', () => {
            this.serverStatus.classList.add('connected');
            this.serverStatus.querySelector('span:last-child').textContent = 'Server: Connected';
        });

        this.socket.on('disconnect', () => {
            this.serverStatus.classList.remove('connected');
            this.serverStatus.querySelector('span:last-child').textContent = 'Server: Disconnected';
        });

        this.socket.on('game/status', data => this.handleGameStatus(data));
        this.socket.on('device/status', data => this.handleDeviceStatus(data));
    }

    selectScenario(scenario) {
        this.selectedScenario = scenario;
        this.scenarioGrid.querySelectorAll('.scenario-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.scenario === scenario);
        });
    }

    addDevice() {
        const id = `ESP_${String(this.devices.size + 1).padStart(2, '0')}`;
        this.devices.set(id, { connected: false, role: null });
        this.renderDevices();
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
    }

    stopSimulation() {
        this.socket.emit('simulation/stop', {});
        this.isRunning = false;
        this.startBtn.disabled = false;
        this.stopBtn.disabled = true;
        this.simStatus.textContent = 'STOPPED';
        this.stopTimer();
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
