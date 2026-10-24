// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared backend for the wheels that speak the T300RS protocol but need the
 * T248 style setup sequence. The individual wheels only provide a struct
 * tm_wheel_desc with their report descriptor and limits.
 */
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

#define TM_COMMON_MAX_EFFECTS 16
#define TM_COMMON_BUFFER_LENGTH 63

static const u8 setup_0[64] = { 0x42, 0x01 };
static const u8 setup_1[64] = { 0x0a, 0x04, 0x90, 0x03 };
static const u8 setup_2[64] = { 0x0a, 0x04, 0x00, 0x0c };
static const u8 setup_3[64] = { 0x0a, 0x04, 0x12, 0x10 };
static const u8 setup_4[64] = { 0x0a, 0x04, 0x00, 0x06 };
static const u8 setup_5[64] = { 0x0a, 0x04, 0x00, 0x0e };
static const u8 setup_6[64] = { 0x0a, 0x04, 0x00, 0x0e, 0x01 };
static const u8 *const setup_arr[] = { setup_0, setup_1, setup_2, setup_3, setup_4, setup_5, setup_6 };
static const unsigned int setup_arr_sizes[] = {
	ARRAY_SIZE(setup_0),
	ARRAY_SIZE(setup_1),
	ARRAY_SIZE(setup_2),
	ARRAY_SIZE(setup_3),
	ARRAY_SIZE(setup_4),
	ARRAY_SIZE(setup_5),
	ARRAY_SIZE(setup_6)
};

static const signed short tm_common_effects[] = {
	FF_CONSTANT,
	FF_RAMP,
	FF_SPRING,
	FF_DAMPER,
	FF_FRICTION,
	FF_INERTIA,
	FF_PERIODIC,
	FF_SINE,
	FF_TRIANGLE,
	FF_SQUARE,
	FF_SAW_UP,
	FF_SAW_DOWN,
	FF_AUTOCENTER,
	FF_GAIN,
	-1
};

static int tm_common_interrupts(struct t300rs_device_entry *wheel)
{
	u8 *send_buf = kmalloc(256, GFP_KERNEL);
	struct usb_interface *usbif = to_usb_interface(wheel->hdev->dev.parent);
	struct usb_host_endpoint *ep;
	int ret, trans, b_ep, i;

	if (!send_buf) {
		hid_err(wheel->hdev, "failed allocating send buffer\n");
		return -ENOMEM;
	}

	ep = &usbif->cur_altsetting->endpoint[1];
	b_ep = ep->desc.bEndpointAddress;

	for (i = 0; i < ARRAY_SIZE(setup_arr); ++i) {
		memcpy(send_buf, setup_arr[i], setup_arr_sizes[i]);

		ret = usb_interrupt_msg(wheel->usbdev,
				usb_sndintpipe(wheel->usbdev, b_ep),
				send_buf, setup_arr_sizes[i],
				&trans,
				USB_CTRL_SET_TIMEOUT);

		if (ret) {
			hid_err(wheel->hdev, "setup data couldn't be sent\n");
			goto err;
		}
	}

err:
	kfree(send_buf);
	return ret;
}

static int tm_common_wheel_destroy(void *data)
{
	struct t300rs_device_entry *wheel = data;

	if (!wheel)
		return -ENODEV;

	kfree(wheel->send_buffer);
	kfree(wheel);
	return 0;
}

static int tm_common_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *wheel = data;
	const struct tm_wheel_desc *desc = wheel->desc;

	if (value < desc->range_min) {
		hid_info(wheel->hdev, "value %i too small, clamping to %i\n",
				value, desc->range_min);
		value = desc->range_min;
	}

	if (value > desc->range_max) {
		hid_info(wheel->hdev, "value %i too large, clamping to %i\n",
				value, desc->range_max);
		value = desc->range_max;
	}

	return t300rs_set_range(data, value);
}

static int tm_common_send_open(struct t300rs_device_entry *wheel)
{
	int r1, r2;
	wheel->send_buffer[0] = 0x01;
	wheel->send_buffer[1] = 0x04;
	if ((r1 = t300rs_send_int(wheel)))
		return r1;

	wheel->send_buffer[0] = 0x01;
	wheel->send_buffer[1] = 0x05;
	if ((r2 = t300rs_send_int(wheel)))
		return r2;

	return 0;
}

static int tm_common_open(void *data, int open_mode)
{
	struct t300rs_device_entry *wheel = data;

	if (!wheel)
		return -ENODEV;

	if (open_mode)
		tm_common_send_open(wheel);

	return wheel->open(wheel->input_dev);
}

static int tm_common_send_close(struct t300rs_device_entry *wheel)
{
	int r1, r2;
	wheel->send_buffer[0] = 0x01;
	wheel->send_buffer[1] = 0x05;
	if ((r1 = t300rs_send_int(wheel)))
		return r1;

	wheel->send_buffer[0] = 0x01;
	wheel->send_buffer[1] = 0x00;
	if ((r2 = t300rs_send_int(wheel)))
		return r2;

	return 0;
}

static int tm_common_close(void *data, int open_mode)
{
	struct t300rs_device_entry *wheel = data;

	if (!wheel)
		return -ENODEV;

	if (open_mode)
		tm_common_send_close(wheel);

	wheel->close(wheel->input_dev);
	return 0;
}

static int tm_common_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	const struct tm_wheel_desc *desc = tmff2->wheel_desc;
	struct t300rs_device_entry *wheel = kzalloc(sizeof(struct t300rs_device_entry),
						   GFP_KERNEL);
	struct list_head *report_list;
	int ret;


	if (!wheel) {
		ret = -ENOMEM;
		goto wheel_err;
	}

	wheel->hdev = tmff2->hdev;
	wheel->input_dev = tmff2->input_dev;
	wheel->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
	wheel->buffer_length = TM_COMMON_BUFFER_LENGTH;
	wheel->desc = desc;

	wheel->send_buffer = kzalloc(wheel->buffer_length, GFP_KERNEL);
	if (!wheel->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	report_list = &wheel->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	wheel->report = list_entry(report_list->next, struct hid_report, list);
	wheel->ff_field = wheel->report->field[0];

	wheel->open = wheel->input_dev->open;
	wheel->close = wheel->input_dev->close;

	if ((ret = tm_common_interrupts(wheel)))
		goto interrupt_err;

	/* everything went OK */
	tmff2->data = wheel;
	tmff2->params = desc->params;
	tmff2->max_effects = TM_COMMON_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, tm_common_effects, sizeof(tm_common_effects));

	if (!open_mode)
		tm_common_send_open(wheel);

	hid_info(wheel->hdev, "force feedback for %s\n", desc->name);
	return 0;

interrupt_err:
send_err:
	kfree(wheel);
wheel_err:
	hid_err(tmff2->hdev, "failed initializing %s\n", desc->name);
	return ret;
}

static __u8 *tm_common_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	/* the driver data is set before hid_parse() calls the fixup */
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);

	if (!tmff2 || !tmff2->wheel_desc)
		return rdesc;

	*rsize = tmff2->wheel_desc->rsize;
	return tmff2->wheel_desc->rdesc;
}

int tm_common_populate_api(struct tmff2_device_entry *tmff2,
		const struct tm_wheel_desc *desc)
{
	tmff2->wheel_desc = desc;

	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_autocenter = t300rs_set_autocenter;
	tmff2->set_range = tm_common_set_range;
	tmff2->wheel_fixup = tm_common_wheel_fixup;

	tmff2->open = tm_common_open;
	tmff2->close = tm_common_close;

	tmff2->wheel_init = tm_common_wheel_init;
	tmff2->wheel_destroy = tm_common_wheel_destroy;

	tmff2->alt_mode_store = desc->alt_mode_store;

	return 0;
}
