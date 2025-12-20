# OpenXR Android Application with Overlay Extension

## 📋 Project Overview

**Objective:** Develop an Android-based OpenXR application utilizing the `XR_EXTX_overlay` extension for enhanced augmented reality functionality.

---

## 📦 Package Contents

This repository contains the following structure:

```
📁 Repository Root
├── 📁 apks/
│   ├── monado.apk              # Monado OpenXR runtime APK
│   ├── base.apk                # Compiled base application APK
│   ├── overlay1.apk            # Primary overlay application APK
│   ├── overlay2.apk            # Secondary overlay application APK
│   └── overlay3.apk            # Tertiary overlay application APK
├── 📁 base/                    # Base application source code
├── 📁 overlays/
│   ├── 📁 overlay1/            # Primary overlay source code
│   ├── 📁 overlay2/            # Secondary overlay source code
│   └── 📁 overlay3/            # Tertiary overlay source code
└── README.md
```

---

## 🚀 Installation Guide

### Prerequisites

- Android device with developer options enabled
- USB debugging enabled (for ADB installation method)
- File manager app with installation permissions

### ⚠️ Important Installation Order

**CRITICAL:** Follow the installation sequence exactly as specified below. Installing components out of order may result in functionality issues.

---

### Step 1: Install Monado Runtime

1. **Download and Install**
   ```
   Install: apks/monado.apk
   ```

2. **Enable Unknown Sources** (if prompted)
   - Navigate to: `Settings` → `Apps` → `Special app access` → `Install unknown apps`
   - Select your file manager or browser
   - Toggle `Allow from this source`

3. **Grant Display Overlay Permission**
   - Navigate to: `Settings` → `Apps & notifications` → `Advanced` → `Special app access` → `Display over other apps`
   - Select `Monado`
   - Toggle `Enable`

---

### Step 2: Install OpenXR Runtime Broker

1. **Download from Play Store**
   - Search for: `OpenXR Runtime Broker`
   - Ensure version: `0.9.3` (or specified version)
   - Install application

2. **Launch and Configure**
   - Open `OpenXR Runtime Broker`
   - Navigate to runtime selection/list
   - Select `Monado` as the active runtime
   - Verify selection is confirmed

---

### Step 3: Install Overlay Applications

**Install ALL overlay APKs BEFORE installing the base application.**

#### GUI Installation Method
```
1. Navigate to apks/ folder using file manager
2. Tap each APK file in sequence:
   - overlay1.apk
   - overlay2.apk  
   - overlay3.apk
3. Follow installation prompts
```

#### ADB Installation Method
```bash
# Ensure device is connected with USB debugging enabled
# Navigate to apks/ folder first
cd apks/
adb install -r overlay1.apk
adb install -r overlay2.apk
adb install -r overlay3.apk
```

---

### Step 4: Install Base Application

**Install ONLY after all overlay APKs are successfully installed.**

#### GUI Installation Method
```
Navigate to apks/ folder and tap base.apk, then follow installation prompts
```

#### ADB Installation Method
```bash
# From apks/ folder
adb install -r base.apk
```

---

## ✅ Verification & Launch

### Final Verification Steps

1. **Confirm Runtime Configuration**
   - Open `OpenXR Runtime Broker`
   - Verify `Monado` is still the active runtime

2. **Launch Application**
   - Start the base application
   - Application should initialize using Monado runtime
   - Overlays should load automatically
---

SRIB-PRISM Program : **Worklet ID:** `25IX05RV`
