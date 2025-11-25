import { translate as $t } from "/src/i18n";

/** global single dialog instance, variables and methods in this file are public */

/** control dialog visibility */
const dialogVisible = false;
/** dialog parameters */
const dialogParam = {};

/**
 * show tip dialog
 * @param {*} content tip content
 * @param {function} callback callback when confirm is clicked
 */
function showTipsDialog(content, showBtnCancel = false, callback) {
    this.dialogParam = {
        option: {
            showBtnCancel: showBtnCancel,
        },
        prop: {
            title: "Tips",
            content: content,
            okCallback: callback,
        },
    };
    this.dialogVisible = true;
}

/**
 * show progress bar dialog, default title is upgrade
 * @param {*} content tip content
 * @param {function} callback callback when confirm is clicked
 */
function showUpgradeDialog(content, callback = {}) {
    this.dialogParam = {
        option: {
            showBtnOK: false,
            showProgress: true,
            showBtnCancel: false,
            showBtnClose: false,
        },
        prop: {
            title: $t("sys.systemUpgrade"),
            content: content,
            okCallback: callback,
        },
    };
    this.dialogVisible = true;
}

/**
 * show form dialog, default is input password to connect Wifi
 * @param {*} content tip content
 * @param {function} callback callback when confirm is clicked
 */
function showFormDialog(formData, callback) {
    this.dialogParam = {
        option: {
            showPwdContent: true,
            btnOkText: $t("wlan.join"),
            showBtnCancel: false,
        },
        prop: {
            title: formData.ssid,
            showError: formData.showError,
            okCallback: callback,
        },
    };
    this.dialogVisible = true;
}
export default {
    dialogVisible,
    dialogParam,
    showTipsDialog,
    showUpgradeDialog,
    showFormDialog,
};
