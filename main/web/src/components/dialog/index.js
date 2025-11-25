import { nextTick } from "/src/lib/petite-vue.es.js";
import { translate as $t } from "/src/i18n";
/**
 * basic dialog component
 * @param {*} type
 * @returns
 */
function MsDialog({ option, prop }) {
    return {
        // dialog component template ID
        $template: "#ms-dialog",
        // dialog top title
        title: prop.title || "Tips",
        // show OK button
        showBtnOK: option.showBtnOK === undefined ? true : option.showBtnOK,
        btnOkText: option.btnOkText || $t("ok"),
        // show Cancel button
        showBtnCancel: 
            option.showBtnCancel === undefined ? false : option.showBtnCancel,
        btnCancelText: option.btnCancelText || $t("cancel"),
        showBtnClose: option.showBtnClose === undefined ? true : option.showBtnClose,
        content: prop.content || "",
        // --password input dialog--
        showPwdContent:
            option.showPwdContent === undefined ? false : option.showPwdContent,
        showPwd: option.showPwd === undefined ? false : option.showPwd,
        dialogPwdModel: "", // input password
        showError: prop.showError === undefined ? false : prop.showError, // input box border turns red when password is wrong
        // --upgrade progress bar dialog--
        showProgress:
            option.showProgress === undefined ? false : option.showProgress,

        upgradeProgress: 0,
        
        handleOK() {
            this.dialogVisible = false;
            // TODO check if type is function
            if (prop.okCallback) {
                // form dialog, pass parameters in callback
                if (this.showPwdContent) {
                    prop.okCallback({
                        ssid: prop.title,
                        password: this.dialogPwdModel,
                    });
                } else {
                    // normal dialog
                    prop.okCallback();
                }
            }
        },
        handleCancel() {
            this.dialogVisible = false;
        },
        handleClose() {
            // if upgrade dialog is loaded, clear timer before closing
            if (this.showProgress) {
                clearInterval(this.mockInterval);
                this.$refs.dialog.dispatchEvent(new CustomEvent('close-upgrade', {
                    bubbles: true 
                }))
            }
            // TODO and stop file upload
            this.dialogVisible = false;
        },
        dialogMounted() {
            // if upgrade dialog is loaded, start loading animation
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
