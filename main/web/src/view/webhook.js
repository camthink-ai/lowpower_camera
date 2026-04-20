import { getData, postData, URL } from '../api';
import { translate as $t } from '../i18n';
import DialogUtil from '../components/dialog/util';

function Webhook() {
    return {
        pushMode: 0,
        pushModeMount: false,
        pushModeOptions: [
            { value: 0, label: 'MQTT' },
            { value: 1, label: 'Webhook' },
        ],
        webhook: {
            url: '',
            header: '',
        },
        webhookUrlError: false,
        ...DialogUtil,

        async getWebhookInfo() {
            try {
                const res = await getData(URL.getWebhookParam);
                if (res) this.webhook = { url: res.url || '', header: res.header || '' };
            } catch (e) {
                console.error('getWebhookInfo error', e);
            }
        },
        async getPushModeInfo() {
            try {
                const res = await getData(URL.getPushMode);
                if (res && typeof res.mode === 'number') {
                    this.pushMode = res.mode;
                }
                this.pushModeMount = true;
            } catch (e) {
                console.error('getPushModeInfo error', e);
                this.pushModeMount = true;
            }
        },
        changePushMode({ detail }) {
            if (typeof detail.value === 'number') {
                this.pushMode = detail.value;
            }
            // Don't send API request on MsSelect initialization
            if (detail.isInit) return;
            postData(URL.setPushMode, { mode: this.pushMode }).then(() => {
                if (this.pushMode == 1) {
                    this.getWebhookInfo();
                }
            }).catch(() => {
                this.alertMessage("error");
            });
        },
        inputWebhookUrl() {
            this.webhookUrlError = !this.checkRequired(this.webhook.url);
        },
        checkRequired(val) {
            return val.toString().trim() !== '';
        },
        validateWebhookForm() {
            const list = document.querySelectorAll('.mqtt-card input[type=text], .mqtt-card textarea');
            list.forEach((el) => { el.focus(); el.blur(); });
            const errEl = document.querySelectorAll('.mqtt-card div.error-input');
            for (const el of errEl) {
                if (el.style.display !== 'none') return false;
            }
            return true;
        },
        async setWebhookInfo() {
            if (!this.validateWebhookForm()) return;
            try {
                await postData(URL.setWebhookParam, {
                    url: this.webhook.url,
                    header: this.webhook.header,
                });
                this.alertMessage("success");
            } catch (error) {
                this.alertMessage("error");
            }
        },
    };
}

export default Webhook;
