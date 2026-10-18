// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

#define TMTSXW_MAX_EFFECTS 16
#define TMTSXW_BUFFER_LENGTH 63

static const unsigned long tsxw_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_RANGE
	| PARAM_GAIN
	;

static const signed short tsxw_effects[] = {
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

/* TODO: sort through this stuff */
static u8 tsxw_pc_rdesc_fixed[] = {
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x04, /* Usage (Joystick) */
	0xa1, 0x01, /* Collection (Application) */
	0x09, 0x01, /* Usage (Pointer) */
	0xa1, 0x00, /* Collection (Physical) */
	0x85, 0x07, /* Report ID (7) */
	0x09, 0x30, /* Usage (X) */
	0x15, 0x00, /* Logical minimum (0) */
	0x27, 0xff, 0xff, 0x00, 0x00, /* Logical maximum (65535) */
	0x35, 0x00, /* Physical minimum (0) */
	0x47, 0xff, 0xff, 0x00, 0x00, /* Physical maximum (65535) */
	0x75, 0x10, /* Report size (16) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x35, /* Usage (Rz) (Brake) */
	0x26, 0xff, 0x03, /* Logical maximum (1023) */
	0x46, 0xff, 0x03, /* Physical maximum (1023) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x32, /* Usage (Z) (Gas) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x31, /* Usage (Y) (Clutch) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) */
	0x05, 0x09, /* Usage page (Button) */
	0x19, 0x01, /* Usage minimum (1) */
	0x29, 0x0d, /* Usage maximum (13) */
	0x25, 0x01, /* Logical maximum (1) */
	0x45, 0x01, /* Physical maximum (1) */
	0x75, 0x01, /* Report size (1) */
	0x95, 0x0d, /* Report count (13) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x75, 0x0b, /* Report size (13) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x03, /* Usage (Variable, Absolute, Constant) */
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x39, /* Usage (Hat Switch) */
	0x25, 0x07, /* Logical maximum (7) */
	0x46, 0x3b, 0x01, /* Physical maximum (315) */
	0x55, 0x00, /* Unit exponent (0) */
	0x65, 0x14, /* Unit (Eng Rot, Angular Pos) */
	0x75, 0x04, /* Report size (4) */
	0x81, 0x42, /* Input (Variable, Absolute, NullState) */
	0x65, 0x00, /* Unit (None) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) */
	0x85, 0x60, /* Report ID (96), prev 10 */
	0x06, 0x00, 0xff, /* Usage page (Vendor 1) */
	0x09, 0x60, /* Usage (96), prev 10 */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x3f, /* Report count (63) */
	0x26, 0xff, 0x7f, /* Logical maximum (32767) */
	0x15, 0x00, /* Logical minimum (0) */
	0x46, 0xff, 0x7f, /* Physical maximum (32767) */
	0x36, 0x00, 0x80, /* Physical minimum (-32768) */
	0x91, 0x02, /* Output (Variable, Absolute) */
	0x85, 0x02, /* Report ID (2) */
	0x09, 0x02, /* Usage (2) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x14, /* Usage (20) */
	0x85, 0x14, /* Report ID (20) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0xc0, /* End collection */
	0xc0, /* End collection */
};

static int tsxw_wheel_destroy(void *data)
{
	struct t300rs_device_entry *t300rs = data;

	if (!t300rs)
		return -ENODEV;

	kfree(t300rs->send_buffer);
	kfree(t300rs);
	return 0;
}

static int tsxw_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *tsxw = data;

	if (value < 140) {
		hid_info(tsxw->hdev, "value %i too small, clamping to 140\n", value);
		value = 140;
	}

	if (value > 1080) {
		hid_info(tsxw->hdev, "value %i too large, clamping to 1080\n", value);
		value = 1080;
	}

	return t300rs_set_range(data, value);
}

static int tsxw_send_open(struct t300rs_device_entry *tsxw)
{
	int r1, r2;
	tsxw->send_buffer[0] = 0x01;
	tsxw->send_buffer[1] = 0x04;
	if ((r1 = t300rs_send_int(tsxw)))
		return r1;

	tsxw->send_buffer[0] = 0x01;
	tsxw->send_buffer[1] = 0x05;
	if ((r2 = t300rs_send_int(tsxw)))
		return r2;

	return 0;
}

static int tsxw_open(void *data, int open_mode)
{
	struct t300rs_device_entry *tsxw = data;

	if (!tsxw)
		return -ENODEV;

	if (open_mode)
		tsxw_send_open(tsxw);

	return tsxw->open(tsxw->input_dev);
}

static int tsxw_send_close(struct t300rs_device_entry *tsxw)
{
	int r1, r2;
	tsxw->send_buffer[0] = 0x01;
	tsxw->send_buffer[1] = 0x05;
	if ((r1 = t300rs_send_int(tsxw)))
		return r1;

	tsxw->send_buffer[0] = 0x01;
	tsxw->send_buffer[1] = 0x00;
	if ((r2 = t300rs_send_int(tsxw)))
		return r2;

	return 0;
}

static int tsxw_close(void *data, int open_mode)
{
	struct t300rs_device_entry *tsxw = data;

	if (!tsxw)
		return -ENODEV;

	if (open_mode)
		tsxw_send_close(tsxw);

	tsxw->close(tsxw->input_dev);
	return 0;
}

static int tsxw_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t300rs_device_entry *tsxw = kzalloc(sizeof(struct t300rs_device_entry),
						   GFP_KERNEL);
	int ret;

	if (!tsxw) {
		ret = -ENOMEM;
		goto tsxw_err;
	}

	if ((ret = t300rs_init_common(tmff2, tsxw, TMTSXW_BUFFER_LENGTH)))
		goto init_err;

	if ((ret = t300rs_send_setup_interrupts(tsxw)))
		goto init_err;

	/* everything went OK */
	tmff2->data = tsxw;
	tmff2->params = tsxw_params;
	tmff2->max_effects = TMTSXW_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, tsxw_effects, sizeof(tsxw_effects));

	if (!open_mode)
		tsxw_send_open(tsxw);

	hid_info(tsxw->hdev, "force feedback for TS-XW\n");
	return 0;

init_err:
	kfree(tsxw->send_buffer);
	kfree(tsxw);
tsxw_err:
	hid_err(tmff2->hdev, "failed initializing TS-XW\n");
	return ret;
}

static __u8 *tsxw_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	rdesc = tsxw_pc_rdesc_fixed;
	*rsize = sizeof(tsxw_pc_rdesc_fixed);
	return rdesc;
}

int tsxw_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_autocenter = t300rs_set_autocenter;
	/* TS-XW has 1080 degree range, just like T300RS 1080 */
	tmff2->set_range = tsxw_set_range;
	tmff2->wheel_fixup = tsxw_wheel_fixup;

	tmff2->open = tsxw_open;
	tmff2->close = tsxw_close;

	tmff2->wheel_init = tsxw_wheel_init;
	tmff2->wheel_destroy = tsxw_wheel_destroy;

	return 0;
}
