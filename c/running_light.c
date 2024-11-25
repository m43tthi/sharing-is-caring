///*
// ============================================================================
// Name        : running_light.c
// Author      : matthiasWeber
// Version     :
// Copyright   : Your copyright notice
// Description : Running Light in C
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <signal.h>
#include <poll.h>

// GPIO chip paths
#define GPIO_CHIP1 "/dev/gpiochip1"
#define GPIO_CHIP3 "/dev/gpiochip3"

// GPIO Line Offsets for LEDs
#define LED1 11
#define LED2 14
#define LED3 13
#define LED4 4

// GPIO Line Offsets for Buttons
#define BUTTON1 10
#define BUTTON2 31
#define BUTTON3 15
#define BUTTON4 7

// Timing Constants
#define INITIAL_DELAY 500000 // Microseconds for 1Hz
#define MIN_DELAY 31250     // Microseconds for 100Hz (maximum speed)
#define MAX_DELAY 500000    // Microseconds for 1Hz (minimum speed)

// Global Control Variables
volatile int running = 1;    // Flag to run the application
volatile int stop = 0;       // 1 = stopped, 0 = running
volatile int direction = 1;  // 1 = forward, -1 = backward
volatile int delay = INITIAL_DELAY;

// Signal Handler for Clean Exit
void handle_signal(int signal) {
    running = 0;
}

// Function to configure GPIO lines for output
int cfg_gpio_output(int chip_fd, struct gpiohandle_request *req, int *pins, int pin_count) {
    memset(req, 0, sizeof(*req));
    for (int i = 0; i < pin_count; i++) {
        req->lineoffsets[i] = pins[i];
    }
    req->lines = pin_count;
    req->flags = GPIOHANDLE_REQUEST_OUTPUT;
    strcpy(req->consumer_label, "running_light");

    if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, req) < 0) {
        perror("Failed to get GPIO handle");
        return -1;
    }
    return 0;
}

// Function to configure GPIO lines for input with events
int cfg_gpio_input_event(int chip_fd, struct gpioevent_request *event_req, int pin) {
    memset(event_req, 0, sizeof(*event_req));
    event_req->lineoffset = pin;
    event_req->handleflags = GPIOHANDLE_REQUEST_INPUT;
    event_req->eventflags = GPIOEVENT_REQUEST_FALLING_EDGE;
    strcpy(event_req->consumer_label, "button_event");

    if (ioctl(chip_fd, GPIO_GET_LINEEVENT_IOCTL, event_req) < 0) {
        perror("Failed to configure GPIO event");
        return -1;
    }
    return 0;
}

// Function to turn off all LEDs
void turn_off_leds(struct gpiohandle_request *req_chip1, struct gpiohandle_request *req_chip3) {
    struct gpiohandle_data data_chip1 = {0};
    struct gpiohandle_data data_chip3 = {0};

    ioctl(req_chip1->fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data_chip1);
    ioctl(req_chip3->fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data_chip3);
}

// Main Program
int main() {
    signal(SIGINT, handle_signal); // Handle Ctrl+C for exit

    // GPIO configuration
    int chip1_fd = open(GPIO_CHIP1, O_RDWR);
    int chip3_fd = open(GPIO_CHIP3, O_RDWR);
    if (chip1_fd < 0 || chip3_fd < 0) {
        perror("Failed to open GPIO chip");
        return EXIT_FAILURE;
    }

    int leds_chip1[] = {LED1};
    int leds_chip3[] = {LED2, LED3, LED4};
    struct gpiohandle_request req_chip1, req_chip3;
    struct gpioevent_request event_button1, event_button2, event_button3, event_button4;

    // Configure LEDs on chip1
    if (cfg_gpio_output(chip1_fd, &req_chip1, leds_chip1, 1) < 0) {
        close(chip1_fd);
        close(chip3_fd);
        return EXIT_FAILURE;
    }

    // Configure LEDs on chip3
    if (cfg_gpio_output(chip3_fd, &req_chip3, leds_chip3, 3) < 0) {
        close(chip1_fd);
        close(chip3_fd);
        return EXIT_FAILURE;
    }

    // Configure Buttons for event detection
    if (cfg_gpio_input_event(chip1_fd, &event_button1, BUTTON1) < 0 ||
        cfg_gpio_input_event(chip1_fd, &event_button2, BUTTON2) < 0 ||
        cfg_gpio_input_event(chip1_fd, &event_button3, BUTTON3) < 0 ||
        cfg_gpio_input_event(chip3_fd, &event_button4, BUTTON4) < 0) {
        close(chip1_fd);
        close(chip3_fd);
        return EXIT_FAILURE;
    }

    // Poll for button events
    struct pollfd pfds[4] = {
        {event_button1.fd, POLLIN, 0},
        {event_button2.fd, POLLIN, 0},
        {event_button3.fd, POLLIN, 0},
        {event_button4.fd, POLLIN, 0}
    };

    // LED sequence control
    struct gpiohandle_data data_chip1 = {0}, data_chip3 = {0};
    int led_positions[] = {0, 1, 2, 3}; // Logical mapping of LEDs
    int current_led = 0;

    while (running) {
        // Check for button presses
        if (poll(pfds, 4, 0) > 0) {
            for (int i = 0; i < 4; i++) {
                if (pfds[i].revents & POLLIN) {
                    struct gpioevent_data event;
                    read(pfds[i].fd, &event, sizeof(event));
                    switch (i) {
                        case 0: // Button 1: Exit
                            printf("Button 1 pressed. Exiting...\n");
                            running = 0;
                            break;
                        case 1: // Button 2: Double speed
                            if (delay > MIN_DELAY) {
                                delay /= 2;
                                printf("Button 2 pressed. Speed doubled (delay: %d us).\n", delay);
                            }
                            break;
                        case 2: // Button 3: Half speed
                            if (delay < MAX_DELAY) {
                                delay *= 2;
                                printf("Button 3 pressed. Speed halved (delay: %d us).\n", delay);
                            }
                            break;
                        case 3: // Button 4: Change direction
                            direction *= -1;
                            printf("Button 4 pressed. Direction inverted.\n");
                            break;
                    }
                }
            }
        }

        if (!stop) {
            // Reset all LEDs
            data_chip1.values[0] = 0;
            ioctl(req_chip1.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data_chip1); // Ensure LED1 is off

            for (int i = 0; i < 3; i++) {
                data_chip3.values[i] = 0;
            }
            ioctl(req_chip3.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data_chip3); // Ensure LEDs on chip3 are off

            // Turn on the current LED
            if (led_positions[current_led] == 0) {
                data_chip1.values[0] = 1; // LED1
                ioctl(req_chip1.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data_chip1);
            } else {
                data_chip3.values[led_positions[current_led] - 1] = 1; // LED2, LED3, or LED4
                ioctl(req_chip3.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data_chip3);
            }

            // Wait before moving to the next LED
            usleep(delay);

            // Move to the next LED
            current_led = (current_led + direction + 4) % 4;
        } else {
            // Stop condition: Turn off all LEDs
            turn_off_leds(&req_chip1, &req_chip3);
            usleep(100000); // Prevent busy-looping
        }
    }

    // Ensure LEDs are off before exiting
    turn_off_leds(&req_chip1, &req_chip3);

    // Clean up
    close(event_button1.fd);
    close(event_button2.fd);
    close(event_button3.fd);
    close(event_button4.fd);
    close(req_chip1.fd);
    close(req_chip3.fd);
    close(chip1_fd);
    close(chip3_fd);

    return 0;
}
