/**
 * // 下拉框组件
 * @param {*} param0
 * @returns
 */
function MsSelect({ value, options }) {
    return {
        $template: '#ms-select',
        name: 'MsSelect',
        selectValue: value || '',
        selectedLabel: '',
        visible: false,
        options: options || [],
        initSelectCpm() {
            this.setSelectItem();
        },
        toggleMenu() {
            this.visible = !this.visible;
            // 假如打开则添加事件监听
            if (this.visible) {
                document.addEventListener('click', this.handleBlur);
            } else {
                // 否则移除事件监听
                document.removeEventListener('click', this.handleBlur);
            }
        },
        setSelectItem() {
            const item = this.options.find(
                (item) => item.value == this.selectValue
            );
            // 初始化时手动触发change选择时间更新值
            this.handleOptionSelect(null, item, true);
            this.selectedLabel = item.label;
        },
        handleBlur(e) {
            const panel = this.$refs.select;
            if (!panel.contains(e.target) && this.visible) {
                this.visible = false;
                document.removeEventListener('click', this.handleBlur);
            }
        },
        /**
         * 选择选项触发更新
         * @param {*} $event 
         * @param {*} option 
         * @param {boolean} isInit 初始化时仅更新值，不发送配置
         */
        handleOptionSelect($event, option, isInit = false) {
            this.visible = false;
            const isChanged = this.selectValue !== option.value;
            this.selectValue = option.value;
            this.selectedLabel = option.label;
            if (isChanged) {
                this.$refs.dropdown.dispatchEvent(
                    new CustomEvent('change-select', {
                        bubbles: true, // Required, attention parent dom
                        detail: {
                            value: this.selectValue,
                            label: this.selectedLabel,
                            isInit: isInit
                        },
                    })
                );
            }
        },
    };
}

export default MsSelect;
