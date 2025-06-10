import { getData, postData, URL } from '../api';
import { translate as $t } from '../i18n';
function Mqtt() {
    return {
        // --MQTT Post--
        mqttHostError: false,
        mqttPortError: false,
        httpPortError: false,
        mqttTopicError: false,

        // --- New ---
        currentPlatformType: 1,
        platformOptions: [
            {
                value: 0,
                label: $t('mqtt.sensingPlatform'),
            },
            {
                value: 1,
                label: $t('mqtt.otherMqttPlatform'),
            },
        ],
        qosOptions: [
            {
                value: 0,
                label: 'QoS 0',
            },
            {
                value: 1,
                label: 'QoS 1',
            },
            {
                value: 2,
                label: 'QoS 2',
            },
        ],
        sensingPlatform: {
            host: '192.168.1.1',
            mqttPort: 1883,
            httpPort: 5220,
        },
        mqttPlatform: {
            host: '192.168.1.1',
            mqttPort: 1883,
            topic: 'NE101SensingCam/Snapshot',
            clientId: '6622123145647890',
            qos: 0,
            username: '',
            password: '',
        },
        dataReportMount: false,
        async getDataReport() {
            const res = await getData(URL.getDataReport);
            // this.currentPlatformType = res.currentPlatformType;
            // this.sensingPlatform = { ...res.sensingPlatform };
            this.mqttPlatform = { ...res.mqttPlatform };
            this.dataReportMount = true;
            return;
        },
        changePlatform({ detail }) {
            // this.currentPlatformType = detail.value;
        },
        changeQos({ detail }) {
            this.mqttPlatform.qos = detail.value;
        },
        inputMqttHost() {
            if (this.checkRequired(this.mqttPlatform.host)) {
                this.mqttHostError = false;
            } else {
                this.mqttHostError = true;
            }
        },
        inputMqttPort() {
            if (
                this.checkRequired(this.mqttPlatform.mqttPort) &&
                this.checkNumberRange(this.mqttPlatform.mqttPort, 1, 65535)
            ) {
                this.mqttPortError = false;
            } else {
                this.mqttPortError = true;
            }
        },
        inputHttpPort() {
            if (
                this.checkRequired(this.sensingPlatform.httpPort) &&
                this.checkNumberRange(this.sensingPlatform.httpPort, 1, 65535)
            ) {
                this.httpPortError = false;
            } else {
                this.httpPortError = true;
            }
        },
        inputMqttTopic() {
            let val = this.mqttPlatform.topic;
            if (this.checkRequired(val)) {
                this.mqttTopicError = false;
            } else {
                this.mqttTopicError = true;
            }
        },
        checkRequired(val) {
            if (val.toString().trim() == '') {
                return false;
            } else {
                return true;
            }
        },
        checkNumberRange(val, min, max) {
            let num = Number(val);
            // console.log('num: ', num);
            if (num >= min && num <= max) {
                return true;
            } else {
                return false;
            }
        },
        checkTopic(val) {
            // 数字、字母和字符“/"
            if (/^[\dA-Za-z\/]+$/.test(val)) {
                return true;
            } else {
                return false;
            }
        },

        /**
         * 文本输入框判断必填项，并控制非法提示的显示
         * @param {*} $el
         * @returns 校验合法true 非法false
         */
        validateEmpty($el) {
            const elError = $el.parentElement.children.namedItem('error-tip');
            if (this.checkRequired($el.value)) {
                elError.style.display = 'none';
                return true;
            } else {
                elError.style.display = '';
                return false;
            }
        },
        /**
         * 关闭MQTT推送
         * @param {*} val
         */
        changeMqttSwitch(val) {
            this.clearMqttValidate();
            if (!val) {
                this.saveMqttInfo();
            }
        },
        /**
         * 对MQTT表单内的文本输入框触发失焦校验
         * @returns {Boolean} 合法true
         */
        validateMqttForm() {
            const list = document.querySelectorAll('.mqtt-card input[type=text], .mqtt-card textarea');
            list.forEach((element) => {
                element.focus();
                element.blur();
            });
            // 只要存在一个非法，则返回false
            const errEl = document.querySelectorAll('.mqtt-card div.error-input');
            for (const element of errEl) {
                if (element.style.display != 'none') {
                    return false;
                }
            }
            return true;
        },
        /**
         * 清除MQTT表单校验结果
         */
        clearMqttValidate() {
            this.mqttHostError = false;
            this.mqttPortError = false;
            this.mqttTopicError = false;
        },
        async setDataReport() {
            // 校验MQTT表单合法性
            if (!this.validateMqttForm()) {
                return;
            }
            this.mqttPlatform.mqttPort = Number(this.mqttPlatform.mqttPort);
            let data = {
                currentPlatformType: 1,
                mqttPlatform: this.mqttPlatform
            };
            // if (this.currentPlatformType == 0) {
            //     this.sensingPlatform.httpPort = Number(this.sensingPlatform.httpPort);
            //     data.sensingPlatform = { ...this.sensingPlatform };
            // } else if (this.currentPlatformType == 1) {
                // Host和Mqtt Port值共用一个
                // this.mqttPlatform.host = this.sensingPlatform.host;
                // this.mqttPlatform.mqttPort = this.sensingPlatform.mqttPort;
                // data.mqttPlatform = { ...this.mqttPlatform };
            // }
            try {
                await postData(URL.setDataReport, data);
            } catch (error) {
                this.alertErrMsg();
            }
        },
        // --end--
    };
}

export default Mqtt;
