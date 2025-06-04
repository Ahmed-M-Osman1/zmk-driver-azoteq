#define DT_DRV_COMPAT azoteq_iqs5xx

#include "iqs5xx.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <nrfx_gpiote.h>
#include <zephyr/input/input.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);



static int iqs5xx_write(const struct device *dev, const uint16_t start_addr, uint8_t *buf,
                        uint32_t num_bytes) {

    const struct azoteq_iqs5xx_data *data = dev->data;

    uint8_t addr_buffer[2];
    struct i2c_msg msg[2];

    addr_buffer[1] = start_addr & 0xFF;
    addr_buffer[0] = start_addr >> 8;
    msg[0].buf = addr_buffer;
    msg[0].len = 2U;
    msg[0].flags = I2C_MSG_WRITE;

    msg[1].buf = (uint8_t *)buf;
    msg[1].len = num_bytes;
    msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

    int err = i2c_transfer(dev, msg, 2, AZOTEQ_IQS5XX_ADDR);
    return err;
}



static int iqs5xx_seq_read(const struct device *dev, const uint16_t start, uint8_t *read_buf,
                           const uint8_t len) {
//    const struct azoteq_iqs5xx_data *data = dev->data;
//    const struct azoteq_iqs5xx_config *config = dev->config;
    uint16_t nstart = (start << 8 ) | (start >> 8);
    return i2c_write_read(dev, AZOTEQ_IQS5XX_ADDR, &nstart, sizeof(nstart), read_buf, len);
}



static int read_register_len(const struct device *dev, uint8_t reg, uint8_t *value, u_int32_t num_bytes) {

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }

    struct azoteq_iqs5xx_config *config = (struct azoteq_iqs5xx_config *)dev->config;

    int ret = i2c_burst_read_dt(&config->i2c_bus, reg, value, num_bytes);
    if (ret != 0) {
        LOG_ERR("i2c_write_read FAIL %d\n", ret);
        return ret;
    }

    return 0;
}



static int read_register(const struct device *dev, uint8_t reg, uint16_t *value) {

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }

    struct azoteq_iqs5xx_config *config = (struct azoteq_iqs5xx_config *)dev->config;

    uint16_t data = 0;
    LOG_DBG("&config->i2c_bus: %d", &config->i2c_bus.addr);
    int ret = i2c_burst_read_dt(&config->i2c_bus, reg, &data, sizeof(data));
    if (ret != 0) {
        LOG_DBG("i2c_write_read FAIL %d\n", ret);
        return ret;
    }

//     the register values are returned in big endian (MSB first)
    *value = sys_be16_to_cpu(data);

    return 0;
}

static int write_register(const struct device *dev, uint8_t reg, uint16_t value) {

    if (k_is_in_isr()) {
        return -EWOULDBLOCK;
    }

    struct azoteq_iqs5xx_config *config = (struct azoteq_iqs5xx_config *)dev->config;

    uint8_t data[2] = {0};
    sys_put_be16(value, &data[0]);

    return i2c_burst_write_dt(&config->i2c_bus, reg, &data[0], sizeof(data));
}







int azoteq_iqs5xx_init(const struct device *dev) {
    LOG_WRN("AZOTEQ START");
    struct azoteq_iqs5xx_data *data = dev->data;
    const struct azoteq_iqs5xx_config *config = dev->config;

    if (!device_is_ready( config->i2c_bus.bus)) {
        LOG_WRN("i2c bus not ready!");
        return -EINVAL;
    }

    data->dev = dev;


//    nrf_gpio_cfg_input(config->i2c_bus.bus.., NRF_GPIO_PIN_PULLUP);
//    nrf_gpio_cfg_input(, NRF_GPIO_PIN_PULLUP);

//    uint16_t ic_version = 0;
////    int err = read_register(dev, AZOTEQ_IQS5XX_ADDR, &ic_version);
//    int err = iqs5xx_seq_read(dev, 0x00, &ic_version, 2);
//    if (err != 0) {
//        LOG_WRN("could not get IC version!");
//        return err;
//    }

    write_register(dev, AZOTEQ_IQS5XX_ADDR + END_WINDOW, 0);
    k_msleep(1);


    uint16_t ic_version = 0;
    //    int err = read_register(dev, AZOTEQ_IQS5XX_ADDR, &ic_version);
    int err = read_register(dev, AZOTEQ_IQS5XX_ADDR, &ic_version);
    if (err != 0) {
        LOG_WRN("could not get IC version!");
    }

    LOG_WRN("DEVICE: %d", dev);

    //    k_msleep(1000);
//    uint8_t prod_number[2];
//    int err = iqs5xx_seq_read(dev, 0x00, &prod_number, 1);
//    if (err != 0) {
//        LOG_WRN("could not get register!");
//        return err;
//    }
//    LOG_INF("prod_number %i", prod_number);


//    uint16_t prod_number = 0;
//    int err = i2c_burst_read(dev, 0x74, 0x00, &prod_number, 2);
//    if (err != 0) {
//        LOG_WRN("could not get register!");
//        return err;
//    }
//    LOG_INF("prod_number %i", prod_number);

    int ret = gpio_pin_configure_dt(&config->dr, GPIO_INPUT);
    gpio_init_callback(&data->gpio_cb, azoteq_iqs5xx_gpio_cb, BIT(config->dr.pin));
    if (ret < 0) {
        LOG_ERR("can't configure gpio pin %i", ret);
        return ret;
    }

//    uint8_t buffer[44];
//    ret = iqs5xx_seq_read(dev, 0x000D, buffer, 44);
//    iqs5xx_write(dev, (uint16_t)0xEEEE, 0, 1);

    ret = azoteq_iqs5xx_set_int(dev, true);
    if (ret < 0) {
        LOG_ERR("can't set interrupt %i", ret);
        return ret;
    }

    ret = gpio_add_callback(config->dr.port, &data->gpio_cb);
    if (ret < 0) {
        LOG_ERR("Failed to set DR callback: %d", ret);
        return -EIO;
    }

    k_work_init(&data->work, azoteq_iqs5xx_work_cb);
    LOG_ERR("&data->work init %d", &data->work);

    LOG_WRN("AZOTEQ START %d", config->dr.pin);
    LOG_WRN("AZOTEQ START %d", BIT(config->dr.pin));

    return 0;
}

static int azoteq_iqs5xx_set_int(const struct device *dev, const bool en) {
    const struct azoteq_iqs5xx_config *config = dev->config;
    int ret = gpio_pin_interrupt_configure_dt(&config->dr,
                                              en ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE);
    if (ret < 0) {
        LOG_ERR("can't set interrupt");
    }

    return ret;
}

static void azoteq_iqs5xx_gpio_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct azoteq_iqs5xx_data *data = CONTAINER_OF(cb, struct azoteq_iqs5xx_data, gpio_cb);
    k_work_submit(&data->work);
}

static void azoteq_iqs5xx_work_cb(struct k_work *work) {
    struct azoteq_iqs5xx_data *data = CONTAINER_OF(work, struct azoteq_iqs5xx_data, work);
    struct azoteq_iqs5xx_config *config = data->dev->config;

    azoteq_iqs5xx_report_data(data);
}

static void azoteq_iqs5xx_report_data(const struct azoteq_iqs5xx_data *data) {


//    uint8_t buffer[44];
    uint8_t buffer[27];

//    struct azoteq_iqs5xx_data *data = dev->data;

    uint16_t ic_version = 0;
//    int err = read_register(dev, AZOTEQ_IQS5XX_ADDR, &ic_version);
//    int err = read_register(data->dev, 0x00, &ic_version);
    int err = read_register_len(data->dev, 0x00, buffer, 27);
    if (err != 0) {
        LOG_WRN("t get IC version!");
        return err;
    }
//    LOG_DBG("VERSION: %d", sys_be16_to_cpu(ic_version));
//
//    LOG_ERR("DEV: data->dev: %d", data->dev);

//    int res = iqs5xx_seq_read(data->dev, GestureEvents0_adr, buffer, 44);
//    iqs5xx_write(data->dev, END_WINDOW, 0, 1);
//    if (res < 0) {
//        LOG_ERR("\ntrackpad res: %d", res);
//        return res;
//    }


    uint16_t *version = &buffer[0];
    LOG_WRN("VERS: %i", sys_be16_to_cpu(*version));
    uint8_t fingers = buffer[17];
    LOG_WRN("FINGS: %i", fingers);

//    int16_t *relX = &buffer[0x14];
//    LOG_WRN("relX %i", sys_be16_to_cpu(*relX));
    int16_t relX = buffer[0x12] << 8 | buffer[0x12 + 1];
    LOG_WRN("relX %i", relX);

//    input_report_rel(data->dev, INPUT_REL_X, relX, true, K_FOREVER);
//    zmk_hid_report_key();
    zmk_hid_mouse_movement_update(relX, 0);


    input_report_rel(data->dev, INPUT_REL_Y, 10, false, K_NO_WAIT);
//    LOG_WRN("ddddXXX %i", zmk_hid_mouse_report()->body->d_x);
    input_report_rel(data->dev, INPUT_REL_X, 10, false, K_NO_WAIT);
    input_report_rel(data->dev, INPUT_REL_WHEEL, 10, true, K_NO_WAIT);

    input_report_key(data->dev, INPUT_BTN_2, 0, true, K_NO_WAIT);

    int16_t absX = buffer[0x16] << 8 | buffer[0x16 + 1];
    LOG_WRN("absXXX %i", absX);

    zmk_hid_mouse_movement_update(relX, 0);
    zmk_usb_hid_send_mouse_report();
//    input_report_rel(data->dev, INPUT_REL_Y, 0, false, K_FOREVER);
//    input_report_rel(data->dev, INPUT_REL_X, 10, true, K_FOREVER);

//    struct input_listener_config *config = 0;
//    input_handler();



    //    azoteq_iqs5xx_set_int(&dev, true);
}

//        .i2c_bus = DT_INST_ON_BUS(n, i2c),
//        .i2c_bus = I2C_DT_SPEC_INST_GET(n),
//        .i2c_bus = DEVICE_DT_GET(DT_BUS(DT_DRV_INST(n))),
#define AZOTEQ_IQS5XX_INST(n)                                                                      \
    static struct azoteq_iqs5xx_data azoteq_iqs5xx_data_##n;                                       \
    static const struct azoteq_iqs5xx_config azoteq_iqs5xx_config_##n = {                          \
        .i2c_bus = I2C_DT_SPEC_INST_GET(n),                                                      \
        .dr = GPIO_DT_SPEC_GET_OR(DT_DRV_INST(n), dr_gpios, {}),                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, azoteq_iqs5xx_init, NULL, &azoteq_iqs5xx_data_##n, \
                          &azoteq_iqs5xx_config_##n, POST_KERNEL, 95, NULL);

DT_INST_FOREACH_STATUS_OKAY(AZOTEQ_IQS5XX_INST)
