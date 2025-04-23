import { translate as $t } from '../i18n';
import { getData, postData, URL } from '../api';

function Cellular() {
    return {
        /** Connect | Disconnect */
        cellularStatus: false,
        /** 是否显示详情页面 */
        goDetail: false,
        cellParam: {
            apn: '',
            user: '',
            password: '',
            pin: '',
            authentication: 0,
        },
        cellAuthenOptions: [
            {
                value: 0,
                label: 'None',
            },
            {
                value: 1,
                label: 'PAP',
            },
            {
                value: 2,
                label: 'CHAP',
            },
            {
                value: 3,
                label: 'PAP or CHAP',
            },
        ],
        command: '',

        cellState: {
            networkStatus: 'Connected',
            modemStatus: 'PIN Error',
            model: '-',
            version: '-',
            signalLevel: 'XX asu( XX dBm)',
            registerStatus: 'Not registered',
            imei: '-',
            imsi: '-',
            iccid: '-',
            isp: '-',
            networkType: '-',
            plmnId: '-',
            lac: '-',
            cellId: '-',
            ipv4Address: '0.0.0.0/0',
            ipv4Gateway: '::',
            ipv4Dns: '::',
            ipv6Address: '0.0.0.0/0',
            ipv6Gateway: '::',
            ipv6Dns: '::',
        },
        cellMounted: false,
        sendLoading: false,
        saveLoading: false,
        changeCellAuthenType({ detail }) {
            this.cellParam.authentication = detail.value;
        },
        async getCellularInfo() {
            const param = await getData(URL.getCellularParam);
            this.cellParam = { ...param };
            await this.getCellularStatus();
            this.cellMounted = true;
        },
        async setCellularInfo() {
            if(this.saveLoading) return;
            this.saveLoading = true;
            try {
                alert($t('cell.saveTip'));
                await postData(URL.setCellularParam, { ...this.cellParam });
                this.getCellularInfo();
                this.saveLoading = false;
            } catch (error) {
                this.alertErrMsg();
            }
        },
        async getCellularStatus() {
            const state = await getData(URL.getCellularStatus);
            this.cellState = { ...state };
        },
        async sendCellularCommand() {
            // at指令是空时不调用接口
            if(!this.command || this.sendLoading || this.saveLoading ) return;
            this.sendLoading = true;
            try {
                const { result, message } = await postData(URL.sendCellularCommand, { command: this.command });
                // TODO show Message
                this.sendLoading = false;
                alert(message);
            } catch (error) {
                this.alertErrMsg();
                this.sendLoading = false;
            }
        },
        // 返回主界面，并滚动至cellular卡片
        goBackCell() {
            this.goDetail = false;
            // 存在渲染问题，需异步
            setTimeout(() => {
                const el = document.querySelector('.cellular-card');
                el.scrollIntoView();
            }, 10);
        },
    };
}

export default Cellular;
