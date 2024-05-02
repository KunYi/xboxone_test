// to code base on
// https://gist.github.com/xpn/9dca0c1663ecdee76ede
//
// build commands on Ubuntu 22.04
// gcc libusbxboxone.c -o test_xboxone -lusb-1.0
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include <math.h>

#define VID  (0x45e)
#define PID  (0x0b12)


#define M_PI 3.14159265358979323846

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BYTES_PER_SAMPLE 2 // 16-bit samples
#define PACKET_SIZE 64
#define FREQUENCY 1000 // 1 kHz sine wave

static uint8_t buffer[PACKET_SIZE];

static double phase_left = 0.0;
static double phase_right = 0.0; // Initial phase offset for the right channel

static int16_t generate_sine_sample_left(void) {
    double value = sin(2 * M_PI * FREQUENCY * phase_left / SAMPLE_RATE);
    phase_left += 1.0;
    return (int16_t)(value * 32767);
}

static int16_t generate_sine_sample_right(void) {
    double value = sin(2 * M_PI * FREQUENCY * phase_right / SAMPLE_RATE);
    phase_right += 1.0;
    return (int16_t)(value * 32767);
}

static void fill_buffer(void) {
    uint8_t *ptr = buffer;
    for (int i = 0; i < PACKET_SIZE; i += BYTES_PER_SAMPLE * CHANNELS) {
        int16_t sampleL = generate_sine_sample_left();
        int16_t sampleR = generate_sine_sample_right();
        *ptr++ = (uint8_t)(sampleL & 0xFF);
        *ptr++ = (uint8_t)(sampleL >> 8);
        *ptr++ = (uint8_t)(sampleR & 0xFF);
        *ptr++ = (uint8_t)(sampleR >> 8);
    }
}

const char* getEPType(const uint8_t bmAttributes) {
	switch(bmAttributes & 0x03) {
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_CONTROL:
		return "CONTROL";
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS:
		return "ISOCHRONOUS";
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK:
		return "BULK";
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT:
		return "INTERRUPT";
	}
        return "";
}

void printdev(libusb_device *dev) {
	struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(dev, &desc);

        if (!((desc.idVendor == VID) && (desc.idProduct)))
           return;

        printf("Vendor ID: 0x%04X, Product ID: 0x%04X\n", desc.idVendor, desc.idProduct);

	struct libusb_config_descriptor *config;
        libusb_get_config_descriptor(dev, 0, &config);

        for(uint8_t i = 0; i < config->bNumInterfaces; i++) {
                const struct libusb_interface* inter = &config->interface[i];
                printf("Number of alternate settings: %d\n", inter->num_altsetting);
                for(int j = 0; j < inter->num_altsetting; j++) {
                        const struct libusb_interface_descriptor* interdesc = &inter->altsetting[j];
                        printf("Interface Number: %d\n", (int)interdesc->bInterfaceNumber);
                        printf("Number of endpoints: %d\n", (int)interdesc->bNumEndpoints);
                        for(uint8_t k = 0; k < interdesc->bNumEndpoints; k++) {
                                const struct libusb_endpoint_descriptor* epdesc = &interdesc->endpoint[k];
                                printf("\tDescriptor Type: 0x%02X(%d)\n", epdesc->bDescriptorType, (int)epdesc->bDescriptorType);
                                printf("\tEP Type:0x%02X, %s\n", epdesc->bmAttributes, getEPType(epdesc->bmAttributes));
                                printf("\tEP Address: 0x%02X(%d)\n", epdesc->bEndpointAddress, (int)epdesc->bEndpointAddress);
                        }
                }
        }
}

#define OPT        (1u<<5)
#define power_len  (1)
#define volume_unmute_len  (8)

#define sub_audvol        (3)
#define sub_audfmt        (4)

#define audvol_unmute     (4)

#define seq_idx (2)
static uint8_t seq = 0;

static uint8_t xboxone_poweron[] =  { 0x05, OPT, 0, power_len, 0 };

// set unmute. output/chat/input to 60%
static uint8_t xboxone_volumeon[] = { 0x08, OPT, 0, volume_unmute_len, sub_audvol, audvol_unmute, 60, 60, 60, 0, 0, 0};

void print_response(libusb_device_handle* const dev_handle ) {
        int actual;
        uint8_t data[64];

        libusb_interrupt_transfer(dev_handle, 0x82, data, sizeof(data), &actual, 5000);
        printf("Received %d bytes\n", actual);
        for(int x = 0; x < actual; x++) {
                printf("%02x ", data[x]);
        }
        printf("\n");
}

int main(int argc, char **argv) {
	libusb_context *ctx = NULL;
	libusb_device **devs;
	libusb_device_handle *dev_handle;
	uint8_t data[64];
	int actual;

        printf("[Xbox Controller Test]: @KunYi \n\n");

        memset(data, 0xFF, sizeof(data));

        int r = libusb_init(&ctx);

        if (r < 0) {
                printf("[X] Error init libusb\n");
                return 1;
        }

        libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);

        ssize_t cnt = libusb_get_device_list(ctx, &devs);

        if (cnt < 0) {
                printf("[X] libusb_get_device_list failed\n");
                return 1;
        }

        for(ssize_t x=0; x < cnt; x++) {
                printdev(devs[x]);
        }

        // Grab a handle to our XBONE controller
        dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID);

        if (dev_handle == NULL) {
                printf("[X] Cannot open device, ensure XBOX controller is attached\n");
                return 1;
        }

        if (libusb_kernel_driver_active(dev_handle, 0) == 1) {
                printf("[i] Kernel has hold of this device, detaching kernel driver\n");
                libusb_detach_kernel_driver(dev_handle, 0);
        }

        libusb_claim_interface(dev_handle, 0);

        // Start our rumble test
        // Thanks to https://github.com/quantus/xbox-one-controller-protocol

	//  unnecessary, to kernel driver will handle this,
	//  but you can to add xpad in module blacklist
	printf("[!] Power On\r\n");
        xboxone_poweron[seq_idx] = seq++;
	libusb_interrupt_transfer(dev_handle, 0x02, xboxone_poweron, sizeof(xboxone_poweron), &actual, 5000);
        sleep(1);
        print_response(dev_handle);

	printf("[!] Set volume\r\n");
        xboxone_volumeon[seq_idx] = seq++;
        libusb_interrupt_transfer(dev_handle, 0x02, xboxone_volumeon, sizeof(xboxone_volumeon), &actual, 5000);
        sleep(1);
        print_response(dev_handle);
#if 0
        printf("[!] Sending Rumble Test 1.. LT\n");
        libusb_interrupt_transfer(dev_handle, 0x02, "\x09\x08\x00\x09\x00\x0f\x20\x04\x20\x20\xFF\x00", 12, &actual, 5000);

        sleep(3);

        printf("[!] Sending Rumble Test 1.. RT\n");
        libusb_interrupt_transfer(dev_handle, 0x02, "\x09\x08\x00\x09\x00\x0f\x04\x20\x20\x20\xFF\x00", 12, &actual, 5000);
#endif

        // while(1) {
        //         libusb_fill_iso_transfer()
        // }

        while(1) {
                libusb_interrupt_transfer(dev_handle, 0x82, data, sizeof(data), &actual, 5000);

                printf("Received %d bytes\n", actual);
                for(int x = 0; x < actual; x++) {
                        printf("%02x ", data[x]);
                }
                printf("\n");
        }

        libusb_release_interface(dev_handle, 0);

        libusb_exit(ctx);
}
