/*
 *  HID driver for Asus notebook built-in keyboard.
 *  Fixes small logical maximum to match usage maximum.
 *
 *  Currently supported devices are:
 *    EeeBook X205TA
 *    VivoBook E200HA
 *
 *  Copyright (c) 2016 Yusuke Fujimaki <usk.fujimaki@gmail.com>
 *
 *  This module based on hid-ortek by
 *  Copyright (c) 2010 Johnathon Harris <jmharris@gmail.com>
 *  Copyright (c) 2011 Jiri Kosina
 *
 *  This module has been updated to add support for Asus i2c touchpad.
 *
 *  Copyright (c) 2016 Brendan McGrath <redmcg@redmandi.dyndns.org>
 *  Copyright (c) 2016 Victor Vlasenko <victor.vlasenko@sysgears.com>
 *  Copyright (c) 2016 Frederik Wenigwieser <frederik.wenigwieser@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/input/mt.h>

#include "hid-ids.h"

MODULE_AUTHOR("Yusuke Fujimaki <usk.fujimaki@gmail.com>");
MODULE_AUTHOR("Brendan McGrath <redmcg@redmandi.dyndns.org>");
MODULE_AUTHOR("Victor Vlasenko <victor.vlasenko@sysgears.com>");
MODULE_AUTHOR("Frederik Wenigwieser <frederik.wenigwieser@gmail.com>");
MODULE_DESCRIPTION("Asus HID Keyboard and TouchPad");

#define FEATURE_REPORT_ID 0x0d
#define INPUT_REPORT_ID 0x5d

#define INPUT_REPORT_SIZE 28

#define MAX_CONTACTS 5

#define MAX_X 0x0aea
#define MAX_Y 0x06de
#define MAX_TOUCH_MAJOR 8
#define MAX_PRESSURE 0x80


#define REPORT_ID_OFFSET 0

#define CONTACT_DOWN_OFFSET 1
#define CONTACT_DOWN_MASK 0x08

#define BTN_LEFT_OFFSET 1
#define BTN_LEFT_MASK 0x01

#define CONTACT_DATA_OFFSET 2
#define CONTACT_DATA_SIZE 5

#define CONTACT_TOOL_TYPE_OFFSET 3
#define CONTACT_TOOL_TYPE_MASK 0x80

#define CONTACT_X_MSB_OFFSET 0
#define CONTACT_X_MSB_BIT_SHIFT 4
#define CONTACT_X_LSB_OFFSET 1

#define CONTACT_Y_MSB_OFFSET 0
#define CONTACT_Y_MSB_MASK 0x0f
#define CONTACT_Y_LSB_OFFSET 2

#define CONTACT_TOUCH_MAJOR_OFFSET 3
#define CONTACT_TOUCH_MAJOR_BIT_SHIFT 4
#define CONTACT_TOUCH_MAJOR_MASK 0x07

#define CONTACT_PRESSURE_OFFSET 4
#define CONTACT_PRESSURE_MASK 0x7f

#define BYTE_BIT_SHIFT 8
#define TRKID_SGN       ((TRKID_MAX + 1) >> 1)

static void asus_report_contact_down(struct input_dev *input,
		int toolType, u8 *data)
{
	int x, y, touch_major, pressure;

	x = data[CONTACT_X_MSB_OFFSET] >> CONTACT_X_MSB_BIT_SHIFT;
	x = (x << BYTE_BIT_SHIFT) | data[CONTACT_X_LSB_OFFSET];

	y = (data[CONTACT_Y_MSB_OFFSET] & CONTACT_Y_MSB_MASK);
	y = MAX_Y - ((y << BYTE_BIT_SHIFT) | data[CONTACT_Y_LSB_OFFSET]);

	if (toolType == MT_TOOL_PALM) {
		touch_major = MAX_TOUCH_MAJOR;
		pressure = MAX_PRESSURE;
	} else {
		touch_major = data[CONTACT_TOUCH_MAJOR_OFFSET];
		touch_major >>= CONTACT_TOUCH_MAJOR_BIT_SHIFT;
		touch_major &= CONTACT_TOUCH_MAJOR_MASK;
		pressure = data[CONTACT_PRESSURE_OFFSET] & CONTACT_PRESSURE_MASK;
	}

	input_report_abs(input, ABS_MT_POSITION_X, x);
	input_report_abs(input, ABS_MT_POSITION_Y, y);
	input_report_abs(input, ABS_MT_TOUCH_MAJOR, touch_major);
	input_report_abs(input, ABS_MT_PRESSURE, pressure);
}

/* Required for Synaptics Palm Detection */
static void asus_report_tool_width(struct input_dev *input)
{
	struct input_mt *mt = input->mt;
	struct input_mt_slot *oldest;
	int oldid, count, i;

	oldest = NULL;
	oldid = mt->trkid;
	count = 0;

	for (i = 0; i < mt->num_slots; ++i) {
		struct input_mt_slot *ps = &mt->slots[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);

		if (id < 0)
			continue;
		if ((id - oldid) & TRKID_SGN) {
			oldest = ps;
			oldid = id;
		}
		count++;
	}

	if (oldest) {
		input_report_abs(input, ABS_TOOL_WIDTH,
			input_mt_get_value(oldest, ABS_MT_TOUCH_MAJOR));
	}
}

static void asus_report_input(struct input_dev *input, u8 *data)
{
	int i;
	u8 *contactData = data + CONTACT_DATA_OFFSET;

	for (i = 0; i < MAX_CONTACTS; i++) {
		bool down = data[CONTACT_DOWN_OFFSET] &
				(CONTACT_DOWN_MASK << i);
		int toolType = contactData[CONTACT_TOOL_TYPE_OFFSET] &
				CONTACT_TOOL_TYPE_MASK ? MT_TOOL_PALM
				: MT_TOOL_FINGER;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, toolType, down);

		if (down) {
			asus_report_contact_down(input, toolType, contactData);
			contactData += CONTACT_DATA_SIZE;
		}
	}

	input_report_key(input, BTN_LEFT,
			data[BTN_LEFT_OFFSET] & BTN_LEFT_MASK);
	asus_report_tool_width(input);

	input_mt_sync_frame(input);
	input_sync(input);
}

static int asus_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	if (hdev->product == USB_DEVICE_ID_ASUSTEK_TOUCHPAD &&
			 data[REPORT_ID_OFFSET] == INPUT_REPORT_ID &&
						size == INPUT_REPORT_SIZE) {
		struct hid_input *hidinput;

		list_for_each_entry(hidinput, &hdev->inputs, list) {
			asus_report_input(hidinput->input, data);
		}
		return 1;
	}

	return 0;
}

static int asus_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	if (hdev->product == USB_DEVICE_ID_ASUSTEK_TOUCHPAD) {
		int ret;
		struct input_dev *input = hi->input;

		input->name = "Asus TouchPad";

		input_set_abs_params(input, ABS_MT_POSITION_X, 0, MAX_X, 0, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0, MAX_Y, 0, 0);
		input_set_abs_params(input, ABS_TOOL_WIDTH, 0, MAX_TOUCH_MAJOR, 0, 0);
		input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, MAX_TOUCH_MAJOR, 0, 0);
		input_set_abs_params(input, ABS_MT_PRESSURE, 0, MAX_PRESSURE, 0, 0);

		__set_bit(BTN_LEFT, input->keybit);
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

		ret = input_mt_init_slots(input, MAX_CONTACTS, INPUT_MT_POINTER);

		if (ret) {
			hid_err(hdev, "Asus input mt init slots failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int asus_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit,
		int *max)
{
	if (hdev->product == USB_DEVICE_ID_ASUSTEK_TOUCHPAD) {
		/* Don't map anything from the HID report.
		 * We do it all manually in asus_input_configured
		 */
		return -1;
	}

	return 0;
}

static int asus_start_multitouch(struct hid_device *hdev)
{
	unsigned char buf[] = { FEATURE_REPORT_ID, 0x00, 0x03, 0x01, 0x00 };
	int ret = hid_hw_raw_request(hdev, FEATURE_REPORT_ID, buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret != sizeof(buf)) {
		hid_err(hdev, "Asus failed to start multitouch: %d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM
static int asus_reset_resume(struct hid_device *hdev)
{
	if (hdev->product == USB_DEVICE_ID_ASUSTEK_TOUCHPAD)
		return asus_start_multitouch(hdev);

	return 0;
}
#endif

static int asus_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	if (hdev->product == USB_DEVICE_ID_ASUSTEK_TOUCHPAD)
		hdev->quirks = HID_QUIRK_NO_INIT_REPORTS;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Asus hid parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "Asus hw start failed: %d\n", ret);
		return ret;
	}


	if (hdev->product == USB_DEVICE_ID_ASUSTEK_TOUCHPAD) {
		ret = asus_start_multitouch(hdev);
		if (ret)
			goto err_stop_hw;
	}

	return 0;
err_stop_hw:
	hid_hw_stop(hdev);
	return ret;
}

static __u8 *asus_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (hdev->product == USB_DEVICE_ID_ASUSTEK_NOTEBOOK_KEYBOARD &&
			*rsize >= 56 && rdesc[54] == 0x25 && rdesc[55] == 0x65) {
		hid_info(hdev, "Fixing up Asus notebook report descriptor\n");
		rdesc[55] = 0xdd;
	}
	return rdesc;
}

static const struct hid_device_id asus_devices[] = {
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_NOTEBOOK_KEYBOARD) },
	{ HID_I2C_DEVICE(USB_VENDOR_ID_ASUSTEK, USB_DEVICE_ID_ASUSTEK_TOUCHPAD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, asus_devices);

static struct hid_driver asus_driver = {
	.name			= "hid-asus",
	.id_table		= asus_devices,
	.report_fixup		= asus_report_fixup,
	.probe                  = asus_probe,
	.input_mapping          = asus_input_mapping,
	.input_configured       = asus_input_configured,
#ifdef CONFIG_PM
	.reset_resume           = asus_reset_resume,
#endif
	.raw_event		= asus_raw_event
};
module_hid_driver(asus_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("acpi*:FTE1*:*");