import { nextTick } from "/src/lib/petite-vue.es.js";
import { translate as $t } from "../i18n";
import { getData, postData, postFileBuffer, URL } from "../api";
function Device() {
    return {
        // --Device Maintenance--
        netmod: "", // network mode wifi cat1 halow
        deviceName: "",
        macAddress: "",
        sn: "",
        battery: "",
        hardwareVer: "",
        firmwareVer: "",
        autoProEnable: true,
        developEnable: true,
        upgradeFile: null,
        upgradeFilename: "",
        countryCode: "",
        // module type
        camera: "CSI", // "USB" | "CSI"
        // NTP sync switch
        ntpSync: true,
        async getDeviceInfo() {
            const res = await getData(URL.getDevInfo);
            this.netmod = res.netmod;
            this.deviceName = res.name;
            this.macAddress = res.mac;
            this.sn = res.sn;
            this.hardwareVer = res.hardVersion;
            this.firmwareVer = res.softVersion;
            this.countryCode = res.countryCode;
            this.camera = res.camera;
            const softType = Number(res.softVersion.split('.')[1])
            if (softType === 1) {
                this.regionOptions = this.regionOptionsForFcc 
            } else if(softType === 2) {
                this.regionOptions = this.regionOptionsForCe // NE_101.2.0.1 CE
            }
            const { freePercent, bBattery } = await getData(URL.getDevBattery);
            if (bBattery) {
                this.battery = freePercent + "%";
            } else {
                this.battery = $t("sys.typecPowered");
            }

            // get NTP sync switch status
            const ntpSyncRes = await getData(URL.getDevNtpSync);
            this.ntpSync = ntpSyncRes.enable ? true : false;

            // const iotRes = await getData(URL.getIoTParam);
            // this.autoProEnable = iotRes.autop_enable ? true : false;
            // this.developEnable = iotRes.dm_enable ? true : false;

            return;
        },
        
        async setDeviceInfo($el) {
            if (this.validateEmpty($el)) {
                try {
                    await postData(URL.setDevInfo, {
                        name: this.deviceName,
                        mac: this.macAddress,
                        sn: this.sn,
                        hardVersion: this.hardwareVer,
                        softVersion: this.firmwareVer,
                    });
                } catch (error) {
                    this.alertMessage("error");
                }
                
            }
        },
        async setNtpSync() {
            try {
                await postData(URL.setDevNtpSync, {
                    enable: this.ntpSync ? 1 : 0,
                });
            } catch (error) {
                this.alertMessage("error");
            }
        },
        handleBrowse() {
            const file = document.getElementById("file");
            file.click();
        },
        fileChange() {
            try {
                const inputEl = document.getElementById("file");
                if (inputEl == null || inputEl.files.length == 0) return;
                this.upgradeFilename = inputEl.files[0].name;
                this.upgradeFile = inputEl.files[0];
            } catch (error) {
                console.debug("choice file err:", error);
            }
        },
        handleUpgrade() {
            if (!this.upgradeFile || this.upgradeFile.name == "") {
                this.showTipsDialog($t("sys.noFirmSpecified"));
                return;
            }
            this.showTipsDialog(
                $t("sys.reallyUpgradeFirmware"),
                true,
                this.confirmUpgrade
            );
        },
        showDialogUpgrade: false,
        dialogUpgradeProp: {},
        
        async confirmUpgrade() {
            const that = this;
            // wait for browser to load image file
            nextTick(() => {
                that.showUpgradeDialog($t("sys.operateWait"));
            });
            // calculate file upload time
            console.time("fileReader");
            const reader = new FileReader();
            reader.readAsArrayBuffer(this.upgradeFile);
            reader.onload = async function () {
                try {
                    console.timeEnd("fileReader");
                    // calculate time for image file transfer completion and response
                    console.time("postFile");
                    const res = await postFileBuffer(
                        URL.setDevUpgrade,
                        reader.result
                    );
                    console.timeEnd("postFile");
                    if (res.result == 1000) {
                        // after file transfer succeeds, wait 5s for device reset, close dialog after 5s
                        setTimeout(() => {
                            that.dialogVisible = false;
                            nextTick(() => {
                                that.showTipsDialog(
                                    $t("sys.updateSuccess"),
                                    false,
                                    that.onReload
                                );
                            });
                        }, 5000);
                    } else if (res.result == 1003) {
                        // failed
                        that.dialogVisible = false;
                        nextTick(() => {
                            that.showTipsDialog(
                                $t("sys.upgradeFailedTryAgain"),
                                false,
                                that.onReload
                            );
                        });
                    }
                } catch (error) {
                    // 失败
                    that.dialogVisible = false;
                    nextTick(() => {
                        that.showTipsDialog(
                            $t("sys.upgradeFailedTryAgain"),
                            false,
                            that.onReload
                        );
                    });
                }
            };
        },
        // TODO close upgrade dialog to interrupt file upload
        // closeUpgrade() {
        //     if (this.reader && this.reader.readyState == 1) {
        //         this.reader.abort();
        //     }
        // },
        onReload() {
            window.location.reload();
        },
        /** remove cloud ecosystem developer platform related parameters */
        // async setIotParam() {
        //     try {
        //         await postData(URL.setIotParam, {
        //             autop_enable: Number(this.autoProEnable),
        //             dm_enable: Number(this.developEnable),
        //         });
        //     } catch (error) {
        //         this.alertErrMsg();
        //     }
        //     return;
        // }
    }
}

export default Device;