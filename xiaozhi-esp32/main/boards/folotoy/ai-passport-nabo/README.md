# AI Passport NABO XiaoZhi

ESP32-C3 / 8 MB / ST7789P3 / ES8311 / GPIO0 ADC keys.
Reuses the user's reviewed 224x224 half-body NABO assets and exact streaming renderer, including eye-only idle blinks. A four-row LVGL decoder avoids a full-frame allocation.
OK toggles conversation. UP/DOWN change volume. Starting up, OK enters Wi-Fi setup.
Double UP returns one menu level, including radio directories, keyboard groups and setting edits. Long UP returns directly to NABO/XiaoZhi; single-click behavior is preserved. The separate hardware power button is not exposed to firmware and cannot be mapped to navigation.
Unique OTA identity: ai-passport-nabo. Single factory app; stock OTA upgrades are unsupported with this partition map. USB updates must preserve NVS and files.
Build: python scripts/build.py folotoy/ai-passport-nabo --name ai-passport-nabo --language zh-CN
