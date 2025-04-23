import { translate as $t } from "../i18n";
import { getData, postData, URL } from "../api";
function Image() {
    return {
        // --Image Adjustment--
        supLight: 0,
        luminoSensity: 60,
        threshold: 58, // 光照阈值
        duty: 50, // 补光亮度
        startTimeHour: "23",
        startTimeMinute: "00",
        endTimeHour: "07",
        endTimeMinute: "00",
        supLightOption: [
            {
                label: $t("img.auto"),
                value: 0,
            },
            {
                label: $t("img.customize"),
                value: 1,
            },
            {
                label: $t("img.alwaysOn"),
                value: 2,
            },
            {
                label: $t("img.alwaysOff"),
                value: 3,
            },
        ],

        brightness: 0,
        contrast: 0,
        saturation: 0,
        aeLevel: 0,
        agcEnable: true,
        gainCeil: 3,
        gain: 15,
        flipHorEnable: false,
        flipVerEnable: false,
        MJPEG_URL: "",
        mountedVideo() {
            // "http://192.168.1.1:8080/api/v1/liveview/getJpegStream";
            const videoPort = 8080;
            const origin  = `${window.location.protocol}//${window.location.hostname}:${videoPort}`
            this.MJPEG_URL = origin + "/api/v1/liveview/getJpegStream";
        },

        lightMount: false,
        async getImageInfo() {
            const lightRes = await getData(URL.getLightParam);
            this.supLight = lightRes.lightMode; // 0 - auto 1 - customize 2 - ON 3 - OFF
            this.lightMount = true;
            this.luminoSensity = lightRes.value;
            this.threshold = lightRes.threshold;
            this.duty = lightRes.duty;
            this.startTimeHour = lightRes.startTime.split(":")[0];
            this.startTimeMinute = lightRes.startTime.split(":")[1];
            this.endTimeHour = lightRes.endTime.split(":")[0];
            this.endTimeMinute = lightRes.endTime.split(":")[1];

            const camRes = await getData(URL.getCamParam);
            this.brightness = camRes.brightness;

            this.contrast = camRes.contrast;
            this.saturation = camRes.saturation;
            this.aeLevel = camRes.aeLevel;
            this.agcEnable = camRes.bAgc ? true : false; // 1 true
            this.gainCeil = camRes.gainCeiling;
            this.gain = camRes.gain;
            this.flipHorEnable = camRes.bHorizonetal ? true : false;
            this.flipVerEnable = camRes.bVertical ? true : false;

            return Promise.resolve();
        },
        // 刷新光敏值
        async refreshLuminoSensity() {
            const { value } = await getData(URL.getLightParam);
            this.luminoSensity = value;
            return;
        },
        changeSupLight({ detail }) {
            this.supLight = detail.value;
            if (!detail.isInit) {
                this.setLightInfo();
            }
        },
        async setLightInfo() {
            await postData(URL.setLightParam, {
                lightMode: this.supLight,
                threshold: Number(this.threshold),
                duty: Number(this.duty),
                startTime: `${this.startTimeHour}:${this.startTimeMinute}`,
                endTime: `${this.endTimeHour}:${this.endTimeMinute}`,
            });
            return;
        },

        async setCamInfo() {
            await postData(URL.setCamParam, {
                brightness: Number(this.brightness),
                contrast: Number(this.contrast),
                saturation: Number(this.saturation),
                aeLevel: Number(this.aeLevel),
                bAgc: Number(this.agcEnable),
                gainCeiling: Number(this.gainCeil),
                gain: Number(this.gain),
                bHorizonetal: Number(this.flipHorEnable),
                bVertical: Number(this.flipVerEnable),
            });
            return;
        },

        async setImgAdjustDefault() {
            // this.duty = 50;
            this.brightness = 0;
            this.contrast = 0;
            this.saturation = 0;
            // 移除无用配置项
            // this.aeLevel = 0;
            // this.agcEnable = true;
            // this.gainCeil = 3;
            this.flipHorEnable = false;
            this.flipVerEnable = false;
            await this.setCamInfo();
            return;
        },

        /**
         * 根据不同的输入框执行不同的格式处理与请求
         * @param {string} type
         */
        inputLightTime(type) {
            switch (type) {
                case "startTimeHour":
                    this.startTimeHour =
                        this.startTimeHour == ""
                            ? "23"
                            : this.formatTimeNumber("hour", this.startTimeHour);
                    break;
                case "startTimeMinute":
                    this.startTimeMinute = this.formatTimeNumber(
                        "minute",
                        this.startTimeMinute
                    );
                    break;
                case "endTimeHour":
                    this.endTimeHour =
                        this.endTimeHour == ""
                            ? "07"
                            : this.formatTimeNumber("hour", this.endTimeHour);
                    break;
                case "endTimeMinute":
                    this.endTimeMinute = this.formatTimeNumber(
                        "minute",
                        this.endTimeMinute
                    );
                    break;
                default:
                    break;
            }
            // 当输入时间相同时，后面的时间增加一分钟
            if (
                this.startTimeHour == this.endTimeHour &&
                this.startTimeMinute == this.endTimeMinute
            ) {
                let str = this.increaseTime1Minute(
                    this.endTimeHour,
                    this.endTimeMinute
                );
                this.endTimeHour = str.split(":")[0];
                this.endTimeMinute = str.split(":")[1];
            }
            this.setLightInfo();
        },
        /**
         *
         * @param {string} type hour | minute
         * @return {string} 格式化后的时间
         */
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

        /**
         * 时间增加一分钟
         * @param {string|number} hour
         * @param {string|number} minute
         * @return {string} "hour:minute"
         */
        increaseTime1Minute(hour, minute) {
            minute = parseInt(minute);
            hour = parseInt(hour);
            let result = "00:00";
            if (minute < 59) {
                minute = minute + 1;
                result = `${hour.formatAddZero()}:${minute.formatAddZero()}`;
            } else if (hour < 23 && minute == 59) {
                hour = hour + 1;
                minute = "00";
                result = `${hour.formatAddZero()}:${minute}`;
            } else if (hour == 23 && minute == 59) {
                result = `00:00`;
            }
            return result;
        },

        changeSlider($el) {
            let percent = (($el.value - $el.min) / ($el.max - $el.min)) * 100;
            $el.style.background =
                "linear-gradient(to right, var(--primary-color), var(--primary-color) " +
                percent +
                "%, #f0f0f0 " +
                percent +
                "%)";
            return $el.value;
        },
    };
}

export default Image;
