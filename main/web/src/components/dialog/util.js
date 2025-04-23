import { translate as $t } from "/src/i18n";

/** 全局仅一个弹窗实例, 该文件中的变量与方法都是公共的 */

/** 控制弹窗显示与否 */
const dialogVisible = false;
/** 弹窗参数 */
const dialogParam = {};

/**
 * 显示提示弹窗
 * @param {*} content 提示内容
 * @param {function} callback 点击确认的回调
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
 * 显示进度条弹窗,默认标题为升级
 * @param {*} content 提示内容
 * @param {function} callback 点击确认的回调
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
 * 显示表单弹窗,默认为输入密码连接Wifi
 * @param {*} content 提示内容
 * @param {function} callback 点击确认的回调
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
