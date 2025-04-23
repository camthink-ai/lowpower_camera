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
        timeSetHour: '00',
        timeSetMinute: '00',
        timeSetSecond: '00',

        // 定时抓拍列表数据
        timeCaptureList: [],
        timeIntervalNum: 8,
        capIntervalError: false,
        timeIntervalUnit: 1,
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
            return;
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
                });
            } catch (error) {
                this.alertErrMsg();
            }
            
            return;
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
