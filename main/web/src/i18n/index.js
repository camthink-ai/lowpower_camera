import zh_CN from "./lang/zh_CN.json";
import en_US from "./lang/en_US.json";

/** 获取当前浏览器的语言 */
export function getCurLang() {
    let curLang = localStorage.getItem("lang") || navigator.language || navigator.browserLanguage || 'en_US';
    if (/en/.test(curLang)) {
        curLang = 'en_US';
    } else if (/zh/.test(curLang)) {
        curLang = 'zh_CN';
    } else {
        curLang = 'en_US';
    }
    console.log("curLang: ", curLang)
    return curLang;
};
const curLang = getCurLang();

const languageMap = {
    'en_US': en_US,
    'zh_CN': zh_CN,
};

/**
 * 解析层级对象
 * @param langObject {*} 语言JSON文件 {innerObj: {a: 'xxx', b: 'yyy'}}
 * @param field "innerObj.a" | "a"
 */
const getValue = (langObject = {}, field = "") => {
    if (typeof langObject !== "object") {
        return langObject;
    }
    const fields = field.split(".");
    return fields.reduce((preVal, currVal) => {
        if (!preVal) return null;
        return preVal[currVal];
    }, langObject);
};

// 语言翻译文件json对象
const currentTranslationConf = languageMap[curLang] || en_US;

export const translate = (path) => {
    return getValue(currentTranslationConf, path) || path;
};
