/**
 * 轻量级 Loading 组件（纯脚本，无需模板/样式文件）
 * 用法：
 * import Loading from './components/loading';
 * Loading.show('Uploading...');
 * Loading.hide();
 */

const OVERLAY_ID = 'ms-loading-overlay';
const SPINNER_ID = 'ms-loading-spinner';
const TEXT_ID = 'ms-loading-text';

function ensureOverlay() {
    let overlay = document.getElementById(OVERLAY_ID);
    if (overlay) return overlay;

    overlay = document.createElement('div');
    overlay.id = OVERLAY_ID;
    overlay.style.position = 'fixed';
    overlay.style.zIndex = '9999';
    overlay.style.left = '0';
    overlay.style.top = '0';
    overlay.style.right = '0';
    overlay.style.bottom = '0';
    overlay.style.background = 'rgba(0,0,0,0.35)';
    overlay.style.display = 'flex';
    overlay.style.flexDirection = 'column';
    overlay.style.alignItems = 'center';
    overlay.style.justifyContent = 'center';
    overlay.style.backdropFilter = 'blur(1px)';
    overlay.style.webkitBackdropFilter = 'blur(1px)';

    // spinner
    const spinner = document.createElement('div');
    spinner.id = SPINNER_ID;
    spinner.style.width = '35px';
    spinner.style.height = '35px';
    spinner.style.border = '4px solid #f24A06';
    spinner.style.borderTopColor = 'rgba(0,0,0,0)';
    spinner.style.borderRadius = '50%';
    spinner.style.animation = 'ms-loading-rotate 0.8s linear infinite';

    // text
    const text = document.createElement('div');
    text.id = TEXT_ID;
    text.style.color = '#fff';
    text.style.marginTop = '12px';
    text.style.fontSize = '14px';
    text.textContent = 'Loading...';

    // keyframes（仅注入一次）
    if (!document.getElementById('ms-loading-style')) {
        const styleEl = document.createElement('style');
        styleEl.id = 'ms-loading-style';
        styleEl.textContent = `@keyframes ms-loading-rotate{from{transform:rotate(0)}to{transform:rotate(360deg)}}`;
        document.head.appendChild(styleEl);
    }

    overlay.appendChild(spinner);
    overlay.appendChild(text);
    document.body.appendChild(overlay);
    return overlay;
}

function showLoading(message = 'Loading...') {
    const overlay = ensureOverlay();
    const textEl = overlay.querySelector('#' + TEXT_ID);
    if (textEl) textEl.textContent = message;
    overlay.style.display = 'flex';
}

function hideLoading() {
    const overlay = document.getElementById(OVERLAY_ID);
    if (overlay) overlay.style.display = 'none';
}

export default {
    show: showLoading,
    hide: hideLoading,
};


