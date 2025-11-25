/**
 * // dropdown component
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
            // if opened, add event listener
            if (this.visible) {
                document.addEventListener('click', this.handleBlur);
            } else {
                // otherwise remove event listener
                document.removeEventListener('click', this.handleBlur);
            }
        },
        setSelectItem() {
            const item = this.options.find(
                (item) => item.value == this.selectValue
            );
            // manually trigger change on initialization to update value
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
         * select option triggers update
         * @param {*} $event 
         * @param {*} option 
         * @param {boolean} isInit only update value on initialization, do not send configuration
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
