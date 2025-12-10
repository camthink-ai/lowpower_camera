import { translate as $t } from "../i18n";
import { getData, postData, URL } from "../api";
function Image() {
    return {
        // --Image Adjustment--
        supLight: 0,
        luminoSensity: 60,
        threshold: 58, // light threshold
        duty: 50, // fill light brightness
        hdrEnable: false, // HDR enable/disable for USB camera
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

        frameSize: 14, // default FRAMESIZE_FHD (1920x1080)
        frameSizeMount: false, // whether resolution data has been loaded
        frameSizeOption: [
            {
                label: "320×240",
                value: 5, // FRAMESIZE_QVGA
            },
            {
                label: "640×480",
                value: 8, // FRAMESIZE_VGA
            },
            {
                label: "800×600",
                value: 9, // FRAMESIZE_SVGA
            },
            {
                label: "1024×768",
                value: 10, // FRAMESIZE_XGA
            },
            {
                label: "1280×720",
                value: 11, // FRAMESIZE_HD
            },
            {
                label: "1280×1024",
                value: 12, // FRAMESIZE_SXGA
            },
            {
                label: "1600×1200",
                value: 13, // FRAMESIZE_UXGA
            },
            {
                label: "1920×1080",
                value: 14, // FRAMESIZE_FHD
            },
            {
                label: "2048×1536",
                value: 17, // FRAMESIZE_QXGA
            },
            {
                label: "2560×1920",
                value: 21, // FRAMESIZE_QSXGA
            },
        ],
        quality: 12, // image quality (0-63, higher value means lower quality)
        brightness: 0,
        contrast: 0,
        saturation: 0,
        aeLevel: 0,
        agcEnable: true,
        gainCeil: 3,
        gain: 15,
        flipHorEnable: true,
        flipVerEnable: true,
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
            this.frameSize = camRes.frameSize;
            this.frameSizeMount = true;
            this.quality = camRes.quality;
            
            // Get HDR status for USB camera
            this.hdrEnable = camRes.hdrEnable ? true : false;
            return Promise.resolve();
        },
        // refresh light sensitivity value
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
                frameSize: Number(this.frameSize),
                quality: Number(this.quality),
                hdrEnable: Number(this.hdrEnable),
            });
            return;
        },

        changeFrameSize({ detail }) {
            this.frameSize = detail.value;
            if (!detail.isInit) {
                this.setCamInfo();
            }
        },
        changeQuality() {
            // limit quality value to range 0-63
            if (this.quality < 0) {
                this.quality = 0;
            } else if (this.quality > 63) {
                this.quality = 63;
            }
            this.setCamInfo();
        },

        async setImgAdjustDefault() {
            // this.duty = 50;
            this.brightness = 0;
            this.contrast = 0;
            this.saturation = 0;
            // remove unused configuration items
            // this.aeLevel = 0;
            // this.agcEnable = true;
            // this.gainCeil = 3;
            this.flipHorEnable = false;
            this.flipVerEnable = false;
            await this.setCamInfo();
            return;
        },

        /**
         * execute different format processing and requests based on different input boxes
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
            // when input times are the same, add one minute to the later time
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
         * @return {string} formatted time
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
         * add one minute to time
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
