import { getData, postData, URL } from "../api";
import { translate as $t } from "/src/i18n";
import { nextTick } from '/src/lib/petite-vue.es.js';
function Wlan() {
    return {
        // --WLAN--
        wlanData: [
            // {
            //     status: 0, // -1未连接 0连接中 1已连接
            //     ssid: "WIFI", // Wifi名称
            //     rssi: -54, // 信号强度 负值
            //     bAuthenticate: 1, // 是否加密 0未加密 1加密
            // }
        ],
        isSignalActive: "signal-active",
        curConnectItem: {},
        wlanLoading: false,
        changeRegionLoading: false,
        regionOptions: [],
        regionOptionsForCe: [
            {
                value: "EU",
                label: "EU",
            },
            {
                value: "IN",
                label: "IN",
            }
        ],
        regionOptionsForFcc: [
            {
                value: "AU",
                label: "AU",
            },
            {
                value: "KR",
                label: "KR",
            },
            {
                value: "NZ",
                label: "NZ",
            },
            {
                value: "SG",
                label: "SG",
            },
            {
                value: "US",
                label: "US",
            },
        ],
        async getWlanInfo() {
            if(this.wlanLoading) return;
            this.wlanData = [];
            this.wlanLoading = true;
            const res = await getData(URL.getWifiList);
            this.wlanData = res.nodes;
            // 获取上次连接信息，在当前列表中查找对应对象
            const lastConRes = await getData(URL.getWifiParam);
            this.wlanLoading = false;
            const lastItem = this.wlanData.find(
                (item) => item.ssid == lastConRes.ssid
            );
            // 如果存在上次连接信息则更新wifi，否则不处理
            if (lastItem) {
                this.curConnectItem = lastItem;
                if (lastConRes.isConnected) {
                    this.curConnectItem.status = 1;
                } else {
                    this.curConnectItem.status = -1;
                }
            }
            return;
        },
        /**
         * 转换wifi信号强度等级
         * @param {number} rssiLevel 负数的信号强度值
         * @return {number} 0 1 2 3 4
         */
        transRssiLevel(rssiLevel) {
            if (rssiLevel < -88) {
                return 0;
            } else if (rssiLevel < -77 && rssiLevel >= -88) {
                return 1;
            } else if (rssiLevel < -66 && rssiLevel >= -77) {
                return 2;
            } else if (rssiLevel < -55 && rssiLevel >= -66) {
                return 3;
            } else {
                // >= -55
                return 4;
            }
        },
        /**
         * 返回Wifi信号样式
         * @param {number} rssi
         * @param {*} colIndex
         */
        getSignalStyle(rssi, colIndex) {
            const level = this.transRssiLevel(rssi);
            if (level >= colIndex) {
                return "signal-col signal-active";
            } else {
                return "signal-col";
            }
        },

        tmpWlanItem: null,
        /**
         * 点击wifi事件
         * @param {*} item 引用对象，修改后视图更新
         */
        async handleConnectWlan(item) {
            // 点击已连接的wifi不会再次连接
            if (item.status == 1) {
                return;
            }
            this.tmpWlanItem = item;
            const wlanParam = {
                ssid: item.ssid,
                password: "",
            };
            // 判断是否加密
            if (item.bAuthenticate) {
                // 弹窗输入密码
                this.showFormDialog(item, this.sendWlanParam);
            } else {
                // 直接连接
                await this.sendWlanParam(wlanParam);
            }
            return;
        },
        /**
         * 发送连接Wifi请求，并更新列表状态
         * 当真正准备发起连接请求时才能更新状态
         * @param {*} wlanParam {password: ""}
         */
        async sendWlanParam(wlanParam) {
            if (this.tmpWlanItem.bAuthenticate && !wlanParam.password) {
                wlanParam.showError = true;
                nextTick(() => {
                    this.showFormDialog(wlanParam, this.sendWlanParam);
                });
            } else {
                // 先清除原连接项的状态值
                this.curConnectItem.status = -1;
                this.curConnectItem = this.tmpWlanItem;
                // 连接时显示加载图标
                this.curConnectItem.status = 0;
                try {
                    const res = await postData(URL.setWifiParam, wlanParam);
                    // 1001：connected 1002: disconnected
                    if (res.result == 1001) {
                        this.curConnectItem.status = 1;
                    } else if (res.result == 1002) {
                        // 连接失败
                        this.curConnectItem.status = -1;
                        // 判断是否加密
                        if (this.curConnectItem.bAuthenticate) {
                            wlanParam.showError = true;
                            this.showFormDialog(wlanParam, this.sendWlanParam);
                        } else {
                            this.showTipsDialog($t("wlan.connectFailTips"));
                        }
                    }
                } catch (error) {
                    this.curConnectItem.status = -1;
                    this.showTipsDialog($t("wlan.connectFailTips"));
                }
            }

            return;
        },
        /**
         * region 下拉框更改事件
         * @param 下拉选中值
         * 更新region，重新更新wlan列表
         */
        async changeRegion({ detail }) {
            if(this.changeRegionLoading) return;
            this.wlanRegion = detail.value;
            this.changeRegionLoading = true;
            try {
                const res = await postData(URL.setDevInfo, {
                    countryCode: this.wlanRegion,
                });
                if (res.result === 1000) {
                    this.getWlanInfo();
                    this.changeRegionLoading = false;
                }
            } catch (error) {
                this.alertErrMsg();
            }
        },
    };
}

export default Wlan;
