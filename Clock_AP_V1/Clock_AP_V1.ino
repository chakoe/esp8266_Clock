/**
 * ESP8266 网络时钟 - 简洁版
 * 功能：使用TM1637四位数码管显示从NTP服务器获取的网络时间
 * 特性：支持WiFiManager一键配网、按钮控制亮度、WiFi重置功能
 * 时间格式：24小时制显示
 * 
 * 硬件连接：
 * - TM1637 CLK -> D5 (GPIO14)
 * - TM1637 DIO -> D6 (GPIO12) 
 * - 按钮 -> D1 (GPIO5) + GND
 * - ESP8266开发板 (NodeMCU/Wemos D1 Mini等)
 * 
 * 按钮功能：
 * - 短按：循环切换显示亮度（4个等级）
 * - 长按5秒：重置WiFi设置并重启
 */

// ==================== 库文件包含 ====================
#include <ESP8266WiFi.h>        // ESP8266 WiFi功能库
#include <WiFiManager.h>         // WiFi配网管理库
#include <NTPClient.h>           // NTP网络时间协议客户端库
#include <WiFiUdp.h>             // UDP通信库
#include <TM1637Display.h>       // TM1637四位数码管显示库

// ==================== 硬件引脚定义 ====================
#define CLK_PIN D5               // TM1637时钟信号引脚 (GPIO14)
#define DIO_PIN D6               // TM1637数据信号引脚 (GPIO12)
#define BUTTON_PIN D1            // 功能按钮引脚 (GPIO5)

// ==================== 系统参数常量 ====================
#define BLINK_INTERVAL 500       // 冒号闪烁间隔时间（毫秒）
#define NTP_UPDATE_INTERVAL 1800000  // NTP时间更新间隔（30分钟）
#define WIFI_RESET_TIMEOUT 5000  // WiFi重置长按触发时间（5秒）
#define DEBOUNCE_TIME 50         // 按钮防抖时间（毫秒）

// ==================== 全局对象初始化 ====================
TM1637Display display(CLK_PIN, DIO_PIN);                    // TM1637显示控制对象
WiFiUDP ntpUDP;                                              // UDP通信对象
NTPClient timeClient(ntpUDP, "ntp1.aliyun.com", 8*3600, 60000); // NTP客户端（UTC+8北京时间）
WiFiManager wifiManager;                                     // WiFi配网管理对象

// ==================== 全局变量 ====================
bool colonState = true;                           // 冒号显示状态
unsigned long lastBlinkTime = 0;                  // 上次冒号闪烁时间
unsigned long lastNTPUpdate = 0;                  // 上次NTP更新时间
uint8_t brightnessLevels[4] = {1, 3, 5, 7};     // 亮度等级数组
uint8_t currentBrightnessIndex = 2;               // 当前亮度等级索引
unsigned long buttonPressStart = 0;               // 按钮按下开始时间
bool buttonPressed = false;                       // 按钮状态
unsigned long lastDebounceTime = 0;               // 上次防抖时间
int currentHour = 12;                             // 当前小时
int currentMinute = 0;                            // 当前分钟

// ==================== 显示管理函数 ====================

/**
 * 显示启动动画
 */
void showStartupAnimation() {
  // 显示"8888"然后清空
  display.showNumberDec(8888, true);
  delay(1000);
  display.clear();
  delay(200);
}

/**
 * 更新时间显示
 */
void updateTimeDisplay() {
  // 格式化时间为4位数字：HHMM
  int timeValue = currentHour * 100 + currentMinute;
  
  // 显示时间，第二个参数控制冒号显示
  display.showNumberDecEx(timeValue, colonState ? 0b01000000 : 0b00000000, true);
}

/**
 * 显示AP配网模式
 */
void showAPMode() {
  uint8_t apMode[4] = {
    0x77,  // A
    0x73,  // P
    0x00,  // 空白
    0x00   // 空白
  };
  display.setSegments(apMode);
}

/**
 * 显示WiFi重置指示
 */
void showResetIndicator() {
  uint8_t resetAnim[4] = {
    0x50,  // r
    0x79,  // E
    0x6D,  // S
    0x78   // t
  };
  display.setSegments(resetAnim);
}

// ==================== 时间管理函数 ====================

/**
 * 从NTP服务器更新时间
 */
void updateTimeFromNTP() {
  if (WiFi.status() == WL_CONNECTED) {
    if (timeClient.update()) {
      currentHour = timeClient.getHours();
      currentMinute = timeClient.getMinutes();
      lastNTPUpdate = millis();
      
      Serial.printf("NTP时间已更新: %02d:%02d\n", currentHour, currentMinute);
    } else {
      Serial.println("NTP更新失败");
    }
  }
}

/**
 * 检查是否需要更新NTP时间
 */
void checkNTPUpdate() {
  if (millis() - lastNTPUpdate > NTP_UPDATE_INTERVAL) {
    updateTimeFromNTP();
  }
}

// ==================== 按钮处理函数 ====================

/**
 * 处理按钮输入
 */
void handleButton() {
  bool currentButtonState = (digitalRead(BUTTON_PIN) == LOW);
  
  // 防抖处理
  if (millis() - lastDebounceTime < DEBOUNCE_TIME) {
    return;
  }
  
  if (currentButtonState && !buttonPressed) {
    // 按钮刚按下
    buttonPressed = true;
    buttonPressStart = millis();
    lastDebounceTime = millis();
    Serial.println("按钮按下");
  } 
  else if (!currentButtonState && buttonPressed) {
    // 按钮释放
    buttonPressed = false;
    lastDebounceTime = millis();
    
    unsigned long pressDuration = millis() - buttonPressStart;
    
    if (pressDuration >= WIFI_RESET_TIMEOUT) {
      // 长按：重置WiFi
      showResetIndicator();
      Serial.println("重置WiFi设置...");
      wifiManager.resetSettings();
      delay(1000);
      ESP.restart();
    } else {
      // 短按：切换亮度
      currentBrightnessIndex = (currentBrightnessIndex + 1) % 4;
      display.setBrightness(brightnessLevels[currentBrightnessIndex]);
      Serial.printf("亮度已调整为: %d\n", brightnessLevels[currentBrightnessIndex]);
    }
  }
}

// ==================== 主程序函数 ====================

/**
 * 系统初始化
 */
void setup() {
  // 初始化串口
  Serial.begin(115200);
  Serial.println("\n\nESP8266网络时钟启动中...");
  
  // 初始化硬件
  display.setBrightness(brightnessLevels[currentBrightnessIndex]);
  display.clear();
  showStartupAnimation();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // 配置WiFiManager
  wifiManager.setConfigPortalTimeout(180);  // 3分钟超时
  
  // 设置AP模式回调
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("进入配网模式");
    Serial.println("请连接到 Clock-AP 热点进行配置");
    showAPMode();
  });
  
  // 连接WiFi
  Serial.println("连接WiFi中...");
  if (!wifiManager.autoConnect("Clock-AP")) {
    Serial.println("配网失败，重启设备...");
    delay(1000);
    ESP.restart();
  }
  
  Serial.println("WiFi已连接");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());
  
  // 初始化NTP客户端
  timeClient.begin();
  
  // 获取初始时间
  Serial.println("正在获取网络时间...");
  updateTimeFromNTP();
  
  // 如果获取失败，使用默认时间
  if (currentHour == 0 && currentMinute == 0) {
    currentHour = 12;
    currentMinute = 0;
    Serial.println("使用默认时间 12:00");
  }
  
  updateTimeDisplay();
  Serial.printf("初始显示时间: %02d:%02d\n", currentHour, currentMinute);
}

/**
 * 主循环
 */
void loop() {
  // 处理按钮输入
  handleButton();
  
  // 检查是否需要更新NTP时间
  checkNTPUpdate();
  
  // 处理冒号闪烁
  if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
    colonState = !colonState;
    lastBlinkTime = millis();
    updateTimeDisplay();
  }
  
  // 每分钟更新一次显示（避免频繁刷新）
  static unsigned long lastMinuteUpdate = 0;
  if (millis() - lastMinuteUpdate >= 60000) {
    if (WiFi.status() == WL_CONNECTED) {
      // 获取当前时间（不强制更新NTP）
      currentHour = timeClient.getHours();
      currentMinute = timeClient.getMinutes();
      updateTimeDisplay();
    }
    lastMinuteUpdate = millis();
  }
  
  // 短暂延时，避免CPU占用过高
  delay(10);
}