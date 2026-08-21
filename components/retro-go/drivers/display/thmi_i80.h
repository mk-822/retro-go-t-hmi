#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// The panel IO queue and this semaphore are deliberately kept at two entries. This gives the
// renderer one DMA transfer in flight while it fills the other buffer, matching Anemoia's
// T-HMI I80 implementation without requiring a full-screen framebuffer.
#define T_HMI_I80_QUEUE_DEPTH       2
#define T_HMI_I80_MAX_TRANSFER_BYTES (LCD_BUFFER_LENGTH * sizeof(uint16_t))
#define T_HMI_I80_PIXEL_CLOCK_HZ    (20 * 1000 * 1000)

static esp_lcd_i80_bus_handle_t i80_bus;
static esp_lcd_panel_io_handle_t i80_panel_io;
static SemaphoreHandle_t i80_free_buffers;
static uint16_t *i80_buffers[T_HMI_I80_QUEUE_DEPTH];
static size_t i80_next_buffer;
static bool i80_first_color;
static bool i80_active;

#define ILI9341_CMD(cmd, data...)                         \
    do                                                     \
    {                                                      \
        const uint8_t command = (cmd);                    \
        const uint8_t parameters[] = {data};              \
        esp_lcd_panel_io_tx_param(i80_panel_io, command,   \
                                  sizeof(parameters) ? parameters : NULL, \
                                  sizeof(parameters));     \
    } while (0)

static bool IRAM_ATTR i80_color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                               esp_lcd_panel_io_event_data_t *event_data,
                                               void *user_ctx)
{
    (void)panel_io;
    (void)event_data;
    (void)user_ctx;
    BaseType_t high_priority_task_woken = pdFALSE;
    if (i80_free_buffers)
        xSemaphoreGiveFromISR(i80_free_buffers, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

static void i80_release_resources(void)
{
    if (i80_panel_io)
    {
        esp_lcd_panel_io_del(i80_panel_io);
        i80_panel_io = NULL;
    }
    if (i80_bus)
    {
        esp_lcd_del_i80_bus(i80_bus);
        i80_bus = NULL;
    }
    if (i80_free_buffers)
    {
        vSemaphoreDelete(i80_free_buffers);
        i80_free_buffers = NULL;
    }
    for (size_t i = 0; i < T_HMI_I80_QUEUE_DEPTH; ++i)
    {
        free(i80_buffers[i]);
        i80_buffers[i] = NULL;
    }
    i80_next_buffer = 0;
    i80_active = false;
}

static void i80_wait_idle(void)
{
    if (!i80_active)
        return;

    xSemaphoreTake(i80_free_buffers, portMAX_DELAY);
    xSemaphoreTake(i80_free_buffers, portMAX_DELAY);
    xSemaphoreGive(i80_free_buffers);
    xSemaphoreGive(i80_free_buffers);
}

static void i80_init(void)
{
#ifdef RG_GPIO_LCD_BCKL
    ledc_timer_config(&(ledc_timer_config_t){
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    });
    ledc_channel_config(&(ledc_channel_config_t){
        .gpio_num = RG_GPIO_LCD_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    #ifdef RG_GPIO_LCD_BCKL_INVERT
        .flags.output_invert = 1,
    #endif
    });
    ledc_fade_func_install(0);
#endif

    i80_free_buffers = xSemaphoreCreateCounting(T_HMI_I80_QUEUE_DEPTH, T_HMI_I80_QUEUE_DEPTH);
    RG_ASSERT(i80_free_buffers != NULL, "T-HMI I80 semaphore allocation failed");
    for (size_t i = 0; i < T_HMI_I80_QUEUE_DEPTH; ++i)
    {
        i80_buffers[i] = rg_alloc(T_HMI_I80_MAX_TRANSFER_BYTES, MEM_DMA);
        RG_ASSERT(i80_buffers[i] != NULL, "T-HMI I80 buffer allocation failed");
    }

    const esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = RG_GPIO_LCD_DC,
        .wr_gpio_num = RG_GPIO_LCD_WR,
        .clk_src = LCD_CLK_SRC_PLL160M,
        .data_gpio_nums = {
            RG_GPIO_LCD_D0, RG_GPIO_LCD_D1, RG_GPIO_LCD_D2, RG_GPIO_LCD_D3,
            RG_GPIO_LCD_D4, RG_GPIO_LCD_D5, RG_GPIO_LCD_D6, RG_GPIO_LCD_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = T_HMI_I80_MAX_TRANSFER_BYTES,
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };

    esp_err_t err = esp_lcd_new_i80_bus(&bus_config, &i80_bus);
    RG_ASSERT(err == ESP_OK, "T-HMI I80 bus init failed");

    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = RG_GPIO_LCD_CS,
        .pclk_hz = T_HMI_I80_PIXEL_CLOCK_HZ,
        .trans_queue_depth = T_HMI_I80_QUEUE_DEPTH,
        .on_color_trans_done = i80_color_transfer_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .cs_active_high = 0,
            .pclk_active_neg = 0,
            .pclk_idle_low = 0,
            // Keep the byte order used by Retro-Go's existing display pipeline. The T-HMI
            // issue is the panel's RGB/BGR color order, handled by MADCTL below; swapping
            // bytes here corrupts the RGB565 bit fields and reverses the tonal gradients.
            .swap_color_bytes = 0,
            .reverse_color_bits = 0,
        },
    };

    err = esp_lcd_new_panel_io_i80(i80_bus, &io_config, &i80_panel_io);
    RG_ASSERT(err == ESP_OK, "T-HMI I80 panel IO init failed");
    i80_active = true;
    i80_next_buffer = 0;
    i80_first_color = true;

#ifdef RG_GPIO_LCD_RST
    gpio_set_direction(RG_GPIO_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(RG_GPIO_LCD_RST, 0);
    rg_usleep(100 * 1000);
    gpio_set_level(RG_GPIO_LCD_RST, 1);
    rg_usleep(10 * 1000);
#endif

    ILI9341_CMD(0x01);       // Software reset
    rg_usleep(5 * 1000);
    ILI9341_CMD(0x3A, 0x55); // RGB565
#ifdef RG_SCREEN_INIT
    RG_SCREEN_INIT();
#endif
    ILI9341_CMD(0x11);       // Exit sleep
    rg_usleep(10 * 1000);
    ILI9341_CMD(0x29);       // Display on
}

static void i80_deinit(void)
{
    if (!i80_active)
    {
        i80_release_resources();
        return;
    }
    i80_wait_idle();
    i80_release_resources();
}

static void lcd_set_backlight(float percent)
{
    const float level = RG_MIN(RG_MAX(percent / 100.f, 0), 1.f);
#ifdef RG_GPIO_LCD_BCKL
    const int error_code = ledc_set_fade_time_and_start(
        LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0x1FFF * level, 50, 0);
    if (error_code)
        RG_LOGE("failed setting backlight to %d%% (0x%02X)\n", (int)(100 * level), error_code);
#else
    (void)level;
#endif
}

static void lcd_set_window(int left, int top, int width, int height)
{
    const int right = left + width - 1;
    const int bottom = top + height - 1;

    if (left < 0 || top < 0 || width <= 0 || height <= 0 ||
        right >= display.screen.real_width || bottom >= display.screen.real_height)
    {
        RG_LOGW("Bad lcd window (x0=%d, y0=%d, x1=%d, y1=%d)\n", left, top, right, bottom);
        return;
    }

    const uint8_t columns[] = {left >> 8, left, right >> 8, right};
    const uint8_t rows[] = {top >> 8, top, bottom >> 8, bottom};
    esp_lcd_panel_io_tx_param(i80_panel_io, 0x2A, columns, sizeof(columns));
    esp_lcd_panel_io_tx_param(i80_panel_io, 0x2B, rows, sizeof(rows));
    i80_first_color = true;
}

static inline uint16_t *lcd_get_buffer(size_t length)
{
    (void)length;
    if (i80_active)
        xSemaphoreTake(i80_free_buffers, portMAX_DELAY);
    return i80_buffers[i80_next_buffer++ % T_HMI_I80_QUEUE_DEPTH];
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    if (!buffer)
        return;

    if (!length)
    {
        if (i80_active)
            xSemaphoreGive(i80_free_buffers);
        return;
    }

    const esp_err_t err = esp_lcd_panel_io_tx_color(
        i80_panel_io, i80_first_color ? 0x2C : 0x3C, buffer, length * sizeof(uint16_t));
    i80_first_color = false;
    if (err != ESP_OK)
    {
        RG_LOGE("T-HMI I80 color transfer failed (0x%x)\n", err);
        xSemaphoreGive(i80_free_buffers);
    }
}

static void lcd_sync(void)
{
    i80_wait_idle();
}

static void lcd_init(void)
{
    i80_init();
}

static void lcd_deinit(void)
{
    i80_deinit();
}
