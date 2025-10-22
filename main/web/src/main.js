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
    /** MJPEG加载错误提示 */
    alertErrMsg(event) {
        // 避免onerror死循环
        if (event && event.target) {
            event.target.onerror = null;
        }
        // 弹出网络错误提示
        this.showTipsDialog($t('networkError'));
    },
    /**
     * Init Request Data
     * 由于应用层目前httpserver无法支持异步，因此视频流独立一个server，其他配置请求独立一个server
     * 且不能用Promise.all并发请求，容易导致server崩溃
     */
    async getInitData() {
        await this.setDevTime(); // 首先同步时间
        await this.getDeviceInfo();
        await this.getImageInfo();
        await this.getCaptureInfo();
        await this.getUploadInfo();
        await this.getDataReport();
        if (this.netmod === 'cat1') {
            await this.getCellularInfo();
        } else {
            await this.getWlanInfo();
        }
        
        this.justifyAllArea();
    },

    // 多语言翻译
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

    /** textarea高度自适应 */
    justifyAllArea() {
        const that = this;
        document.querySelectorAll('textarea').forEach((item) => that.justifyAreaHeight(item));
    },
    justifyAreaHeight($el) {
        $el.style.height = '30px';
        // 单行时scrollHeight由于border会减小为28,此处优化
        $el.style.height = ($el.scrollHeight <= '28' ? '30' : $el.scrollHeight) + 'px';
    },

    /** 数字输入限制 */
    inputNumLimit(name) {
        const tmpValue = this[name].toString().replace(/[^\d]/g, '');
        nextTick(() => {
            this[name] = tmpValue;
        });
    },

    /** 获取当前浏览器时区的时区代码 */
    getTimeZoneCode() {
        const timeZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
        return timeZoneOptions[timeZone];
    },
    async setDevTime() {
        await postData(URL.setDevTime, {
            tz: this.getTimeZoneCode(),
            ts: Math.floor(Date.now() / 1000),  // 使用秒级时间戳
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

    // 导入其他功能模块的方法
    ...Image(),
    ...Capture(),
    ...Mqtt(),
    ...Device(),
    ...Wlan(),
    ...Cellular(),
};

createApp({
    ...App,
    MsSelect,
    MsDialog,
    ...DialogUtil,
}).mount('.container');
