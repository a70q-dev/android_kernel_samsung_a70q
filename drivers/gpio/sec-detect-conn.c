/**
 * sec-detect-conn.c
 *
 * Copyright (C) 2017 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define DEBUG_FOR_SECDETECT
#include <linux/sec-detect-conn.h>
#include <linux/delay.h>

static int detect_conn_enabled;
struct detect_conn_info *gpinfo;

#define SEC_CONN_PRINT(format, ...) pr_info("[SEC_Detect_Conn] " format, ##__VA_ARGS__)

#ifdef CONFIG_OF
static const struct of_device_id sec_detect_conn_dt_match[] = {
	{ .compatible = "samsung,sec_detect_conn" },
	{ }
};
#endif

static int sec_detect_conn_pm_suspend(struct device *dev)
{
	return 0;
}

static int sec_detect_conn_pm_resume(struct device *dev)
{
	return 0;
}

static int sec_detect_conn_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct dev_pm_ops sec_detect_conn_pm = {
	.suspend = sec_detect_conn_pm_suspend,
	.resume = sec_detect_conn_pm_resume,
};

/**
 * Send an uevent about given gpio pin number.
 */
void send_uevent_by_num(int num, struct detect_conn_info *pinfo, int level)
{
	char *uevent_conn_str[3] = {"", "", NULL};
	char uevent_dev_str[UEVENT_CONN_MAX_DEV_NAME];
	char uevent_dev_type_str[UEVENT_CONN_MAX_DEV_NAME];

	/*Send Uevent Data*/
	sprintf(uevent_dev_str, "CONNECTOR_NAME=%s", pinfo->pdata->name[num]);
	uevent_conn_str[0] = uevent_dev_str;
	if (level == 1)
		sprintf(uevent_dev_type_str, "CONNECTOR_TYPE=HIGH_LEVEL");
	else if (level == 0)
		sprintf(uevent_dev_type_str, "CONNECTOR_TYPE=LOW_LEVEL");

	uevent_conn_str[1] = uevent_dev_type_str;

	kobject_uevent_env(&pinfo->dev->kobj, KOBJ_CHANGE, uevent_conn_str);

	if (level == 1)
		SEC_CONN_PRINT("send uevent pin[%d]:CONNECTOR_NAME=%s,CONNECTOR_TYPE=HIGH_LEVEL.\n"
			, num, pinfo->pdata->name[num]);
	else if (level == 0)
		SEC_CONN_PRINT("send uevent pin[%d]:CONNECTOR_NAME=%s,CONNECTOR_TYPE=LOW_LEVEL.\n"
			, num, pinfo->pdata->name[num]);
}

/**
 * Check and send uevent from irq handler.
 */
void check_and_send_uevent_irq(int irq, struct detect_conn_info *pinfo, int type)
{
	int i;

	/*Send Uevent Data*/
	for (i = 0; i < pinfo->pdata->gpio_total_cnt; i++) {
		if (irq == pinfo->pdata->irq_number[i]) {
			/* if Rising or falling edge occurs, check level one more.
			 * and if it still level high send uevent.
			 */
			usleep_range(1 * 1000, 1 * 1000);
			if (gpio_get_value(pinfo->pdata->irq_gpio[i]))
				send_uevent_by_num(i, pinfo, 1);

			SEC_CONN_PRINT("%s status changed.\n", pinfo->pdata->name[i]);
		}
	}
}

/**
 * Send uevent from irq handler.
 */
void send_uevent_edge_irq(int irq, struct detect_conn_info *pinfo, int type)
{
	char *uevent_conn_str[3] = {"", "", NULL};
	char uevent_dev_str[UEVENT_CONN_MAX_DEV_NAME];
	char uevent_dev_type_str[UEVENT_CONN_MAX_DEV_NAME];

	int i;

	/*Send Uevent Data*/
	for (i = 0; i < pinfo->pdata->gpio_total_cnt; i++) {
		if (irq == pinfo->pdata->irq_number[i]) {
			sprintf(uevent_dev_str, "CONNECTOR_NAME=%s", pinfo->pdata->name[i]);
			if (type == IRQ_TYPE_EDGE_RISING) {
				sprintf(uevent_dev_type_str, "CONNECTOR_TYPE=RISING_EDGE");
				SEC_CONN_PRINT("send uevent irq[%d]:CONNECTOR_NAME=%s,CONNECTOR_TYPE=RISING_EDGE.\n"
					, irq, pinfo->pdata->name[i]);
			} else if (type == IRQ_TYPE_EDGE_FALLING) {
				sprintf(uevent_dev_type_str, "CONNECTOR_TYPE=FALLING_EDGE");
				SEC_CONN_PRINT("send uevent irq[%d]:CONNECTOR_NAME=%s,CONNECTOR_TYPE=FALLING_EDGE.\n"
					, irq, pinfo->pdata->name[i]);
			} else if (type == IRQ_TYPE_EDGE_BOTH) {
				sprintf(uevent_dev_type_str, "CONNECTOR_TYPE=EDGE_BOTH");
				SEC_CONN_PRINT("send uevent irq[%d]:CONNECTOR_NAME=%s,CONNECTOR_TYPE=ALL_EDGE.\n"
					, irq, pinfo->pdata->name[i]);
			} else {
				SEC_CONN_PRINT("Err:Unknown type irq : irq[%d]:CONNECTOR_NAME=%s,CONNECTOR_TYPE=%d.\n"
					, irq, pinfo->pdata->name[i], type);
				return;
			}
			uevent_conn_str[0] = uevent_dev_str;
			uevent_conn_str[1] = uevent_dev_type_str;

			kobject_uevent_env(&pinfo->dev->kobj, KOBJ_CHANGE, uevent_conn_str);
			SEC_CONN_PRINT("%s status changed.\n", pinfo->pdata->name[i]);
		}
	}
}
/**
 * Called when the connector pin state changes.
 */
static irqreturn_t detect_conn_interrupt_handler(int irq, void *handle)
{
	int type;
	struct detect_conn_info *pinfo = handle;

	if (detect_conn_enabled != 0) {
		SEC_CONN_PRINT("%s\n", __func__);

		type = irq_get_trigger_type(irq);
		check_and_send_uevent_irq(irq, pinfo, type);
	}

	return IRQ_HANDLED;
}

/**
 * Enable all gpio pin IRQ which is from Device Tree.
 */
int detect_conn_irq_enable(struct detect_conn_info *pinfo, bool enable, int pin)
{
	int retval = 0;
	int i;

	if (enable) {
		if (pin < pinfo->pdata->gpio_total_cnt) {
			retval = request_threaded_irq(pinfo->pdata->irq_number[pin], NULL,
					detect_conn_interrupt_handler,
					pinfo->pdata->irq_type[pin] | IRQF_ONESHOT,
					pinfo->pdata->name[pin], pinfo);

			if (retval) {
				SEC_CONN_PRINT("%s: Failed to request threaded irq %d.\n",
						__func__, retval);

				return retval;
			}

#if defined(DEBUG_FOR_SECDETECT)
			SEC_CONN_PRINT("%s: Succeeded to request threaded irq %d: irq_num[%d], type[%x],name[%s].\n",
						__func__, retval, pinfo->pdata->irq_number[pin],
						pinfo->pdata->irq_type[pin], pinfo->pdata->name[pin]);
#endif
			pinfo->irq_enabled[pin] = true;
		}
	} else {
		for (i = 0; i < pinfo->pdata->gpio_total_cnt; i++) {
			if (pinfo->irq_enabled[i]) {
				disable_irq(pinfo->pdata->irq_number[i]);

				free_irq(pinfo->pdata->irq_number[i], pinfo);

				pinfo->irq_enabled[i] = false;
			}
		}
	}

	return retval;
}

/**
 * Triggered when "enabled" node is set.
 * When enabling this node, check and send an uevent if the pin level is high.
 * And then gpio pin interrupt is enabled.
 */
static ssize_t store_detect_conn_enabled(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct detect_conn_info *pinfo;
	struct sec_det_conn_p_data *pdata;
	int ret;
	int i;
	int bufLength;
	int pinNameLength;

	if (gpinfo == 0)
		return -1;

	pinfo = gpinfo;
	pdata = pinfo->pdata;

	bufLength = strlen(buf);
#if defined(DEBUG_FOR_SECDETECT)
	SEC_CONN_PRINT("buf = %s\n", buf);
	SEC_CONN_PRINT("bufLength = %d\n", bufLength);
#endif

	/*Disable irq when "enabled" value set to 0*/
	if (!strncmp(buf, "0", 1)) {
		SEC_CONN_PRINT("SEC Detect connector driver disable.\n");
		detect_conn_enabled = 0;

		ret = detect_conn_irq_enable(pinfo, false, 0);

		if (ret) {
			SEC_CONN_PRINT("Interrupt not disabled.\n");
			return ret;
		}

	} else {
		for (i = 0; i < pdata->gpio_total_cnt; i++) {
			pinNameLength = strlen(pdata->name[i]);
#if defined(DEBUG_FOR_SECDETECT)
			SEC_CONN_PRINT("pinName = %s\n", pdata->name[i]);
			SEC_CONN_PRINT("pinNameLength = %d\n", pinNameLength);
#endif
			if (pinNameLength == bufLength) {
				if (!strncmp(buf, pdata->name[i], bufLength)) {
					SEC_CONN_PRINT("%s driver enabled.\n", buf);
					detect_conn_enabled |= (1 << i);

#if defined(DEBUG_FOR_SECDETECT)
					SEC_CONN_PRINT("gpio level [%d] = %d\n", pdata->irq_gpio[i],
						gpio_get_value(pdata->irq_gpio[i]));
#endif
					/*get level value of the gpio pin.*/
					/*if there's gpio low pin, send uevent*/
					if (gpio_get_value(pdata->irq_gpio[i]))
						send_uevent_by_num(i, pinfo, 1);
					else
						send_uevent_by_num(i, pinfo, 0);

					/*Enable interrupt.*/
					ret = detect_conn_irq_enable(pinfo, true, i);

					if (ret < 0) {
						SEC_CONN_PRINT("%s Interrupt not enabled.\n", buf);
						return ret;
					}
				}
			}

			/* For ALL_CONNECT input, enable all nodes except already enabled node. */
			if (bufLength >= 11) {
				if (!strncmp(buf, "ALL_CONNECT", 11)) { //bufLength
					if (!(detect_conn_enabled & (1 << i))) {
						SEC_CONN_PRINT("%s driver enabled.\n", buf);
						detect_conn_enabled |= (1 << i);

#if defined(DEBUG_FOR_SECDETECT)
						SEC_CONN_PRINT("gpio level [%d] = %d\n", pdata->irq_gpio[i],
							gpio_get_value(pdata->irq_gpio[i]));
#endif
						/*get level value of the gpio pin.*/
						/*if there's gpio low pin, send uevent*/
						if (gpio_get_value(pdata->irq_gpio[i]))
							send_uevent_by_num(i, pinfo, 1);
						else
							send_uevent_by_num(i, pinfo, 0);

						/*Enable interrupt.*/
						ret = detect_conn_irq_enable(pinfo, true, i);

						if (ret < 0) {
							SEC_CONN_PRINT("%s Interrupt not enabled.\n", buf);
							return ret;
						}
					}
				}
			}
		}
	}

	return count;
}

static ssize_t show_detect_conn_enabled(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", detect_conn_enabled);
}

static DEVICE_ATTR(enabled, 0644, show_detect_conn_enabled, store_detect_conn_enabled);

/**
 * Reading this "available_pins" helps user to know available pins which support connection detection.
 * This function prints available pin names.
 */
static ssize_t show_detect_conn_available(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct sec_det_conn_p_data *pdata;
	struct detect_conn_info *pinfo;
	char available_pins_string[1024] = {0, };
	int i;

	if (gpinfo == 0)
		return -1;

	pinfo = gpinfo;
	pdata = pinfo->pdata;

	for (i = 0; i < pdata->gpio_total_cnt; i++) {
		SEC_CONN_PRINT("pinName = %s\n", pdata->name[i]);
		strcat(available_pins_string, pdata->name[i]);
		strcat(available_pins_string, "/");
	}

	available_pins_string[strlen(available_pins_string)-1] = '\0';

	return sprintf(buf, "%s\n", available_pins_string);
}
static DEVICE_ATTR(available_pins, 0444, show_detect_conn_available, NULL);

#ifdef CONFIG_OF
/**
 * Parse the device tree and get gpio number, irq type.
 * Request gpio
 */
static int detect_conn_parse_dt(struct device *dev)
{
	struct sec_det_conn_p_data *pdata = dev->platform_data;
	struct device_node *np = dev->of_node;
	int i;
	int retval = 0;
	struct pinctrl *conn_pinctrl;
	struct pinctrl *pm_conn_pinctrl;

	pdata->gpio_cnt = of_gpio_named_count(np, "sec,det_conn_gpios");

	if (pdata->gpio_cnt < 1) {
		SEC_CONN_PRINT("detect_pinctrl_init failed gpio_cnt < 1 %d.\n", pdata->gpio_cnt);
		pdata->gpio_cnt = 0;
		/*return 0;*/
	}

	pdata->gpio_total_cnt = pdata->gpio_cnt;

	/* Setting pinctrl state to NO PULL */
	conn_pinctrl = devm_pinctrl_get_select(dev, "det_ap_connect");
	if (IS_ERR_OR_NULL(conn_pinctrl)) {
		SEC_CONN_PRINT("detect_pinctrl_init failed.\n");
		/*return -EINVAL;*/
	}

	for (i = 0; i < pdata->gpio_cnt; i++) {
		/*Get connector name*/
		of_property_read_string_index(np, "sec,det_conn_name", i, &(pdata->name[i]));

		/*Get connector gpio number*/
		pdata->irq_gpio[i] = of_get_named_gpio(np, "sec,det_conn_gpios", i);

		if (gpio_is_valid(pdata->irq_gpio[i])) {
			retval = gpio_request_one(pdata->irq_gpio[i], GPIOF_DIR_IN, pdata->name[i]);

			if (retval) {
				dev_err(dev, "%s: Unable to request %s int [%d]\n",
						__func__, pdata->name[i], pdata->irq_gpio[i]);
				return -EINVAL;
			}

#if defined(DEBUG_FOR_SECDETECT)
			SEC_CONN_PRINT("i = [%d] gpio level [%d] = %d\n", i, pdata->irq_gpio[i],
				gpio_get_value(pdata->irq_gpio[i]));
			SEC_CONN_PRINT("gpio irq gpio = [%d], irq = [%d]\n", pdata->irq_gpio[i],
			gpio_to_irq(pdata->irq_gpio[i]));
			pdata->irq_type[i] = IRQ_TYPE_EDGE_BOTH;
#endif
			/*Filling the irq_number from this gpio.*/
			pdata->irq_number[i] = gpio_to_irq(pdata->irq_gpio[i]);

		} else {
			dev_err(dev, "%s: Failed to get irq gpio.\n", __func__);
			return -EINVAL;
		}
	}

	/*Get type of gpio irq*/
	/*Do not use dtsi temporarily*/
	/*if (pdata->gpio_cnt > 0) {
		if (of_property_read_u32_array(np, "sec,det_conn_irq_type", pdata->irq_type, pdata->gpio_cnt)) {
			dev_err(dev, "%s, Failed to get irq_type property!\n", __func__);
			return -EINVAL;
		}
	}*/

	/* Get pm_gpio */
	pdata->gpio_pm_cnt = of_gpio_named_count(np, "sec,det_pm_conn_gpios");

	if (pdata->gpio_pm_cnt < 1) {
		SEC_CONN_PRINT("pm detect_pinctrl_init failed pm cnt < 1. %d\n", pdata->gpio_pm_cnt);
		pdata->gpio_pm_cnt = 0;
		/*return 0;*/
	}

	/* Setting pinctrl state to NO PULL */
	pm_conn_pinctrl = devm_pinctrl_get_select(dev, "det_pm_connect");
	if (IS_ERR_OR_NULL(pm_conn_pinctrl)) {
		SEC_CONN_PRINT("pm detect_pinctrl_init failed.\n");
		/*return 0;*/
	}

	pdata->gpio_total_cnt = pdata->gpio_total_cnt + pdata->gpio_pm_cnt;
#if defined(DEBUG_FOR_SECDETECT)
	SEC_CONN_PRINT("gpio_total_count = %d", pdata->gpio_total_cnt);
#endif

	for (i = pdata->gpio_cnt; i < pdata->gpio_total_cnt; i++) {
		/*Get connector name*/
		of_property_read_string_index(np, "sec,det_pm_conn_name", i - pdata->gpio_cnt, &(pdata->name[i]));

		/*Get connector gpio number*/
		pdata->irq_gpio[i] = of_get_named_gpio(np, "sec,det_pm_conn_gpios", i - pdata->gpio_cnt);
		if (gpio_is_valid(pdata->irq_gpio[i])) {
			retval = gpio_request_one(pdata->irq_gpio[i], GPIOF_DIR_IN, pdata->name[i]);

			if (retval) {
				dev_err(dev, "%s: Unable to request %s int [%d]\n",
						__func__, pdata->name[i], pdata->irq_gpio[i]);
				return -EINVAL;
			}
			SEC_CONN_PRINT("i = [%d], gpio level [%d] = %d\n", i, pdata->irq_gpio[i],
				gpio_get_value(pdata->irq_gpio[i]));
			SEC_CONN_PRINT("gpio irq gpio = [%d], irq = [%d]\n", pdata->irq_gpio[i],
			gpio_to_irq(pdata->irq_gpio[i]));
		} else {
			SEC_CONN_PRINT("pm gpio[%d] invalid\n", i - pdata->gpio_cnt);
			return -EINVAL;
		}

		pdata->irq_number[i] = gpio_to_irq(pdata->irq_gpio[i]);
		pdata->irq_type[i] = IRQ_TYPE_EDGE_BOTH;
	}
	return 0;
}
#endif

static int sec_detect_conn_probe(struct platform_device *pdev)
{
	struct sec_det_conn_p_data *pdata;
	struct detect_conn_info *pinfo;
	int ret;

	SEC_CONN_PRINT("%s\n", __func__);

	/* First Get the GPIO pins; if it fails, we'll defer the probe. */
	if (pdev->dev.of_node) {
		pdata = devm_kzalloc(&pdev->dev,
				sizeof(struct sec_det_conn_p_data), GFP_KERNEL);

		if (!pdata) {
			dev_err(&pdev->dev, "Failed to allocate platform data.\n");
			return -ENOMEM;
		}

		pdev->dev.platform_data = pdata;
#if CONFIG_OF
		ret = detect_conn_parse_dt(&pdev->dev);
#else
		ret = 0;
#endif
		if (ret) {
			dev_err(&pdev->dev, "Failed to parse dt data.\n");
			return ret;
		}

		pr_info("%s: parse dt done.\n", __func__);
	} else {
		pdata = pdev->dev.platform_data;
	}

	if (!pdata) {
		dev_err(&pdev->dev, "There are no platform data.\n");
		return -EINVAL;
	}

	pinfo = devm_kzalloc(&pdev->dev, sizeof(struct detect_conn_info), GFP_KERNEL);

	if (!pinfo) {
		SEC_CONN_PRINT("pinfo : failed to allocate pinfo.\n");
		return -ENOMEM;
	}

	/* Create sys device /sys/class/sec/sec_detect_conn */
	pinfo->dev = sec_device_create(0, NULL, "sec_detect_conn");

	if (unlikely(IS_ERR(pinfo->dev))) {
		pr_err("%s Failed to create device(sec_detect_conn).\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	/* Create sys node /sys/class/sec/sec_detect_conn/enabled */
	ret = device_create_file(pinfo->dev, &dev_attr_enabled);

	if (ret) {
		dev_err(&pdev->dev, "%s: Failed to create device file.\n", __func__);
		goto err_create_detect_conn_sysfs;
	}

	/* Create sys node /sys/class/sec/sec_detect_conn/available_pins */
	ret = device_create_file(pinfo->dev, &dev_attr_available_pins);

	if (ret) {
		dev_err(&pdev->dev, "%s: Failed to create device file.\n", __func__);
		goto err_create_detect_conn_sysfs;
	}

	/*save pinfo data to pdata to interrupt enable*/
	pdata->pinfo = pinfo;

	/*save pdata data to pinfo for enable node*/
	pinfo->pdata = pdata;

	/* save pinfo to gpinfo to enabled node*/
	gpinfo = pinfo;

	return ret;

err_create_detect_conn_sysfs:
	sec_device_destroy(pinfo->dev->devt);

out:
	gpinfo = 0;

	return ret;
}

static struct platform_driver sec_detect_conn_driver = {
	.probe = sec_detect_conn_probe,
	.remove = sec_detect_conn_remove,
	.driver = {
		.name = "sec_detect_conn",
		.owner = THIS_MODULE,
#if defined(CONFIG_PM)
		.pm	= &sec_detect_conn_pm,
#endif
#if CONFIG_OF
		.of_match_table = of_match_ptr(sec_detect_conn_dt_match),
#endif
	},
};

static int __init sec_detect_conn_init(void)
{
	SEC_CONN_PRINT("%s\n", __func__);

	return platform_driver_register(&sec_detect_conn_driver);
}

static void __exit sec_detect_conn_exit(void)
{
	return platform_driver_unregister(&sec_detect_conn_driver);
}

module_init(sec_detect_conn_init);
module_exit(sec_detect_conn_exit);

MODULE_DESCRIPTION("Samsung Detecting Connector Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
