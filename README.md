# LowPower Camera
![NE101](https://wiki.camthink.ai/assets/images/Module-e3b1464484e50a4d1b01294295f2b1d1.png)

## Project Overview
This project is a low-power image acquisition solution based on the CamThink Event Camera NeoEyes NE101. The main features include:

- Support for multiple trigger conditions to wake up autonomously
- Low-power image acquisition
- Transmission of image data to the cloud via MQTT protocol
  
[Quick Start](https://wiki.camthink.ai/docs/neoeyes-ne101-series/quick-start) 
## Hardware Preparation

### Required Equipment

- Standard development board (integrated with camera sensor)

### Optional Communication Modules

1. CAT1 Cellular Communication Module
2. Halow WIFI Module

> For detailed hardware specifications, please refer to the [hardware introduction document](https://wiki.camthink.ai/docs/NeoEyes%20NE101%20Series/Overview).

## Software Environment Setup

### 1. Obtain the ESP-IDF Development Framework

This project is developed based on ESP-IDF v5.1.6. Please follow the steps below to configure:

```bash
git clone -b v5.1.6 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
```

Configure environment variables:

```bash
. ./export.sh
```

> For detailed installation instructions, please refer to the [ESP-IDF official documentation](https://idf.espressif.com/).

### 2. Obtain the Project Source Code

```bash
git clone https://github.com/camthink-ai/lowpower_camera
cd lowpower_camera
```

## Project Compilation and Execution

### Step 1: Hardware Connection

Please refer to the [hardware connection guide](https://wiki.camthink.ai/docs/NeoEyes%20NE101%20Series/Quick%20Start) to complete the device wiring.

### Step 2: Set Target Chip

```bash
idf.py set-target esp32s3
```

### Step 3: Configure Project Parameters (Optional)

```bash
idf.py menuconfig
```

### Step 4: Compile and Flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

### Step 5: Run Monitor

```bash
idf.py monitor
```
## Star History
[![Star History Chart](https://api.star-history.com/svg?repos=camthink-ai/lowpower_camera&type=Date)](https://star-history.com/#camthink-ai/lowpower_camera&Date)

## Technical Support and Feedback

If you encounter any issues during usage, please submit relevant [issues](https://github.com/camthink-ai/lowpower_camera/issues), and we will respond as soon as possible.

## License

This software is released under a **Dual-License** model.  
- *Community Edition License*   
- *Commercial Edition License*
  
Please see the full terms in [LICENSE](./LICENSE)  


