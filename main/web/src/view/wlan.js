import { getData, postData, URL } from "../api";
import { translate as $t } from "/src/i18n";
import { nextTick } from '/src/lib/petite-vue.es.js';
function Wlan() {
    return {
        // --WLAN--
        wlanData: [
            // {
            //     status: 0, // -1 not connected 0 connecting 1 connected
            //     ssid: "WIFI", // Wifi name
            //     rssi: -54, // signal strength negative value
            //     bAuthenticate: 1, // whether encrypted 0 unencrypted 1 encrypted
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
            // get last connection info, find corresponding object in current list
            const lastConRes = await getData(URL.getWifiParam);
            this.wlanLoading = false;
            const lastItem = this.wlanData.find(
                (item) => item.ssid == lastConRes.ssid
            );
            // if last connection info exists, update wifi, otherwise do nothing
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
         * convert wifi signal strength level
         * @param {number} rssiLevel negative signal strength value
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
         * return Wifi signal style
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
         * click wifi event
         * @param {*} item reference object, view updates after modification
         */
        async handleConnectWlan(item) {
            // clicking already connected wifi will not reconnect
            if (item.status == 1) {
                return;
            }
            this.tmpWlanItem = item;
            const wlanParam = {
                ssid: item.ssid,
                password: "",
            };
            // check if encrypted
            if (item.bAuthenticate) {
                // show dialog to input password
                this.showFormDialog(item, this.sendWlanParam);
            } else {
                // connect directly
                await this.sendWlanParam(wlanParam);
            }
            return;
        },
        /**
         * send wifi connection request and update list status
         * status can only be updated when actually ready to initiate connection request
         * @param {*} wlanParam {password: ""}
         */
        async sendWlanParam(wlanParam) {
            if (this.tmpWlanItem.bAuthenticate && !wlanParam.password) {
                wlanParam.showError = true;
                nextTick(() => {
                    this.showFormDialog(wlanParam, this.sendWlanParam);
                });
            } else {
                // first clear status value of original connection item
                this.curConnectItem.status = -1;
                this.curConnectItem = this.tmpWlanItem;
                // show loading icon when connecting
                this.curConnectItem.status = 0;
                try {
                    const res = await postData(URL.setWifiParam, wlanParam);
                    // 1001：connected 1002: disconnected
                    if (res.result == 1001) {
                        this.curConnectItem.status = 1;
                    } else if (res.result == 1002) {
                        // connection failed
                        this.curConnectItem.status = -1;
                        // check if encrypted
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
         * region dropdown change event
         * @param dropdown selected value
         * update region, refresh wlan list
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
                this.alertMessage("error");
            }
        },
    };
}

export default Wlan;
