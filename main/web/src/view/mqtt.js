import { getData, postData, URL, postFileBuffer, postMqttFile } from '../api';
import { translate as $t } from '../i18n';
import DialogUtil from '../components/dialog/util';
import Loading from '../components/loading';
let statusTimer = null;
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
        protocol: 0,
        protocolOptions: [
            {
                value: 0,
                label: 'MQTT',
            },
            {
                value: 1,
                label: 'MQTTS',
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
            /** 0未连接 1已连接 */
            isConnected: 0,
            tlsEnable: 0,
            caName: '',
            certName: '',
            keyName: '',
        },
        dataReportMount: false,

        // 文件上传相关
        fileType: 'ca', // 文件类型: ca, cert, key
        // 原始文件对象保存（用于后续上传）
        caFile: null,
        certFile: null,
        keyFile: null,

        // Dialog相关方法解构
        ...DialogUtil,

        // MQTTS
        changeSSLSwitch(event) {
            console.log('SSL switch changed:', event.target.checked);
            this.mqttPlatform.tlsEnable = event.target.checked ? 1 : 0;
            // 根据TLS开启与否，建议端口（不强制覆盖用户已填）
            if (!this.mqttPlatform.mqttPort || this.mqttPlatform.mqttPort === 0) {
                this.mqttPlatform.mqttPort = this.mqttPlatform.tlsEnable == 1 ? 8883 : 1883;
            }
        },
        handleBrowseCa() {
            this.MQTThandleBrowse('ca');
        },
        handleBrowseCert() {
            this.MQTThandleBrowse('cert');
        },
        handleBrowseKey() {
            this.MQTThandleBrowse('key');
        },
        MQTThandleBrowse(type) {
            this.fileType = type;
            const map = { ca: 'cafile', cert: 'certfile', key: 'keyfile' };;
            const el = document.getElementById(map[type]);
            if (el) {
                el.value = '';
                el.click();
            }
        },
        caFileChange() {
            try {
                const inputEl = document.getElementById("cafile");
                if (inputEl == null || inputEl.files.length == 0) return;
                const isValid = this.handleFileInput({ target: inputEl });
                if (!isValid) {
                    inputEl.value = '';
                    return;
                }
                const reader = new FileReader();
                reader.readAsArrayBuffer(inputEl.files[0]);
                reader.onload = () => {
                    Loading.show('Uploading...');
                    postMqttFile(URL.uploadMQTTCa, inputEl.files[0].name, reader.result).then(res => {
                        this.caFile = inputEl.files[0];
                        this.mqttPlatform.caName = inputEl.files[0].name;
                        this.alertMessage("success");
                    }).catch(error => {
                        this.alertMessage("error");
                    }).finally(() => {
                        Loading.hide();
                        inputEl.value = '';
                    })
                }
            } catch (error) {
                console.log('caFileChange error: ', error);
            }

        },
        certFileChange() {
            try {
                const inputEl = document.getElementById("certfile");
                console.log('inputEl: ', inputEl);
                if (inputEl == null || inputEl.files.length == 0) return;
                const isValid = this.handleFileInput({ target: inputEl });
                if (!isValid) {
                    inputEl.value = '';
                    return;
                }
                const reader = new FileReader();
                reader.readAsArrayBuffer(inputEl.files[0]);
                reader.onload = () => {
                    Loading.show('Uploading...');
                    postMqttFile(URL.uploadMQTTCert, inputEl.files[0].name, reader.result).then(res => {
                        this.certFile = inputEl.files[0];
                        this.mqttPlatform.certName = inputEl.files[0].name;
                        this.alertMessage("success");
                    }).catch(error => {
                        this.alertMessage("error");
                    }).finally(() => {
                        Loading.hide();
                        inputEl.value = '';
                    })
                }

            } catch (error) {
                console.log('certFileChange error: ', error);
            }

        },
        keyFileChange() {
            try {
                const inputEl = document.getElementById("keyfile");
                if (inputEl == null || inputEl.files.length == 0) return;
                const isValid = this.handleFileInput({ target: inputEl });
                if (!isValid) {
                    inputEl.value = '';
                    return;
                }
                const reader = new FileReader();
                reader.readAsArrayBuffer(inputEl.files[0]);
                reader.onload = () => {
                    Loading.show('Uploading...');
                    postMqttFile(URL.uploadMQTTKey, inputEl.files[0].name, reader.result).then(res => {
                        this.keyFile = inputEl.files[0];
                        this.mqttPlatform.keyName = inputEl.files[0].name;
                        this.alertMessage("success");
                    })
                    .catch(error => {
                        this.alertMessage("error");
                    })
                    .finally(() => {
                        Loading.hide();
                        inputEl.value = '';
                    })
                }
            } catch (error) {
                console.log('keyFileChange error: ', error);
            }
        },

        // 文件选择处理（带校验）
        handleFileInput(event) {
            const file = event && event.target && event.target.files && event.target.files[0];
            if (!file) return;

            const maxSizeMB = 512; // 最大512MB，实际可按需调整
            // 根据类型限制后缀
            const extMap = {
                ca: ['.pem', '.crt', '.cer'],
                cert: ['.pem', '.crt', '.cer', '.cert'],
                key: ['.key', '.pem']
            };
            const allowedExt = extMap[this.fileType] || ['.pem', '.crt', '.cer', '.der', '.key', '.cert'];
            const fileName = file.name || '';
            const lower = fileName.toLowerCase();
            const hasAllowedExt = allowedExt.some(ext => lower.endsWith(ext));

            // 空文件
            if (file.size === 0) {
                this.showTipsDialog($t('file.empty'), false);
                if (event && event.target) event.target.value = '';
                return false;
            }
            // 超大
            if (file.size > maxSizeMB * 1024 * 1024) {
                this.showTipsDialog($t('file.tooLarge', { size: maxSizeMB }), false);
                if (event && event.target) event.target.value = '';
                return false;
            }
            // 扩展名
            if (!hasAllowedExt) {
                this.showTipsDialog($t('file.invalidExt'), false);
                if (event && event.target) event.target.value = '';
                return false;
            }
            return true;
        },
        clearCa() {
            this.handleClearWithConfirm('ca');
        },
        clearCert() {
            this.handleClearWithConfirm('cert');
        },
        clearKey() {
            this.handleClearWithConfirm('key');
        },
        // 检查文件是否存在
        hasFile(type) {
            switch (type) {
                case 'ca':
                    return this.mqttPlatform.caName && this.mqttPlatform.caName.trim() !== '';
                case 'cert':
                    return this.mqttPlatform.certName && this.mqttPlatform.certName.trim() !== '';
                case 'key':
                    return this.mqttPlatform.keyName && this.mqttPlatform.keyName.trim() !== '';
                default:
                    return false;
            }
        },
        // 带确认对话框的清除文件处理
        handleClearWithConfirm(type) {
            if (this.hasFile(type)) {
                // 获取文件类型的中文描述
                let fileTypeName = '';
                switch (type) {
                    case 'ca':
                        fileTypeName = $t('mqtt.CaCert');
                        break;
                    case 'cert':
                        fileTypeName = $t('mqtt.cert');
                        break;
                    case 'key':
                        fileTypeName = $t('mqtt.key');
                        break;
                }

                // 显示确认对话框
                this.showTipsDialog(
                    $t('mqtt.confirmClear'),
                    true, // 显示取消按钮
                    () => {
                        try {
                            if (type == 'ca') {
                                // postData(URL.deleteMQTTCa)
                                try {
                                    postMqttFile(URL.deleteMQTTCa).then(res => {
                                        this.alertMessage("success");
                                    }).catch(error => {
                                        this.alertMessage("error");
                                    })
                                } catch (error) {
                                    console.log('handleClearWithConfirm error: ', error);
                                }   
                            } else if (type == 'cert') {
                                try {
                                    postMqttFile(URL.deleteMQTTCert).then(res => {
                                        this.alertMessage("success");
                                    }).catch(error => {
                                        this.alertMessage("error");
                                    })
                                } catch (error) {
                                    console.log('handleClearWithConfirm error: ', error);
                                }
                            } else if (type == 'key') {
                                try {
                                    postMqttFile(URL.deleteMQTTKey).then(res => {
                                        this.alertMessage("success");
                                    }).catch(error => {
                                        this.alertMessage("error");
                                    })
                                } catch (error) {
                                    console.log('handleClearWithConfirm error: ', error);
                                }
                            }
                        } catch (error) {
                            console.log('handleClearWithConfirm error: ', error);
                        } finally {
                            this.handleClear(type);
                        }
                    }
                );
            } else {
                this.handleClear(type);
            }
        },
        // 清除文件处理
        handleClear(type) {
            switch (type) {
                case 'ca':
                    this.mqttPlatform.caName = '';
                    this.caFile = null;
                    break;
                case 'cert':
                    this.mqttPlatform.certName = '';
                    this.certFile = null;
                    break;
                case 'key':
                    this.mqttPlatform.keyName = '';
                    this.keyFile = null;
                    break;
            }
        },


        async getDataReport() {
            const res = await getData(URL.getDataReport);
            // this.currentPlatformType = res.currentPlatformType;
            // this.sensingPlatform = { ...res.sensingPlatform };
            this.mqttPlatform = { ...res.mqttPlatform };
            // 兼容后端ssl为0/1，前端使用tlsEnable字段
            if (res.mqttPlatform && res.mqttPlatform.ssl !== undefined) {
                this.mqttPlatform.tlsEnable = res.mqttPlatform.ssl;
            }
            this.dataReportMount = true;
            this.updateMqttStatus()
            return;
        },
        /** 定时器间隔2s更新mqtt连接状态 TODO未调用销毁 */
        async updateMqttStatus() {
            if (statusTimer) return;
            const that = this;
            async function loop() {
                const res = await getData(URL.getDataReport);
                that.mqttPlatform.isConnected = res.mqttPlatform.isConnected;
                statusTimer = setTimeout(loop, 2000);
            }
            loop();
        },
        /** 销毁定时器 */
        async destroyMqttTimer() {
            if (statusTimer) {
                clearTimeout(statusTimer);
                statusTimer = null;
            }
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
            // 当开启TLS时做文件必填校验
            // if (this.mqttPlatform.tlsEnable == 1) {
            //     const hasCA = !!(this.caFile || (this.mqttPlatform.caName && this.mqttPlatform.caName.trim()))
            //     const hasCert = !!(this.certFile || (this.mqttPlatform.certName && this.mqttPlatform.certName.trim()))
            //     const hasKey = !!(this.keyFile || (this.mqttPlatform.keyName && this.mqttPlatform.keyName.trim()))
            //     if (!hasCA) {
            //         this.showTipsDialog($t('mqtt.needCaCert'), false)
            //         return;
            //     }
            //     if ((hasCert && !hasKey) || (!hasCert && hasKey)) {
            //         this.showTipsDialog($t('mqtt.needCertKeyPair'), false)
            //         return;
            //     }
            // }
            this.mqttPlatform.mqttPort = Number(this.mqttPlatform.mqttPort);
            // 提交时将tlsEnable映射为ssl
            const submitPlatform = { ...this.mqttPlatform };
            submitPlatform.ssl = this.mqttPlatform.tlsEnable;
            let data = {
                currentPlatformType: 1,
                mqttPlatform: submitPlatform
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
                this.alertMessage("success");
            } catch (error) {
                this.alertMessage("error");
            }
        },
        // --end--
    };
}

export default Mqtt;
