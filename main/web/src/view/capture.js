import { translate as $t } from '../i18n';
import { getData, postData, URL } from '../api';
function Capture() {
    return {
        // --Capture Setting--
        scheduledCaptureEnable: true,
        captureMode: 0,
        capOptions: [
            {
                value: 0,
                label: $t('cap.timedCap'),
            },
            {
                value: 1,
                label: $t('cap.intervalCap'),
            },
        ],
        timeSetDay: 7,
        timeDayOptions: [
            {
                label: $t('week.Daily'),
                value: 7,
            },
            {
                label: $t('week.Mon'),
                value: 1,
            },
            {
                label: $t('week.Tue'),
                value: 2,
            },
            {
                label: $t('week.Wed'),
                value: 3,
            },
            {
                label: $t('week.Thu'),
                value: 4,
            },
            {
                label: $t('week.Fri'),
                value: 5,
            },
            {
                label: $t('week.Sat'),
                value: 6,
            },
            {
                label: $t('week.Sun'),
                value: 0,
            },
        ],
        uploadMode: 0,
        uploadModeOptions: [
            {
                label: $t('cap.immediatelyUpload'),
                value: 0,
            },
            {
                label: $t('cap.scheduleUpload'),
                value: 1,
            },
        ],
        timeSetHour: '00',
        timeSetMinute: '00',
        timeSetSecond: '00',

        // scheduled capture list data
        timeCaptureList: [],
        timeIntervalNum: 8,
        capIntervalError: false,
        timeIntervalUnit: 1,
        intervalAnchorHour: '00',
        intervalAnchorMinute: '00',
        // camera open stabilization delay (ms)
        camWarmupMs: 5000,
        uploadTimeSetDay: 7,
        timeIntervalOptions: [
            {
                label: $t('cap.min'),
                value: 0,
            },
            {
                label: $t('cap.h'),
                value: 1,
            },
            {
                label: $t('cap.d'),
                value: 2,
            },
        ],
        capAlarmInEnable: true,
        capButtonEnable: true,
        captureMount: false,
        timeIntervalUnitMount: false,
        triggerMode: 1, // 1: alarm, 2: PIR (0 is disabled, but not shown in UI)
        triggerModeOptions: [
            {
                label: $t('cap.triggerModeAlarm'),
                value: 1,
            },
            {
                label: $t('cap.triggerModePIR'),
                value: 2,
            },
        ],
        pirSens: 15,        // Sensitivity: 0-255 (raw value, no conversion)
        pirBlind: 2.0,      // Blind time: seconds (display = (reg * 0.5) + 0.5)
        pirPulse: 2,        // Pulse count: times (display = reg + 1)
        pirWindow: 2.0,     // Window time: seconds (display = (reg * 2) + 2)
        triggerMount: false,
        savedTriggerMode: 1, // Save previous trigger mode when disabling

        // scheduled upload
        uploadTimeSetHour: '00',
        uploadTimeSetMinute: '00',
        uploadTimeSetSecond: '00',
        timeUploadList: [],
        uploadModeMount: false,
        timeUploadOptions: [
            {
                label: $t('cap.min'),
                value: 0,
            },
            {
                label: $t('cap.h'),
                value: 1,
            },
            {
                label: $t('cap.d'),
                value: 2,
            },
        ],
        timeUploadUnitMount: false,
        timeUploadRetryCount: 3,

      async changeUploadMode({ detail }) {
            this.uploadMode = detail.value;
            if (this.uploadMode == 1) {
                const ele = document.querySelector(
                    '.upload-time-list .error-input'
                );
                if (ele) {
                    ele.style.display = 'none';
                    this.uploadIntervalNum = 10;
                }
            }
            if (!detail.isInit) {
                this.setUploadInfo();
            }
        },
        addUploadTimeSetting() {
            this.timeUploadList.push({
                day: this.uploadTimeSetDay,
                time: `${this.uploadTimeSetHour}:${this.uploadTimeSetMinute}:${this.uploadTimeSetSecond}`,
            });
            this.setUploadInfo();
        },
        deleteUploadTimeSetting(index) {
            this.timeUploadList.splice(index, 1);
            this.setUploadInfo();
        },
        getTimeUploadDayLabel(dayValue) {
            return this.timeDayOptions.find((item) => item.value == dayValue)
                .label;
        },

        async getCaptureInfo() {
            try {
                const res = await getData(URL.getCapParam);
                this.scheduledCaptureEnable = res.bScheCap ? true : false;
                this.captureMode = res.scheCapMode; // 0: Timed Capture 1 : Interval Capture
                this.timeCaptureList = res.timedNodes;
                this.timeIntervalNum = res.intervalValue;
                this.timeIntervalUnit = res.intervalUnit;
                if (res.intervalAnchorTime && typeof res.intervalAnchorTime === 'string') {
                    const parts = res.intervalAnchorTime.split(':');
                    if (parts.length >= 2) {
                        this.intervalAnchorHour = this.formatTimeNumber('hour', parts[0]);
                        this.intervalAnchorMinute = this.formatTimeNumber('minute', parts[1]);
                    }
                }
                this.capAlarmInEnable = res.bAlarmInCap ? true : false;
                this.capButtonEnable = res.bButtonCap ? true : false;
                this.camWarmupMs = res.camWarmupMs || 5000;
                await this.getTriggerInfo();
            } catch (error) {
                console.error('Failed to get capture info:', error);
            } finally {
                this.captureMount = true;
                this.timeIntervalUnitMount = true;
            }
        },
        async getTriggerInfo() {
            try {
                const res = await getData(URL.getTriggerParam);
                // Only set trigger mode if trigger capture is enabled
                if (this.capAlarmInEnable) {
                    // Ensure trigger mode is valid (1 or 2, not 0)
                    // If server returns 0, use saved trigger mode if available, otherwise default to 1
                    const mode = Number(res.trigger_mode ?? 0);
                    if (mode > 0) {
                        this.triggerMode = mode;
                        // Update saved trigger mode when we get a valid value from server
                        this.savedTriggerMode = mode;
                    } else if (this.savedTriggerMode > 0) {
                        // Server returned 0, but we have a saved value, restore it
                        this.triggerMode = this.savedTriggerMode;
                    } else {
                        // No saved value, default to alarm mode
                        this.triggerMode = 1;
                        this.savedTriggerMode = 1;
                    }
                } else {
                    this.triggerMode = 0; // Disabled when trigger capture is off
                }
                // Convert register values to display values
                this.pirSens = res.sens || 15; // No conversion needed
                // Blind time: display = (reg * 0.5) + 0.5
                this.pirBlind = ((res.blind || 3) * 0.5) + 0.5;
                // Pulse count: display = reg + 1
                this.pirPulse = (res.pulse || 1) + 1;
                // Window time: display = (reg * 2) + 2
                this.pirWindow = ((res.window || 0) * 2) + 2;
                this.triggerMount = true;
            } catch (error) {
                console.error('Failed to get trigger info:', error);
                // On error, ensure triggerMount is set so UI can render
                this.triggerMount = true;
            }
        },
        async setTriggerInfo() {
            try {
                // Validate and convert display values to register values
                // Sensitivity: 0-255, recommended > 20, minimum 10 (no interference)
                let sens = Number(this.pirSens);
                if (isNaN(sens) || sens < 0) sens = 15;
                if (sens > 255) sens = 255;
                // Note: Values < 10 may cause false alarms, > 20 is recommended
                
                // Blind time: display range 0.5s ~ 8s, reg range 0-15
                // Formula: interrupt time = register value * 0.5s + 0.5s
                let blindDisplay = Number(this.pirBlind);
                if (isNaN(blindDisplay) || blindDisplay < 0.5) blindDisplay = 0.5;
                if (blindDisplay > 8) blindDisplay = 8;
                const blind = Math.round((blindDisplay - 0.5) * 2);
                
                // Pulse count: display range 1 ~ 4, reg range 0-3
                // Formula: pulse count = register value + 1
                let pulseDisplay = Number(this.pirPulse);
                if (isNaN(pulseDisplay) || pulseDisplay < 1) pulseDisplay = 1;
                if (pulseDisplay > 4) pulseDisplay = 4;
                const pulse = pulseDisplay - 1;
                
                // Window time: display range 2s ~ 8s, reg range 0-3
                // Formula: window time = register value * 2s + 2s
                let windowDisplay = Number(this.pirWindow);
                if (isNaN(windowDisplay) || windowDisplay < 2) windowDisplay = 2;
                if (windowDisplay > 8) windowDisplay = 8;
                const window = Math.round((windowDisplay - 2) / 2);
                
                await postData(URL.setTriggerParam, {
                    trigger_mode: Number(this.triggerMode),
                    sens: sens,
                    blind: blind,
                    pulse: pulse,
                    window: window,
                });
            } catch (error) {
                this.alertMessage("error");
            }
        },
        changeTriggerMode({ detail }) {
            this.triggerMode = detail.value;
            // Update saved trigger mode when user changes it
            if (this.triggerMode > 0) {
                this.savedTriggerMode = this.triggerMode;
            }
            if (!detail.isInit) {
                this.setTriggerInfo();
            }
        },
        async getUploadInfo() {
            const res = await getData(URL.getUploadParam);
            this.uploadMode = res.uploadMode;
            this.uploadModeMount = true;
            this.timeUploadList = res.timedNodes;
            this.timeUploadRetryCount = res.retryCount;
            console.log('this.uploadMode: ', this.uploadMode);
        },
        changeUploadTimeSetDay({ detail }) {
            this.uploadTimeSetDay = detail.value;
        },

        changeCapMode({ detail }) {
            this.captureMode = detail.value;
            if (this.captureMode == 1) {
                const ele = document.querySelector(
                    '.capture-interval-content .error-input'
                );
                if (ele) {
                    ele.style.display = 'none';
                    this.timeIntervalNum = 8;
                }
            }
            if (!detail.isInit) {
                this.setCaptureInfo();
            }
        },
        changeTimeSetDay({ detail }) {
            this.timeSetDay = detail.value;
        },
        changeIntervalUnit({ detail }) {
            this.timeIntervalUnit = detail.value;
            if (!detail.isInit) {
                this.setCaptureInfo();
            }
        },

        async setCaptureInfo() {
            try {
                await postData(URL.setCapParam, {
                    bScheCap: Number(this.scheduledCaptureEnable),
                    scheCapMode: Number(this.captureMode),
                    timedNodes: this.timeCaptureList,
                    timedCount: this.timeCaptureList.length,
                    intervalValue: Number(this.timeIntervalNum),
                    intervalUnit: Number(this.timeIntervalUnit),
                    intervalAnchorTime: `${this.intervalAnchorHour}:${this.intervalAnchorMinute}`,
                    bAlarmInCap: Number(this.capAlarmInEnable),
                    bButtonCap: Number(this.capButtonEnable),
                    camWarmupMs: Number(this.camWarmupMs),
                });
                // If trigger capture is disabled, save current trigger mode and set to 0
                if (!this.capAlarmInEnable) {
                    // Save current trigger mode before disabling
                    if (this.triggerMode > 0) {
                        this.savedTriggerMode = this.triggerMode;
                    }
                    this.triggerMode = 0;
                    await this.setTriggerInfo();
                } else {
                    // If trigger capture is re-enabled, restore saved trigger mode first
                    if (this.triggerMode === 0 && this.savedTriggerMode > 0) {
                        this.triggerMode = this.savedTriggerMode;
                    }
                    // Then reload trigger info to get latest values from server
                    await this.getTriggerInfo();
                }
            } catch (error) {
                this.alertMessage("error");
            }
            
            return;
        },
        async setUploadInfo() {
            try {
                await postData(URL.setUploadParam, {
                    uploadMode: Number(this.uploadMode),
                    timedNodes: this.timeUploadList,
                    timedCount: this.timeUploadList.length,
                    retryCount: Number(this.timeUploadRetryCount),
                });
            } catch (error) {
                this.alertMessage("error");
            }
        },
        getTimeCapDayLabel(dayValue) {
            return this.timeDayOptions.find((item) => item.value == dayValue)
                .label;
        },
        inputCaptureTime(type) {
            console.log('this.timeSetHour:', this.timeSetHour);
            switch (type) {
                case 'timeSetHour':
                    this.timeSetHour = this.formatTimeNumber(
                        'hour',
                        this.timeSetHour
                    );
                    break;
                case 'timeSetMinute':
                    this.timeSetMinute = this.formatTimeNumber(
                        'minute',
                        this.timeSetMinute
                    );
                    break;
                case 'timeSetSecond':
                    this.timeSetSecond = this.formatTimeNumber(
                        'minute',
                        this.timeSetSecond
                    );
                    break;
                default:
                    break;
            }
        },
        inputIntervalAnchorTime(type) {
            switch (type) {
                case 'intervalAnchorHour':
                    this.intervalAnchorHour = this.formatTimeNumber('hour', this.intervalAnchorHour);
                    break;
                case 'intervalAnchorMinute':
                    this.intervalAnchorMinute = this.formatTimeNumber('minute', this.intervalAnchorMinute);
                    break;
                default:
                    break;
            }
            this.setCaptureInfo();
        },
        inputUploadTime(type) {
            switch (type) {
                case 'uploadTimeSetHour':
                    this.uploadTimeSetHour = this.formatTimeNumber(
                        'hour',
                        this.uploadTimeSetHour
                    );
                    break;
                case 'uploadTimeSetMinute':
                    this.uploadTimeSetMinute = this.formatTimeNumber(
                        'minute',
                        this.uploadTimeSetMinute
                    );
                    break;
                case 'uploadTimeSetSecond':
                    this.uploadTimeSetSecond = this.formatTimeNumber(
                        'minute',
                        this.uploadTimeSetSecond
                    );
                    break;
                default:
                    break;
            }
        },
        formatTimeNumber(type, rawNum) {
            let result = "";
            let maxNum = type == "hour" ? 23 : 59;
            if (rawNum == '' || rawNum <= 0) {
                result = "00";
            } else if (rawNum > maxNum) {
                result = maxNum;
            } else {
                result = parseInt(rawNum).formatAddZero();
            }
            return result;
        },
        /** validate interval time */
        checkIntervalNum() {
            if (
                this.checkRequired(this.timeIntervalNum) &&
                this.checkNumberRange(this.timeIntervalNum, 1, 999)
            ) {
                this.capIntervalError = false;
                return true;
            } else {
                this.capIntervalError = true;
                return false;
            }
        },
        /** capture interval time input box blur */
        inputCapInterNum() {
            if (this.checkIntervalNum()) {
                this.timeIntervalNum = parseInt(this.timeIntervalNum);
                this.setCaptureInfo();
            }
        },

        addTimeSetting() {
            this.timeCaptureList.push({
                day: this.timeSetDay,
                time: `${this.timeSetHour}:${this.timeSetMinute}:${this.timeSetSecond}`,
            });
            this.setCaptureInfo();
        },
        deleteTimeSetting(index) {
            this.timeCaptureList.splice(index, 1);
            this.setCaptureInfo();
        },
        // Validate and clamp PIR sensitivity (0-255, recommended > 20, minimum 10)
        validatePirSens() {
            let val = Number(this.pirSens);
            if (isNaN(val) || val < 0) val = 15;
            if (val > 255) val = 255;
            // Note: Values < 10 may cause false alarms, > 20 is recommended
            this.pirSens = val;
            this.setTriggerInfo();
        },
        // Validate and clamp PIR blind time (0.5s ~ 8s)
        validatePirBlind() {
            let val = Number(this.pirBlind);
            if (isNaN(val) || val < 0.5) val = 0.5;
            if (val > 8) val = 8;
            this.pirBlind = val;
            this.setTriggerInfo();
        },
        // Validate and clamp PIR pulse count (1 ~ 4)
        validatePirPulse() {
            let val = Number(this.pirPulse);
            if (isNaN(val) || val < 1) val = 1;
            if (val > 4) val = 4;
            this.pirPulse = val;
            this.setTriggerInfo();
        },
        // Validate and clamp PIR window time (2s ~ 8s)
        validatePirWindow() {
            let val = Number(this.pirWindow);
            if (isNaN(val) || val < 2) val = 2;
            if (val > 8) val = 8;
            this.pirWindow = val;
            this.setTriggerInfo();
        },
    };
}

export default Capture;
