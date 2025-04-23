import { nextTick } from "/src/lib/petite-vue.es.js";
import { translate as $t } from "/src/i18n";
/**
 * 弹窗基本组件
 * @param {*} type
 * @returns
 */
function MsDialog({ option, prop }) {
    return {
        // 弹窗组件template ID
        $template: "#ms-dialog",
        // 弹窗顶部标题
        title: prop.title || "Tips",
        // 显示OK按钮
        showBtnOK: option.showBtnOK === undefined ? true : option.showBtnOK,
        btnOkText: option.btnOkText || $t("ok"),
        // 显示Cancel按钮
        showBtnCancel: 
            option.showBtnCancel === undefined ? false : option.showBtnCancel,
        btnCancelText: option.btnCancelText || $t("cancel"),
        showBtnClose: option.showBtnClose === undefined ? true : option.showBtnClose,
        content: prop.content || "",
        // --输入密码弹窗--
        showPwdContent:
            option.showPwdContent === undefined ? false : option.showPwdContent,
        showPwd: option.showPwd === undefined ? false : option.showPwd,
        dialogPwdModel: "", // 输入密码
        showError: prop.showError === undefined ? false : prop.showError, // 密码错误时输入框边框为红
        // --升级进度条弹窗--
        showProgress:
            option.showProgress === undefined ? false : option.showProgress,

        upgradeProgress: 0,
        
        handleOK() {
            this.dialogVisible = false;
            // TODO 判断类型为函数
            if (prop.okCallback) {
                // 表单弹窗，回调传参数
                if (this.showPwdContent) {
                    prop.okCallback({
                        ssid: prop.title,
                        password: this.dialogPwdModel,
                    });
                } else {
                    // 普通弹窗
                    prop.okCallback();
                }
            }
        },
        handleCancel() {
            this.dialogVisible = false;
        },
        handleClose() {
            // 如果加载的是升级弹窗，则关闭前清除定时器
            if (this.showProgress) {
                clearInterval(this.mockInterval);
                this.$refs.dialog.dispatchEvent(new CustomEvent('close-upgrade', {
                    bubbles: true 
                }))
            }
            // TODO 并停止文件上传
            this.dialogVisible = false;
        },
        dialogMounted() {
            // 如果加载的是升级弹窗，则开启loading动画
            if (this.showProgress) {
                this.upgradeProgress = 0;
                nextTick(() => {
                    let progressDom = document.getElementById("progress-inner");
                    progressDom.style.width = this.upgradeProgress + "%";
                    this.mockInterval = setInterval(() => {
                        this.upgradeProgress = this.upgradeProgress + 1;
                        document.getElementById("progress-inner").style.width =
                            this.upgradeProgress + "%";
                        if (this.upgradeProgress >= 100) {
                            clearInterval(this.mockInterval);
                        }
                    }, 120);
                    progressDom = null;
                });
            }
        },
    };
}

export default MsDialog;
