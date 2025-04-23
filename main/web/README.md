<h1>NE101 Sensing Camera</h1>

## Description

NE101感知相机前端

### 前端技术框架

- petite-vue v0.4.1 轻量级Vue框架
- Vite 打包工具

## Get Started

### 安装依赖

```bash
npm install
```

### 启动项目

```bash
npm run dev
```

### 编译与烧录
1. 编译生成dist文件
```bash
npm run build
```
2. 使用ESP-IDF Build Flash and Monitor

3. 观察Monitor等待编译烧录完成，提示进入睡眠时，按下设备按钮启动

4. 连接Wifi NE101-xxx，进入网页192.168.1.1
## Git commit message

| type     | description                                            |
| -------- | ------------------------------------------------------ |
| build    | 编译相关的修改，例如发布版本、对项目构建或者依赖的改动 |
| feat     | 新特性、新功能                                         |
| fix      | 修改 bug                                               |

## Documents

详细设计文档https://doc.weixin.qq.com/doc/w3_AeUARAZEAE4g5okMzInRRC7dkfAyJ?scode=AMgAYAe8AAYIFWiLoPAeUARAZEAE4


### 基础功能模块

### 1.实时图片流
使用img标签，链接到对应URL地址，请求返回mjpeg格式图片，后台会刷，以达到实时图片流的效果。注意由于后台的并发限制，视频流URL的端口另为8080，与其他api区分
``` html
<img id="mjpeg" src="http://192.168.1.1:8080/api/v1/liveview/getJpegStream">
```

### 2.网络请求
使用原生fetch API完成网络请求，自定义封装详见\src\api\index.js
* getData
* postData
* postFileBuffer

### 3.多语言
* /src/i18n/index/js 解析方法
* /src/i18n/lang 语言字典
自行封装多语言方法如下：
1) 默认显示英文，并将语言选择存储到本地localStorage，key为"lang"
2) 切换多语言选项时，更新本地存储，并刷新页面window.location.reload
3) 翻译方法translate()对外暴露为$t，其内部的getValue(langObject, field)方法完成对层级对象的解析，传入参数langObject是当前语言JSON文件对象，field是要解析的字符串，支持多层，以点分隔。
4) 翻译文件中，按业务功能模块区分，如图像调节模块img: { imageAdjust: 图像调节 }

### 4.组件
* 目前petite-vue的组件仅仅是简单复用的组件，要实现父子间双向通信比较复杂
* 组件模板
``` html
<template id="ms-dialog">
    <div class="dialog-shade" ref="dialog">
        <div class="dialog-box">
            <div class="dialog-header" ref="title">
                <span class="dialog-header-title"> {{ title }} </span>
            </div>
            <button class="dialog-header-close icon" @click="handleClose">
                X
            </button>
            ...
```
* 组件的prop&方法
```js
//\components\dialog\index.js
function MsDialog({ option, prop }) {
    return {
        // 弹窗组件template ID
        $template: "#ms-dialog",
        // 弹窗顶部标题
        title: prop.title || "Tips",
        handleCancel() {
            this.dialogVisible = false;
        },
    }
}
```
* 组件注册 (不带括号)
``` js
createApp({
    MsDialog,
}).mount('.container');
```

* 组件使用,在要用到的html里面
``` html
<div
    v-if="dialogVisible"
    v-scope="MsDialog(dialogParam)"
    @vue:mounted="dialogMounted"
></div>
```
* 使用时括号里面传参，相当于prop
* 组件仍有其生命周期mounted/unmounted
* prop父值更新并不会通知子组件，所以使用了v-if控制
* 子传父使用了dispatchEvent事件分发来传递参数

#### 弹窗组件MsDialog
* src/component/dialog/util中封装了三种弹窗调用方法
* 全局仅一个弹窗实例，通过dialogVisible控制显示与更新
#### 选择下拉框组件
四要素
1. value
2. options
3. v-if 等数据拿到后再初始化组件
4. change事件
``` html
<div
    class="capture-interval-select"
    v-scope="MsSelect({value: timeIntervalUnit, options: timeIntervalOptions})"
    v-if="timeIntervalUnitMount"
    @change-select="changeIntervalUnit"
></div>
```

### 业务功能模块
* Image
* Capture
* MQTT
* Device
* WLAN
* Sleep

### 其他问题
#### Q：切换语言时解析慢导致{{}}数据闪烁
A：使用v-cloak属性，配合样式display:none，当{{}}内的数据解析完成时再显示

### Vite Build
* 配合后端编译生成固定名称的文件，配置output
``` js
output: {
    // 固定打包output文件命名
    entryFileNames: `assets/[name].js`,
    chunkFileNames: `assets/[name].js`,
    assetFileNames: `assets/[name].[ext]`,
    }
```
* 为兼容移动端浏览器，防止将rgba()颜色转化为#十六进制
``` js
cssTarget: 'chrome61'
```

* 由于固定打包文件名，版本迭代升级时浏览器容易请求缓存资源导致错误，因此需要手动修改dist/index.html中js资源的引用，在后面加上“?时间戳”
``` js
src=./assets/index.js?202302011054
```
