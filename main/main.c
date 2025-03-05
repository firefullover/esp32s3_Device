#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "mqtt_client.h"
//---------------------MPU6050传感器部分-----------------------------------

#define MPU6050_ADDR        0x68   // I2C 地址（AD0 接地）
#define I2C_MASTER_NUM      0      // 使用 I2C 控制器 0

#define CONFIG_I2C_MASTER_SCL    0        // I2C SCL 引脚
#define CONFIG_I2C_MASTER_SDA    1        // I2C SDA 引脚
#define CONFIG_I2C_MASTER_FREQ_HZ 100000     // I2C 通信频率

static i2c_master_bus_handle_t bus_handle;  // I2C 总线句柄
static i2c_master_dev_handle_t dev_handle;  // MPU6050 设备句柄

//I2C初始化
void i2c_init() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = CONFIG_I2C_MASTER_SDA,
        .scl_io_num = CONFIG_I2C_MASTER_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // 启用内部上拉电阻
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
}

// 初始化 MPU6050
void mpu6050_init() {
    // 添加 MPU6050 设备到总线
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_ADDR,
        .scl_speed_hz = CONFIG_I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    // 唤醒设备（向 0x6B 寄存器写 0x00）
    uint8_t wakeup_cmd[] = {0x6B, 0x00};
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, wakeup_cmd, sizeof(wakeup_cmd), -1));

    // 可选：配置量程（默认 ±2g 和 ±250°/s）
    // uint8_t accel_config[] = {0x1C, 0x00}; // ±2g
    // uint8_t gyro_config[] = {0x1B, 0x00};  // ±250°/s
    // i2c_master_transmit(dev_handle, accel_config, sizeof(accel_config), -1);
    // i2c_master_transmit(dev_handle, gyro_config, sizeof(gyro_config), -1);
}

// 读取原始数据（加速度、陀螺仪、温度）
void mpu6050_read_raw(int16_t *accel, int16_t *gyro, int16_t *temp) {
    // 从寄存器 0x3B 开始读取 14 字节
    uint8_t reg_addr = 0x3B;
    uint8_t data[14];
    ESP_ERROR_CHECK(i2c_master_transmit_receive(
        dev_handle, 
        &reg_addr, 1,          // 发送寄存器地址
        data, sizeof(data),     // 接收数据
        -1
    ));

    // 解析数据（高位在前）
    accel[0] = (data[0] << 8) | data[1];  // X 加速度
    accel[1] = (data[2] << 8) | data[3];  // Y 加速度
    accel[2] = (data[4] << 8) | data[5];  // Z 加速度
    *temp    = (data[6] << 8) | data[7];  // 温度
    gyro[0]  = (data[8] << 8) | data[9];  // X 陀螺仪
    gyro[1]  = (data[10] << 8) | data[11];// Y 陀螺仪
    gyro[2]  = (data[12] << 8) | data[13];// Z 陀螺仪（修正旧代码错误）
}



//---------------------WIFI配置部分-----------------------------------
#define WIFI_SSID_STA      "DragonG"
#define WIFI_PASS_STA      "lrt13729011089"

// Wi-Fi 事件处理函数
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
    int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {  // 如果是 Wi-Fi 事件
        if (event_id == WIFI_EVENT_STA_START) {   // Wi-Fi 站点启动事件
            ESP_LOGI("WiFi_STA", "STA 启动");
            esp_wifi_connect();   // 连接 Wi-Fi
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {   // Wi-Fi 断开连接事件
            ESP_LOGI("WiFi_STA", "已断开 Wi-Fi 连接，正在重新连接...");
            esp_wifi_connect();   // 尝试重新连接 Wi-Fi
        }
    } else if (event_base == IP_EVENT) {   // 如果是 IP 事件
        if (event_id == IP_EVENT_STA_GOT_IP) {   // 获取到 IP 地址事件
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;  // 获取 IP 信息
            ESP_LOGI("WiFi_STA", "Wifi连接，已获取到 IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));  // 打印 IP 地址
        }
    }
}

// STA 模式初始化
void wifi_init_sta() {
    // 初始化 NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_netif_init();
    esp_event_loop_create_default();

    // 仅创建 STA 的网络接口
    esp_netif_create_default_wifi_sta();

    // 初始化 Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    // 配置 STA 参数
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID_STA,
            .password = WIFI_PASS_STA,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        }
    };

    // 设置模式为仅 STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI("WiFi_STA", "STA 模式初始化完成");
}
//--------------------- ST7789 与 SPI 配置 --------------------------------------------

// ST7789 控制引脚定义
#define MOSI_PIN GPIO_NUM_11    // SPI MOSI (SDA)
#define SCLK_PIN GPIO_NUM_12    // SPI SCLK (时钟)
#define RES_PIN  GPIO_NUM_6     // 复位
#define DC_PIN   GPIO_NUM_7     // 数据/命令

// SPI 总线相关
#define SPI_PORT SPI3_HOST
#define MAX_SPI_TRANSFER_SIZE 4096

// 显示尺寸（与 MQTT 图像数据尺寸匹配）
#define WIDTH  240
#define HEIGHT 240

// 声明 SPI 设备句柄
static spi_device_handle_t spi = NULL;

// 初始化 ST7789 控制的 GPIO（DC 与 RES）
void st7789_gpio_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << DC_PIN) | (1ULL << RES_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI("ST7789", "GPIO 初始化完成");
}

// 初始化 SPI 总线与 ST7789 设备
void st7789_spi_init(void) {
    spi_bus_config_t buscfg = {
        .miso_io_num = -1, // 未使用 MISO
        .mosi_io_num = MOSI_PIN,
        .sclk_io_num = SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = WIDTH * HEIGHT * 2,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,  // 10 MHz
        .mode = 3,                          // SPI 模式 3
        .spics_io_num = -1,                 // 不使用 CS 引脚
        .queue_size = 7,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    spi_bus_initialize(SPI_PORT, &buscfg, SPI_DMA_CH_AUTO);
    // SPI 总线初始化

    spi_bus_add_device(SPI_PORT, &devcfg, &spi);
    // SPI 设备添加
}

// 发送命令给 ST7789（命令模式：DC=0）
void st7789_write_command(uint8_t cmd) {
    gpio_set_level(DC_PIN, 0); // 命令模式
    spi_transaction_t t = {0};
    t.length = 8;  // 8 位命令
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(spi, &t);
    // 发送命令
}

// 发送数据给 ST7789（数据模式：DC=1）
void st7789_write_data(const uint8_t *data, int len) {
    gpio_set_level(DC_PIN, 1); // 数据模式
    spi_transaction_t t = {0};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(spi, &t);
    // 发送数据
}

// 初始化 ST7789 显示屏
void st7789_init(void) {
    // 先初始化 GPIO 和 SPI
    st7789_gpio_init();
    st7789_spi_init();

    // 硬件复位
    gpio_set_level(RES_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(RES_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 软件复位
    st7789_write_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    // 退出睡眠模式
    st7789_write_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 设置颜色格式为 16 位
    st7789_write_command(0x3A);
    uint8_t color_mode = 0x55;  // 16 位颜色
    st7789_write_data(&color_mode, 1);

    // 设置 MADCTL 寄存器（屏幕方向，可根据实际情况调整）
    st7789_write_command(0x36);
    uint8_t madctl = 0x60;
    st7789_write_data(&madctl, 1);

    // 开启显示
    st7789_write_command(0x29);
    ESP_LOGI("ST7789", "ST7789 初始化完成");
}

// 清屏函数（填充单一颜色）
void st7789_clear_screen(uint16_t color) {
    ESP_LOGI("ST7789", "清屏，颜色: 0x%04X", color);

    // 设置列地址范围：0 到 WIDTH-1
    uint8_t col_addr_cmd[4] = {0x00, 0x00, 0x00, (uint8_t)(WIDTH - 1)};
    st7789_write_command(0x2A);
    st7789_write_data(col_addr_cmd, 4);

    // 设置行地址范围：0 到 HEIGHT-1
    uint8_t row_addr_cmd[4] = {0x00, 0x00, 0x00, (uint8_t)(HEIGHT - 1)};
    st7789_write_command(0x2B);
    st7789_write_data(row_addr_cmd, 4);

    // 发送写内存命令
    st7789_write_command(0x2C);

    // 准备一行数据缓冲区（每个像素 2 字节）
    uint8_t line_buffer[WIDTH * 2];
    uint8_t color_data[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    color_data[0] ^= 0xFF;
    color_data[1] ^= 0xFF;


    for (int i = 0; i < WIDTH; i++) {
        memcpy(&line_buffer[i * 2], color_data, 2);
    }

    // 逐行发送，填充整个屏幕
    for (int j = 0; j < HEIGHT; j++) {
        st7789_write_data(line_buffer, sizeof(line_buffer));
    }
}

// 绘制图像函数：将 240x240 的 16 位 RGB 数据分块发送到 ST7789
void st7789_draw_image(const uint16_t *image_data, int width, int height) {
    // 设置列地址
    uint8_t col_addr_cmd[4] = {0x00, 0x00, 0x00, (uint8_t)(width - 1)};
    st7789_write_command(0x2A);
    st7789_write_data(col_addr_cmd, 4);

    // 设置行地址
    uint8_t row_addr_cmd[4] = {0x00, 0x00, 0x00, (uint8_t)(height - 1)};
    st7789_write_command(0x2B);
    st7789_write_data(row_addr_cmd, 4);

    // 发送写内存命令
    st7789_write_command(0x2C);

    size_t total_bytes = width * height * sizeof(uint16_t);
    const uint8_t *data_ptr = (const uint8_t *)image_data;
    while (total_bytes > 0) {
        int chunk_size = (total_bytes > MAX_SPI_TRANSFER_SIZE) ? MAX_SPI_TRANSFER_SIZE : total_bytes;
        st7789_write_data(data_ptr, chunk_size);
        data_ptr += chunk_size;
        total_bytes -= chunk_size;
    }
}

// 此处 data 指向一帧 240x240 16 位 RGB 数据
void display_image(const uint8_t *data, size_t len) {
    // 此处无需重新分配内存，直接将接收到的数据转换为 16 位数组进行显示
    const uint16_t *img = (const uint16_t *)data;
    st7789_draw_image(img, WIDTH, HEIGHT);
}

//--------------MQTT协议------------------------------------------------
#define MQTT_URI         "mqtt://192.168.5.64:1883"
#define MQTT_TOPIC       "6050_date"       // 发送6050数据主题
#define MQTT_TOPIC_IMAGE "lcd_image"    // 图像数据主题
#define MQTT_CLIENT_ID   "ESP32_S3_Client" //客户端标识符
#define MQTT_USERNAME    "mymqtt"
#define MQTT_PASSWORD    "138248"

static esp_mqtt_client_handle_t mqtt_client = NULL;

// 图像数据尺寸：240 * 240，16位RGB（2字节/像素）
#define IMG_WIDTH   240
#define IMG_HEIGHT  240
#define IMG_BPP     2
#define EXPECTED_IMG_SIZE  (IMG_WIDTH * IMG_HEIGHT * IMG_BPP)
static uint8_t image_buffer[EXPECTED_IMG_SIZE]; // 图像数据缓存
static size_t received_bytes = 0; // 已接收字节数
static bool receiving_image = false; // 接收状态标志

// 处理图像数据函数（仅验证数据长度并打印日志，实际显示可在此调用 LCD 接口）
void process_image_data(const uint8_t *data, size_t len) {
    if (len != EXPECTED_IMG_SIZE) {
        ESP_LOGE("MQTT", "图像数据长度不匹配: 期望 %d 字节, 实际 %d 字节", EXPECTED_IMG_SIZE, len);
        return;
    }
    ESP_LOGI("MQTT", "接收到一帧图像数据，大小: %d 字节", len);

    // 调用显示函数将图像直接输出到 LCD
    display_image(data, len);
}

// MQTT 事件处理函数
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    mqtt_client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "MQTT 已连接，准备订阅主题");
            // 除了原有传感器数据主题外，还订阅图像数据主题
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_IMAGE, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("MQTT", "MQTT 断开连接");
            break;

        case MQTT_EVENT_DATA:
            // 判断接收的数据属于哪个主题
            // printf("%.*s\n", event->data_len, event->data);
            // if (event->topic_len == strlen(MQTT_TOPIC_IMAGE) &&
            //     strncmp(event->topic, MQTT_TOPIC_IMAGE, event->topic_len) == 0) {
            //     ESP_LOGI("MQTT", "收到图像数据");
            //     process_image_data((const uint8_t *)event->data, event->data_len);
            // }
            // 可在此处处理其他主题的数据，比如传感器数据...
            if (event->topic_len == strlen(MQTT_TOPIC_IMAGE) &&
                strncmp(event->topic, MQTT_TOPIC_IMAGE, event->topic_len) == 0) {
                
                // 检查数据是否超过缓冲区容量
                if (received_bytes + event->data_len > EXPECTED_IMG_SIZE) {
                    ESP_LOGE("MQTT", "数据溢出，已接收: %d, 本次: %d", received_bytes, event->data_len);
                    received_bytes = 0;
                    return;
                }

                // 复制数据到缓冲区
                memcpy(image_buffer + received_bytes, event->data, event->data_len);
                received_bytes += event->data_len;
                ESP_LOGI("MQTT", "累计接收: %d/%d 字节", received_bytes, EXPECTED_IMG_SIZE);

                // 检查数据是否接收完成
                if (received_bytes == EXPECTED_IMG_SIZE) {
                    ESP_LOGI("MQTT", "接收完成，开始显示");
                    display_image(image_buffer, EXPECTED_IMG_SIZE);
                    received_bytes = 0; // 重置计数器
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI("MQTT", "MQTT 发生错误，错误代码: %d", event->error_handle->error_type);
            break;

        default:
            break;
    }
}

// 初始化 MQTT
void mqtt_app_start() {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .buffer.size = 40960,      // 设置输入缓冲区大小
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

//------------------------------------------------------------------------------------
void app_main() {
    wifi_init_sta();// wifi-sta
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 初始化 ST7789 显示屏
    st7789_init();             // 包括 GPIO、SPI、复位、颜色模式设置等
    st7789_clear_screen(0xFFFF); // 清屏为黑色
    // st7789_draw_image(firefly_rgb,WIDTH,HEIGHT);
    mqtt_app_start();// 启动 MQTT

    // while (1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(1000));
        // int64_t start_time = esp_timer_get_time(); // 获取开始时间
        // st7789_draw_image(firefly_rgb,WIDTH,HEIGHT);
        // int64_t end_time = esp_timer_get_time(); // 获取结束时间
        // int64_t elapsed_time = end_time - start_time; // 计算消耗时间
        // printf("代码执行时间: %lld 微秒\n", elapsed_time);  
        // st7789_clear_screen(0x0000); // 清屏为黑色
    // }
    

    ESP_LOGI("I2C", "Initializing I2C...");
    i2c_init();

    ESP_LOGI("MPU6050", "Initializing MPU6050...");
    mpu6050_init();


    
    int16_t accel[3], gyro[3], temp;
    float accel_scale = 16384.0; // ±2g 灵敏度
    float gyro_scale = 131.0;    // ±250°/s 灵敏度

    while (1) {
        mpu6050_read_raw(accel, gyro, &temp);

        // 转换为实际物理值
        float ax = accel[0] / accel_scale;
        float ay = accel[1] / accel_scale;
        float az = accel[2] / accel_scale;
        float gx = gyro[0] / gyro_scale;
        float gy = gyro[1] / gyro_scale;
        float gz = gyro[2] / gyro_scale;
        float temp_C = temp / 340.0 + 36.53;

        // 构建 JSON 数据
        char payload[128]; // 确保足够的缓冲区
        snprintf(payload, sizeof(payload),
                 "{\"accel\":{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f},"
                 "\"gyro\":{\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f},"
                 "\"temp\":%.2f}",
                 ax, ay, az, gx, gy, gz, temp_C);
        printf("%s\n",payload);
        // 发送 MQTT 数据
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, payload, 0, 1, 0);

        // // 输出日志
        // ESP_LOGI("MPU6050", "Accel (g): X=%.2f, Y=%.2f, Z=%.2f", ax, ay, az);
        // ESP_LOGI("MPU6050", "Gyro (°/s): X=%.2f, Y=%.2f, Z=%.2f", gx, gy, gz);
        // ESP_LOGI("MPU6050", "Temperature: %.2f°C", temp_C);

        vTaskDelay(pdMS_TO_TICKS(1000)); // 50ms 采样间隔
    }
}