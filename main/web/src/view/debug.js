import { translate as $t } from '../i18n';
import { postData, URL } from '../api';

function Debug() {
    return {
        pingHost: '8.8.8.8',
        pingResult: '',
        pingLoading: false,

        async doPing() {
            if (!this.pingHost.trim() || this.pingLoading) return;
            this.pingLoading = true;
            this.pingResult = '';
            try {
                const { result, message } = await postData(URL.pingTest, {
                    host: this.pingHost.trim(),
                    count: 4
                });
                this.pingResult = message || '';
            } catch (error) {
                this.pingResult = $t('debug.pingError') || 'Network error';
            } finally {
                this.pingLoading = false;
            }
        },
    };
}

export default Debug;
