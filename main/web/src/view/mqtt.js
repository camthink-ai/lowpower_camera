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
            /** 0 not connected 1 connected */
            isConnected: 0,
            tlsEnable: 0,
            caName: '',
            certName: '',
            keyName: '',
        },
        dataReportMount: false,

        // file upload related
        fileType: 'ca', // file type: ca, cert, key
        // save original file object (for subsequent upload)
        caFile: null,
        certFile: null,
        keyFile: null,

        // Dialog相关方法解构
        ...DialogUtil,

        // MQTTS
        changeSSLSwitch(event) {
            console.log('SSL switch changed:', event.target.checked);
            this.mqttPlatform.tlsEnable = event.target.checked ? 1 : 0;
            // suggest port based on whether TLS is enabled (do not force override user input)
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

        // file selection handling (with validation)
        handleFileInput(event) {
            const file = event && event.target && event.target.files && event.target.files[0];
            if (!file) return;

            const maxSizeMB = 512; // maximum 512MB, can be adjusted as needed
            // limit extension by type
            const extMap = {
                ca: ['.pem', '.crt', '.cer'],
                cert: ['.pem', '.crt', '.cer', '.cert'],
                key: ['.key', '.pem']
            };
            const allowedExt = extMap[this.fileType] || ['.pem', '.crt', '.cer', '.der', '.key', '.cert'];
            const fileName = file.name || '';
            const lower = fileName.toLowerCase();
            const hasAllowedExt = allowedExt.some(ext => lower.endsWith(ext));

            // empty file
            if (file.size === 0) {
                this.showTipsDialog($t('file.empty'), false);
                if (event && event.target) event.target.value = '';
                return false;
            }
            // file too large
            if (file.size > maxSizeMB * 1024 * 1024) {
                this.showTipsDialog($t('file.tooLarge', { size: maxSizeMB }), false);
                if (event && event.target) event.target.value = '';
                return false;
            }
            // extension
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
        // check if file exists
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
        // clear file handling with confirmation dialog
        handleClearWithConfirm(type) {
            if (this.hasFile(type)) {
                // get file type description
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

                // show confirmation dialog
                this.showTipsDialog(
                    $t('mqtt.confirmClear'),
                    true, // show cancel button
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
        // clear file handling
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
            // compatible with backend ssl 0/1, frontend uses tlsEnable field
            if (res.mqttPlatform && res.mqttPlatform.ssl !== undefined) {
                this.mqttPlatform.tlsEnable = res.mqttPlatform.ssl;
            }
            this.dataReportMount = true;
            this.updateMqttStatus()
            return;
        },
        /** timer interval 2s update mqtt connection status TODO not called destroy */
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
        /** destroy timer */
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
            // numbers, letters and character "/"
            if (/^[\dA-Za-z\/]+$/.test(val)) {
                return true;
            } else {
                return false;
            }
        },

        /**
         * text input box validate required fields and control illegal tip display
         * @param {*} $el
         * @returns valid true invalid false
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
         * close MQTT push
         * @param {*} val
         */
        changeMqttSwitch(val) {
            this.clearMqttValidate();
            if (!val) {
                this.saveMqttInfo();
            }
        },
        /**
         * trigger blur validation for text input boxes in MQTT form
         * @returns {Boolean} valid true
         */
        validateMqttForm() {
            const list = document.querySelectorAll('.mqtt-card input[type=text], .mqtt-card textarea');
            list.forEach((element) => {
                element.focus();
                element.blur();
            });
            // if any invalid exists, return false
            const errEl = document.querySelectorAll('.mqtt-card div.error-input');
            for (const element of errEl) {
                if (element.style.display != 'none') {
                    return false;
                }
            }
            return true;
        },
        /**
         * clear MQTT form validation results
         */
        clearMqttValidate() {
            this.mqttHostError = false;
            this.mqttPortError = false;
            this.mqttTopicError = false;
        },
        async setDataReport() {
            // validate MQTT form legality
            if (!this.validateMqttForm()) {
                return;
            }
            // when TLS is enabled, validate required files
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
            // map tlsEnable to ssl when submitting
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
            // Host and Mqtt Port values share one
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
