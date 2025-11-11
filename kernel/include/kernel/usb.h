#ifndef _KERNEL_USB_H
#define _KERNEL_USB_H

#include <stdint.h>

/* USB Request Types */
#define USB_REQ_GET_STATUS        0
#define USB_REQ_CLEAR_FEATURE     1
#define USB_REQ_SET_FEATURE       3
#define USB_REQ_SET_ADDRESS       5
#define USB_REQ_GET_DESCRIPTOR    6
#define USB_REQ_SET_DESCRIPTOR    7
#define USB_REQ_GET_CONFIGURATION 8
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE     10
#define USB_REQ_SET_INTERFACE     11

/* USB Descriptor Types */
#define USB_DESC_DEVICE           1
#define USB_DESC_CONFIGURATION    2
#define USB_DESC_STRING           3
#define USB_DESC_INTERFACE        4
#define USB_DESC_ENDPOINT         5

/* USB Device Classes */
#define USB_CLASS_PER_INTERFACE   0x00
#define USB_CLASS_HID             0x03
#define USB_CLASS_HUB             0x09

/* HID Subclasses */
#define USB_HID_SUBCLASS_BOOT     0x01

/* HID Protocols */
#define USB_HID_PROTOCOL_KEYBOARD 0x01
#define USB_HID_PROTOCOL_MOUSE    0x02

typedef struct {
    uint8_t  length;
    uint8_t  descriptor_type;
    uint16_t bcd_usb;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t  manufacturer_str;
    uint8_t  product_str;
    uint8_t  serial_str;
    uint8_t  num_configurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t  length;
    uint8_t  descriptor_type;
    uint16_t total_length;
    uint8_t  num_interfaces;
    uint8_t  configuration_value;
    uint8_t  configuration_str;
    uint8_t  attributes;
    uint8_t  max_power;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t num_endpoints;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t interface_str;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t endpoint_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed)) usb_device_request_t;

/* USB API */
int usb_init(void);
void usb_poll(void);

#endif
