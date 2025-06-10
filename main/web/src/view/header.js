function Header() {
    return {
        $template: "#header",
        // --Layout Header--
        local: localStorage.getItem("lang") || "en_US",
        languageOptions: [
            {
                label: "English",
                value: "en_US",
            },
            {
                label: "中文",
                value: "zh_CN",
            },
        ],
        changeLanguage({ detail }) {
            this.local = detail.value;
            localStorage.setItem("lang", detail.value);
            window.location.reload();
        },
    };
}

export default Header;
