// 使用Fetch原生API并根据请求类型进行封装

function getData(url = "") {
    return fetch(url, {
        method: "GET",
        mode: "cors",
        headers: {
            "Content-Type": "application/json",
        },
    })
        .then((response) => {
            if (response.ok) return response.json();
            else return Promise.reject(response.json());
        })
        .catch((error) => console.error(error));
}
function postData(url = "", data = {}) {
    return fetch(url, {
        method: "POST",
        mode: "cors",
        headers: {
            "Content-Type": "application/json",
        },
        body: JSON.stringify(data),
    })
        .then((response) => {
            if (response.ok) return response.json();
            else return Promise.reject(response.json());
        })
        .catch((error) => {
            console.error(error);
            return Promise.reject();
        });
}

// 上传MQTTS文件
function postMqttFile(url = "",fileName = "", formData) {
    return fetch(url, {
        method: "POST",
        body: formData,
        headers: {
            "Content-Type": "application/octet-stream",
            "X-File-Name": fileName,
        },
    })
        .then((response) => {
            if (response.ok) return response.json();
            else return Promise.reject(response.json());
        })
        .catch((error) => console.error(error));
}
// 传输文件
function postFile(url = "", formData) {
    return fetch(url, {
        method: "POST",
        body: formData,
        headers: {
            "Content-Type": "multipart/form-data",
        },
    })
        .then((response) => {
            if (response.ok) return response.json();
            else return Promise.reject(response.json());
        })
        .catch((error) => console.error(error));
}
// 传输二进制文件
function postFileBuffer(url = "", formData) {
    return fetch(url, {
        method: "POST",
        body: formData,
        headers: {
            "Content-Type": "application/octet-stream",
        },
    })
        .then((response) => {
            if (response.ok) return response.json();
            else return Promise.reject(response.json());
        })
        .catch((error) => console.error(error));
}
const baseUrl = "/api/v1";
const URL = {
    getJpegStream: baseUrl + "/liveview/getJpegStream",
    getCamParam: baseUrl + "/image/getCamParam",
    setCamParam: baseUrl + "/image/setCamParam",
    getLightParam: baseUrl + "/image/getLightParam",
    setLightParam: baseUrl + "/image/setLightParam",
    getCapParam: baseUrl + "/capture/getCapParam",

    setCapParam: baseUrl + "/capture/setCapParam",
    getMqttParam: baseUrl + "/network/getPlatformParam",

    getDataReport: baseUrl + "/network/getPlatformParam",
    setDataReport: baseUrl + "/network/setPlatformParam",
    getDevInfo: baseUrl + "/system/getDevInfo",
    setDevInfo: baseUrl + "/system/setDevInfo",
    getDevBattery: baseUrl + "/system/getDevBattery",
    setDevUpgrade: baseUrl + "/system/setDevUpgrade",
    getWifiList: baseUrl + "/network/getWifiList",
    getWifiParam: baseUrl + "/network/getWifiParam",
    setWifiParam: baseUrl + "/network/setWifiParam",
    setDevSleep: baseUrl + "/system/setDevSleep",
    setDevTime: baseUrl + "/system/setDevTime",

    // Cellular
    getCellularParam: baseUrl + "/network/getCellularParam",
    setCellularParam: baseUrl + "/network/setCellularParam",
    sendCellularCommand: baseUrl + "/network/sendCellularCommand",
    getCellularStatus: baseUrl + "/network/getCellularStatus",
    getIoTParam: baseUrl + "/network/getIoTParam",
    setIotParam: baseUrl + "/network/setIoTParam",

    // 定时上传
    setUploadParam: baseUrl + "/capture/setUploadParam",
    getUploadParam: baseUrl + "/capture/getUploadParam",


    // MQTTFiles
    uploadMQTTCa: baseUrl + "/network/uploadMqttCa",
    uploadMQTTCert: baseUrl + "/network/uploadMqttCert",
    uploadMQTTKey: baseUrl + "/network/uploadMqttKey",
    deleteMQTTCa: baseUrl + "/network/deleteMqttCa",
    deleteMQTTCert: baseUrl + "/network/deleteMqttCert",
    deleteMQTTKey: baseUrl + "/network/deleteMqttKey",

};

export { getData, postData, postFile, postFileBuffer, URL, postMqttFile };
