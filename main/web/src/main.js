import { translate as $t, getCurLang } from './i18n';
import { createApp, nextTick } from '/src/lib/petite-vue.es.js';
import { postData, URL } from './api';
import MsDialog from './components/dialog';
import DialogUtil from './components/dialog/util';
import MsSelect from './components/select';
import { timeZoneOptions } from './utils';
import Image from './view/image';
import Capture from './view/capture';
import Mqtt from './view/mqtt';
import Device from './view/device';
import Wlan from './view/wlan';
import Cellular from './view/cellular';
import Debug from './view/debug';

const App = {
    name: 'App',
    showMsg: {
        enable: false,
        type: "error"
    },
    /**
     * 全局提示消息框
     * @param type "succes" | "error"
     */
    alertMessage(type = "error") {
        this.showMsg.enable = true;
        this.showMsg.type = type;
        setTimeout(() => {
            this.showMsg.enable = false;
        }, 5000);
    },
    /** MJPEG load error tip */
    alertErrMsg(event) {
        // avoid onerror infinite loop
        if (event && event.target) {
            event.target.onerror = null;
        }
        // show network error tip
        this.showTipsDialog($t('networkError'));
    },
    /**
     * Init Request Data
     * since application layer httpserver currently cannot support async, video stream uses one independent server, other config requests use another independent server
     * and cannot use Promise.all for concurrent requests, easily causes server crash
     */
    async getInitData() {
        await this.setDevTime(); // first sync time
        await this.getDeviceInfo();
        await this.getImageInfo();
        await this.getCaptureInfo(); // This will also call getTriggerInfo internally
        await this.getUploadInfo();
        await this.getDataReport();
        if (this.netmod === 'cat1') {
            await this.getCellularInfo();
        } else {
            await this.getWlanInfo();
        }
        
        this.justifyAllArea();
    },

    // multi-language translation
    $t(str) {
        return $t(str);
    },
    local: getCurLang(),
    languageOptions: [
        {
            label: 'English',
            value: 'en_US',
        },
        {
            label: '中文',
            value: 'zh_CN',
        },
    ],
    changeLanguage({ detail }) {
        this.local = detail.value;
        localStorage.setItem('lang', detail.value);
        window.location.reload();
    },

    /** textarea height auto-adapt */
    justifyAllArea() {
        const that = this;
        document.querySelectorAll('textarea').forEach((item) => that.justifyAreaHeight(item));
    },
    justifyAreaHeight($el) {
        $el.style.height = '30px';
        // when single line, scrollHeight decreases to 28 due to border, optimize here
        $el.style.height = ($el.scrollHeight <= '28' ? '30' : $el.scrollHeight) + 'px';
    },

    /** number input limit */
    inputNumLimit(name) {
        const tmpValue = this[name].toString().replace(/[^\d]/g, '');
        nextTick(() => {
            this[name] = tmpValue;
        });
    },
    /** decimal number input limit */
    inputDecimalLimit(name) {
        let tmpValue = this[name].toString();
        // Remove all non-digit and non-dot characters
        tmpValue = tmpValue.replace(/[^\d.]/g, '');
        // Replace multiple dots with single dot
        tmpValue = tmpValue.replace(/\.{2,}/g, '.');
        // If starts with dot, add 0 before it
        if (tmpValue.startsWith('.')) {
            tmpValue = '0' + tmpValue;
        }
        // Remove dots after first one
        const parts = tmpValue.split('.');
        if (parts.length > 2) {
            tmpValue = parts[0] + '.' + parts.slice(1).join('');
        }
        nextTick(() => {
            this[name] = tmpValue;
        });
    },

    /** get timezone code of current browser timezone */
    getTimeZoneCode() {
        const timeZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
        return timeZoneOptions[timeZone];
    },
    async setDevTime() {
        await postData(URL.setDevTime, {
            tz: this.getTimeZoneCode(),
            ts: Math.floor(Date.now() / 1000),  // use second-level timestamp
        });
        return;
    },

    handleSleepMode() {
        this.showTipsDialog($t('sleerModeTips'), true, this.changeSleepMode);
    },
    changeSleepMode() {
        postData(URL.setDevSleep).then((res) => {
            console.log(res);
        });
    },

    // import methods from other function modules
    ...Image(),
    ...Capture(),
    ...Mqtt(),
    ...Device(),
    ...Wlan(),
    ...Cellular(),
    ...Debug(),
};

createApp({
    ...App,
    MsSelect,
    MsDialog,
    ...DialogUtil,
}).mount('.container');
