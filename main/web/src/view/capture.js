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

        // 定时抓拍列表数据
        timeCaptureList: [],
        timeIntervalNum: 8,
        capIntervalError: false,
        timeIntervalUnit: 1,
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

        // 定时上传
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
            const res = await getData(URL.getCapParam);
            this.scheduledCaptureEnable = res.bScheCap ? true : false;
            this.captureMode = res.scheCapMode; // 0: Timed Capture 1 : Interval Capture
            this.captureMount = true;
            this.timeCaptureList = res.timedNodes;
            this.timeIntervalNum = res.intervalValue;
            this.timeIntervalUnit = res.intervalUnit;
            this.timeIntervalUnitMount = true;
            this.capAlarmInEnable = res.bAlarmInCap ? true : false;
            this.capButtonEnable = res.bButtonCap ? true : false;
            this.camWarmupMs = res.camWarmupMs || 5000;
            return;
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
            this.setCaptureInfo();
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
                    bAlarmInCap: Number(this.capAlarmInEnable),
                    bButtonCap: Number(this.capButtonEnable),
                    camWarmupMs: Number(this.camWarmupMs),
                });
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
        /** 校验间隔时间 */
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
        /** 抓拍间隔时间输入框失焦 */
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
    };
}

export default Capture;
