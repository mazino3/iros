#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/hal/input.h>
#include <kernel/hal/output.h>
#include <kernel/hal/x86/drivers/mouse.h>
#include <kernel/hal/x86/drivers/ps2.h>
#include <kernel/hal/x86/drivers/vmware_back_door.h>
#include <kernel/irqs/handlers.h>
#include <kernel/net/umessage.h>
#include <kernel/util/init.h>

struct mouse_data {
    struct hw_device hw_device;
    struct mouse_event_queue *start;
    struct mouse_event_queue *end;
    spinlock_t queue_lock;
    struct ps2_controller *controller;
    struct ps2_port *port;
    struct irq_handler irq_handler;
    struct mouse_event event;
    bool has_scroll_wheel;
    uint8_t index;
    uint8_t buffer[4];
};

static void add_mouse_event(struct mouse_data *data, struct mouse_event *event) {
    (void) data;
    struct queued_umessage *umessage =
        net_create_umessage(UMESSAGE_INPUT, UMESSAGE_INPUT_MOUSE_EVENT, 0, sizeof(struct umessage_input_mouse_event), event);
    net_post_umessage(umessage);
    net_drop_umessage(umessage);
}

bool on_interrupt(struct irq_context *context) {
    struct mouse_data *data = context->closure;

    uint8_t mouse_data;
    if (data->controller->read_byte(&mouse_data)) {
        return true;
    }

    if (vmmouse_is_enabled()) {
        struct mouse_event *event = vmmouse_read();
        if (event) {
            add_mouse_event(data, event);
        }
        return true;
    }

    switch (data->index) {
        case 0:
            if (!(mouse_data & (1 << 3))) {
                return true;
            }
            // Fall through
        case 1:
            data->buffer[data->index++] = mouse_data;
            return true;
        case 2:
            data->buffer[data->index++] = mouse_data;
            if (data->has_scroll_wheel) {
                return true;
            }
            break;
        case 3:
            assert(data->has_scroll_wheel);
            data->buffer[data->index++] = mouse_data;
            break;
        default:
            assert(false);
    }

    if (data->has_scroll_wheel) {
        if (data->buffer[3] == 0x1) {
            data->event.dz = 1;
        } else if (data->buffer[3] == 0xFF) {
            data->event.dz = -1;
        }
    }

    data->event.buttons = data->buffer[0] & 0b11;

    if (data->buffer[0] & 0xC0) {
        data->event.dx = 0;
        data->event.dy = 0;
    } else {
        data->event.dx = (int) data->buffer[1] - ((((int) data->buffer[0]) << 4) & 0x100);
        data->event.dy = (int) data->buffer[2] - ((((int) data->buffer[0]) << 3) & 0x100);
    }

    data->event.scale_mode = SCALE_DELTA;
    add_mouse_event(data, &data->event);
    data->index = 0;
    return true;
}

static void mouse_create(struct ps2_controller *controller, struct ps2_port *port) {
    struct mouse_data *data = calloc(1, sizeof(struct mouse_data));
    init_hw_device(&data->hw_device, "PS/2 Mouse", &controller->hw_device, hw_device_id_ps2(port->id), NULL, NULL);
    data->hw_device.status = HW_STATUS_ACTIVE;
    init_spinlock(&data->queue_lock);
    data->controller = controller;
    data->port = port;
    data->irq_handler.closure = data;
    data->irq_handler.flags = IRQ_HANDLER_EXTERNAL;
    data->irq_handler.handler = on_interrupt;
    register_irq_handler(&data->irq_handler, port->irq);

    controller->send_command(PS2_DEVICE_COMMAND_SET_DEFAULTS, port->port_number);

    controller->send_command(PS2_DEVICE_COMMAND_SET_SAMPLE_RATE, port->port_number);
    controller->send_command(200, port->port_number);

    controller->send_command(PS2_DEVICE_COMMAND_SET_SAMPLE_RATE, port->port_number);
    controller->send_command(100, port->port_number);

    controller->send_command(PS2_DEVICE_COMMAND_SET_SAMPLE_RATE, port->port_number);
    controller->send_command(80, port->port_number);

    controller->send_command(PS2_DEVICE_COMMAND_ID, port->port_number);

    uint8_t mouse_id = 0xFF;
    uint8_t scratch;
    controller->read_byte(&mouse_id);
    controller->read_byte(&scratch);
    controller->read_byte(&scratch);

    data->has_scroll_wheel = mouse_id == 3;
    controller->send_command(PS2_DEVICE_COMMAND_ENABLE_SCANNING, port->port_number);
}

static struct ps2_device_id mouse_ids[] = {
    { 0x00, 0x00 },
    { 0x03, 0x00 },
    { 0x04, 0x00 },
};

static struct ps2_driver mouse_driver = {
    .name = "PS/2 Mouse",
    .device_id_list = mouse_ids,
    .device_id_count = sizeof(mouse_ids) / sizeof(struct ps2_device_id),
    .create = mouse_create,
};

static void init_mouse() {
    ps2_register_driver(&mouse_driver);
}
INIT_FUNCTION(init_mouse, driver);
