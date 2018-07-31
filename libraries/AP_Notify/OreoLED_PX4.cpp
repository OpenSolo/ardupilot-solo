/*
  OreoLED PX4 driver
*/
/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <AP_HAL.h>

#if CONFIG_HAL_BOARD == HAL_BOARD_PX4
#include "OreoLED_PX4.h"
#include "AP_Notify.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <drivers/drv_oreoled.h>
#include <stdio.h>
#include <errno.h>

extern const AP_HAL::HAL& hal;

#define OREOLED_BACKLEFT                0       // back left led instance number
#define OREOLED_BACKRIGHT               1       // back right led instance number
#define OREOLED_FRONTRIGHT              2       // front right led instance number
#define OREOLED_FRONTLEFT               3       // front left led instance number

// constructor
OreoLED_PX4::OreoLED_PX4() : NotifyDevice(),
    _overall_health(false),
    _oreoled_fd(-1),
    _send_required(false),
    _state_desired_semaphore(false),
    _pattern_override(0)
{
    // initialise desired and sent state
    memset(_state_desired,0,sizeof(_state_desired));
    memset(_state_sent,0,sizeof(_state_sent));
}

// init - initialised the device
bool OreoLED_PX4::init()
{
    // open the device
    _oreoled_fd = open(OREOLED0_DEVICE_PATH, O_RDWR);
    if (_oreoled_fd == -1) {
        hal.console->printf("Unable to open " OREOLED0_DEVICE_PATH);
        _overall_health = false;
    } else {
        // set overall health
        _overall_health = true;
        // register timer
        hal.scheduler->register_io_process(AP_HAL_MEMBERPROC(&OreoLED_PX4::update_timer));
    }

    // return health
    return _overall_health;
}

// update - updates device according to timed_updated.  Should be
// called at 50Hz
void OreoLED_PX4::update()
{
    static uint8_t counter = 0;     // counter to reduce rate from 50hz to 10hz
    static uint8_t step = 0;        // step to control pattern
    static uint8_t last_stage = 0;  // unique id of the last messages sent to the LED, used to reduce resends which disrupt some patterns
    static uint8_t initialization_done = 0;   // Keep track if initialization has begun.  There is a period when the driver
                                              // is running but initialization has not yet begun -- this prevents post-initialization
                                              // LED patterns from displaying before initialization has completed.

    // return immediately if not healthy
    if (!_overall_health) {
        return;
    }

    // handle firmware update event
    if (AP_Notify::flags.firmware_update) {
        // Force a syncronisation before setting the free-running colour cycle macro
        send_sync();
        set_macro(OREOLED_INSTANCE_ALL, OREOLED_PARAM_MACRO_COLOUR_CYCLE);
        return;
    }

    // return immediately if custom pattern has been sent
    if (OreoLED_PX4::_pattern_override != 0) {
        // reset stage so patterns will be resent once override clears
        last_stage = 0;
        return;
    }

    // slow rate from 50Hz to 10hz
    counter++;
    if (counter < 5) {
        return;
    }
    counter = 0;

    // move forward one step
    step++;
    if (step >= 10) {
        step = 0;
    }

    // Pre-initialization pattern is all solid green
    if (!initialization_done) {
        set_rgb(OREOLED_ALL_INSTANCES, 0, OREOLED_BRIGHT, 0);
    }

    // initialising pattern
    if (AP_Notify::flags.initialising) {
        initialization_done = 1;  // Record initialization has begun
        last_stage = 1;   // record stage

        // exit so no other status modify this pattern
        return;
    }

    // save trim and esc calibration pattern
    if (AP_Notify::flags.save_trim || AP_Notify::flags.esc_calibration) {
        switch(step) {
            case 0:
            case 3:
            case 6:
                // red
                set_rgb(OREOLED_INSTANCE_ALL, OREOLED_BRIGHT, 0, 0);
                break;

            case 1:
            case 4:
            case 7:
                // blue
                set_rgb(OREOLED_INSTANCE_ALL, 0, 0, OREOLED_BRIGHT);
                break;

            case 2:
            case 5:
            case 8:
                // green on
                set_rgb(OREOLED_INSTANCE_ALL, 0, OREOLED_BRIGHT, 0);
                break;

            case 9:
                // all off
                set_rgb(OREOLED_INSTANCE_ALL, 0, 0, 0);
                break;
        }
        // record stage
        last_stage = 2;
        // exit so no other status modify this pattern
        return;
    }

    // radio failsafe pattern: Alternate between front red/rear black and front black/rear red
    if (AP_Notify::flags.failsafe_radio) {
        switch(step) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                // Front red/rear black
                set_rgb(OREOLED_FRONTLEFT, OREOLED_BRIGHT, 0, 0);
                set_rgb(OREOLED_FRONTRIGHT, OREOLED_BRIGHT, 0, 0);
                set_rgb(OREOLED_BACKLEFT, 0, 0, 0);
                set_rgb(OREOLED_BACKRIGHT, 0, 0, 0);
                break;
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
                // Front black/rear red
                set_rgb(OREOLED_FRONTLEFT, 0, 0, 0);
                set_rgb(OREOLED_FRONTRIGHT, 0, 0, 0);
                set_rgb(OREOLED_BACKLEFT, OREOLED_BRIGHT, 0, 0);
                set_rgb(OREOLED_BACKRIGHT, OREOLED_BRIGHT, 0, 0);
                break;
        }
        // record stage
        last_stage = 3;
        // exit so no other status modify this pattern
        return;
    }

    // send colours (later we will set macro if required)
    if (last_stage < 10 && initialization_done) {
        set_macro(OREOLED_INSTANCE_ALL, OREOLED_PARAM_MACRO_AUTOMOBILE);
        last_stage = 10;
    } else if (last_stage >= 10) {
        static uint8_t previous_autopilot_mode = -1;
        if (previous_autopilot_mode != AP_Notify::flags.autopilot_mode) {

            if (AP_Notify::flags.autopilot_mode) {
                // autopilot flight modes start breathing macro
                set_macro(OREOLED_INSTANCE_ALL, OREOLED_PARAM_MACRO_AUTOMOBILE);
                set_macro(OREOLED_INSTANCE_ALL, OREOLED_PARAM_MACRO_BREATH);
            } else {
                // manual flight modes stop breathing -- solid color
                set_macro(OREOLED_INSTANCE_ALL, OREOLED_PARAM_MACRO_AUTOMOBILE);
            }

            // record we have processed this change
            previous_autopilot_mode = AP_Notify::flags.autopilot_mode;
        }
        last_stage = 11;
    }
}

// set_rgb - set color as a combination of red, green and blue values for one or all LEDs, pattern defaults to solid color
void OreoLED_PX4::set_rgb(uint8_t instance, uint8_t red, uint8_t green, uint8_t blue)
{
	set_rgb(instance, OREOLED_PATTERN_SOLID, red, green, blue);
}

// set_rgb - set color as a combination of red, green and blue values for one or all LEDs, using the specified pattern
void OreoLED_PX4::set_rgb(uint8_t instance, oreoled_pattern pattern, uint8_t red, uint8_t green, uint8_t blue)
{
    // return immediately if no healty leds
    if (!_overall_health) {
        return;
    }

    // get semaphore
    _state_desired_semaphore = true;

    // check for all instances
    if (instance == OREOLED_INSTANCE_ALL) {
        // store desired rgb for all LEDs
        for (uint8_t i=0; i<OREOLED_NUM_LEDS; i++) {
            _state_desired[i].set_rgb(pattern, red, green, blue);
            if (!(_state_desired[i] == _state_sent[i])) {
                _send_required = true;
            }
        }
    } else if (instance < OREOLED_NUM_LEDS) {
        // store desired rgb for one LED
        _state_desired[instance].set_rgb(pattern, red, green, blue);
        if (!(_state_desired[instance] == _state_sent[instance])) {
            _send_required = true;
        }
    }

    // release semaphore
    _state_desired_semaphore = false;
}

// set_rgb - set color as a combination of red, green and blue values for one or all LEDs, using the specified pattern
void OreoLED_PX4::set_rgb(uint8_t instance, oreoled_pattern pattern, uint8_t red, uint8_t green, uint8_t blue,
        uint8_t amplitude_red, uint8_t amplitude_green, uint8_t amplitude_blue,
        uint16_t period, uint16_t phase_offset)
{
    // return immediately if no healty leds
    if (!_overall_health) {
        return;
    }

    // get semaphore
    _state_desired_semaphore = true;

    // check for all instances
    if (instance == OREOLED_INSTANCE_ALL) {
        // store desired rgb for all LEDs
        for (uint8_t i=0; i<OREOLED_NUM_LEDS; i++) {
            _state_desired[i].set_rgb(pattern, red, green, blue, amplitude_red, amplitude_green, amplitude_blue, period, phase_offset);
            if (!(_state_desired[i] == _state_sent[i])) {
                _send_required = true;
            }
        }
    } else if (instance < OREOLED_NUM_LEDS) {
        // store desired rgb for one LED
        _state_desired[instance].set_rgb(pattern, red, green, blue, amplitude_red, amplitude_green, amplitude_blue, period, phase_offset);
        if (!(_state_desired[instance] == _state_sent[instance])) {
            _send_required = true;
        }
    }

    // release semaphore
    _state_desired_semaphore = false;
}

// set_macro - set macro for one or all LEDs
void OreoLED_PX4::set_macro(uint8_t instance, oreoled_macro macro)
{
    // return immediately if no healthy leds
    if (!_overall_health) {
        return;
    }

    // set semaphore
    _state_desired_semaphore = true;

    // check for all instances
    if (instance == OREOLED_INSTANCE_ALL) {
        // store desired macro for all LEDs
        for (uint8_t i=0; i<OREOLED_NUM_LEDS; i++) {
            _state_desired[i].set_macro(macro);
            if (!(_state_desired[i] == _state_sent[i])) {
                _send_required = true;
            }
        }
    } else if (instance < OREOLED_NUM_LEDS) {
        // store desired macro for one LED
        _state_desired[instance].set_macro(macro);
        if (!(_state_desired[instance] == _state_sent[instance])) {
            _send_required = true;
        }
    }

    // release semaphore
    _state_desired_semaphore = false;
}

// send_sync - force a syncronisation of the all LED's
void OreoLED_PX4::send_sync()
{
    // return immediately if no healthy leds
    if (!_overall_health) {
        return;
    }

    // set semaphore
    _state_desired_semaphore = true;


    // store desired macro for all LEDs
    for (uint8_t i=0; i<OREOLED_NUM_LEDS; i++) {
        _state_desired[i].send_sync();
        if (!(_state_desired[i] == _state_sent[i])) {
            _send_required = true;
        }
    }


    // release semaphore
    _state_desired_semaphore = false;
}

// Clear the desired state
void OreoLED_PX4::clear_state(void)
{
    // set semaphore
     _state_desired_semaphore = true;

    for (uint8_t i=0; i<OREOLED_NUM_LEDS; i++) {
        _state_desired[i].clear_state();
    }

    _send_required = false;

    // release semaphore
    _state_desired_semaphore = false;
}

// update_timer - called by scheduler and updates PX4 driver with commands
void OreoLED_PX4::update_timer(void)
{
    // exit immediately if unhealthy
    if (!_overall_health) {
        return;
    }

    // exit immediately if send not required, or state is being updated
    if (!_send_required || _state_desired_semaphore) {
        return;
    }

    // for each LED
    for (uint8_t i=0; i<OREOLED_NUM_LEDS; i++) {

        // check for state change
        if (!(_state_desired[i] == _state_sent[i])) {
            switch (_state_desired[i].mode) {
                case OREOLED_MODE_MACRO:
                    {
                    oreoled_macrorun_t macro_run = {i, _state_desired[i].macro};
                    ioctl(_oreoled_fd, OREOLED_RUN_MACRO, (unsigned long)&macro_run);
                    }
                    break;
                case OREOLED_MODE_RGB:
                    {
                    oreoled_rgbset_t rgb_set = {i, _state_desired[i].pattern, _state_desired[i].red, _state_desired[i].green, _state_desired[i].blue};
                    ioctl(_oreoled_fd, OREOLED_SET_RGB, (unsigned long)&rgb_set);
                    }
                    break;
                case OREOLED_MODE_RGB_EXTENDED:
                    {
                    oreoled_cmd_t cmd;
                    memset(&cmd, 0, sizeof(oreoled_cmd_t));
                    cmd.led_num = i;
                    cmd.buff[0] = _state_desired[i].pattern;
                    cmd.buff[1] = OREOLED_PARAM_BIAS_RED;
                    cmd.buff[2] = _state_desired[i].red;
                    cmd.buff[3] = OREOLED_PARAM_BIAS_GREEN;
                    cmd.buff[4] = _state_desired[i].green;
                    cmd.buff[5] = OREOLED_PARAM_BIAS_BLUE;
                    cmd.buff[6] = _state_desired[i].blue;
                    cmd.buff[7] = OREOLED_PARAM_AMPLITUDE_RED;
                    cmd.buff[8] = _state_desired[i].amplitude_red;
                    cmd.buff[9] = OREOLED_PARAM_AMPLITUDE_GREEN;
                    cmd.buff[10] = _state_desired[i].amplitude_green;
                    cmd.buff[11] = OREOLED_PARAM_AMPLITUDE_BLUE;
                    cmd.buff[12] = _state_desired[i].amplitude_blue;
                    // Note: The Oreo LED controller expects to receive uint16 values
                    // in little endian order
                    cmd.buff[13] = OREOLED_PARAM_PERIOD;
                    cmd.buff[14] = (_state_desired[i].period & 0xFF00) >> 8;
                    cmd.buff[15] = (_state_desired[i].period & 0x00FF);
                    cmd.buff[16] = OREOLED_PARAM_PHASEOFFSET;
                    cmd.buff[17] = (_state_desired[i].phase_offset & 0xFF00) >> 8;
                    cmd.buff[18] = (_state_desired[i].phase_offset & 0x00FF);
                    cmd.num_bytes = 19;
                    ioctl(_oreoled_fd, OREOLED_SEND_BYTES, (unsigned long)&cmd);
                    }
                    break;
                case OREOLED_MODE_SYNC:
                    {
                    ioctl(_oreoled_fd, OREOLED_FORCE_SYNC, 0);
                    }
                    break;
                default:
                    break;
            };
            // save state change
            _state_sent[i] = _state_desired[i];
        }
    }

    // flag updates sent
    _send_required = false;
}

// handle a LED_CONTROL message
void OreoLED_PX4::handle_led_control(mavlink_message_t *msg)
{
    // exit immediately if unhealthy
    if (!_overall_health) {
        return;
    }

    // decode mavlink message
    mavlink_led_control_t packet;
    mavlink_msg_led_control_decode(msg, &packet);

    // exit immediately if instance is invalid
    if (packet.instance >= OREOLED_NUM_LEDS && packet.instance != OREOLED_INSTANCE_ALL) {
        return;
    }

    // if pattern is OFF, we clear pattern override so normal lighting should resume
    if (packet.pattern == LED_CONTROL_PATTERN_OFF) {
        _pattern_override = 0;
        clear_state();
        return;
    }

    if (packet.pattern == LED_CONTROL_PATTERN_CUSTOM) {
        // Here we handle two different "sub commands",
        // depending on the bytes in the first CUSTOM_HEADER_LENGTH
        // of the custom pattern byte buffer

        // Return if we don't have at least CUSTOM_HEADER_LENGTH bytes
        if (packet.custom_len < CUSTOM_HEADER_LENGTH) {
            return;
        }

        // check for the RGB0 sub-command
        if (memcmp(packet.custom_bytes, "RGB0", CUSTOM_HEADER_LENGTH) == 0) {
            // check to make sure the total length matches the length of the RGB0 command + data values
            if (packet.custom_len != CUSTOM_HEADER_LENGTH + 4) {
                return;
            }

            // check for valid pattern id
            if (packet.custom_bytes[CUSTOM_HEADER_LENGTH] >= OREOLED_PATTERN_ENUM_COUNT) {
            return;
            }

            // convert the first byte after the command to a oreoled_pattern
            oreoled_pattern pattern = (oreoled_pattern)packet.custom_bytes[CUSTOM_HEADER_LENGTH];

            // call the set_rgb function, using the rest of the bytes as the RGB values
            set_rgb(packet.instance, pattern, packet.custom_bytes[CUSTOM_HEADER_LENGTH + 1], packet.custom_bytes[CUSTOM_HEADER_LENGTH + 2], packet.custom_bytes[CUSTOM_HEADER_LENGTH + 3]);

        } else if (memcmp(packet.custom_bytes, "RGB1", CUSTOM_HEADER_LENGTH) == 0) { // check for the RGB1 sub-command

            // check to make sure the total length matches the length of the RGB1 command + data values
            if (packet.custom_len != CUSTOM_HEADER_LENGTH + 11) {
                return;
            }

            // check for valid pattern id
            if (packet.custom_bytes[CUSTOM_HEADER_LENGTH] >= OREOLED_PATTERN_ENUM_COUNT) {
                return;
            }

            // convert the first byte after the command to a oreoled_pattern
            oreoled_pattern pattern = (oreoled_pattern)packet.custom_bytes[CUSTOM_HEADER_LENGTH];

            // uint16_t values are stored in custom_bytes in little endian order
            // assume the flight controller is little endian when decoding values
            uint16_t period =
                    ((0x00FF & (uint16_t)packet.custom_bytes[CUSTOM_HEADER_LENGTH + 7]) << 8) |
                    (0x00FF & (uint16_t)packet.custom_bytes[CUSTOM_HEADER_LENGTH + 8]);
            uint16_t phase_offset =
                    ((0x00FF & (uint16_t)packet.custom_bytes[CUSTOM_HEADER_LENGTH + 9]) << 8) |
                    (0x00FF & (uint16_t)packet.custom_bytes[CUSTOM_HEADER_LENGTH + 10]);

            // call the set_rgb function, using the rest of the bytes as the RGB values
            set_rgb(packet.instance, pattern, packet.custom_bytes[CUSTOM_HEADER_LENGTH + 1], packet.custom_bytes[CUSTOM_HEADER_LENGTH + 2],
                    packet.custom_bytes[CUSTOM_HEADER_LENGTH + 3], packet.custom_bytes[CUSTOM_HEADER_LENGTH + 4], packet.custom_bytes[CUSTOM_HEADER_LENGTH + 5],
                    packet.custom_bytes[CUSTOM_HEADER_LENGTH + 6], period, phase_offset);
        } else if (memcmp(packet.custom_bytes, "SYNC", CUSTOM_HEADER_LENGTH) == 0) { // check for the SYNC sub-command
            // check to make sure the total length matches the length of the SYN0 command + data values
            if (packet.custom_len != CUSTOM_HEADER_LENGTH + 0) {
                return;
            }
            send_sync();
        } else { // unrecognized command
            return;
        }
    } else {
    	// other patterns sent as macro
    	set_macro(packet.instance, (oreoled_macro)packet.pattern);
    }
    _pattern_override = packet.pattern;
}

OreoLED_PX4::oreo_state::oreo_state() {
    clear_state();
}

void OreoLED_PX4::oreo_state::clear_state() {
    mode = OREOLED_MODE_NONE;
    pattern = OREOLED_PATTERN_OFF;
    macro = OREOLED_PARAM_MACRO_RESET;
    red = 0;
    green = 0;
    blue = 0;
    amplitude_red = 0;
    amplitude_green = 0;
    amplitude_blue = 0;
    period = 0;
    repeat = 0;
    phase_offset = 0;
}

void OreoLED_PX4::oreo_state::send_sync() {
    clear_state();
    mode = OREOLED_MODE_SYNC;
}

void OreoLED_PX4::oreo_state::set_macro(oreoled_macro new_macro) {
    clear_state();
    mode = OREOLED_MODE_MACRO;
    macro = new_macro;
}

void OreoLED_PX4::oreo_state::set_rgb(enum oreoled_pattern new_pattern, uint8_t new_red, uint8_t new_green, uint8_t new_blue) {
    clear_state();
    mode = OREOLED_MODE_RGB;
    pattern = new_pattern;
    red = new_red;
    green = new_green;
    blue = new_blue;
}

void OreoLED_PX4::oreo_state::set_rgb(enum oreoled_pattern new_pattern, uint8_t new_red, uint8_t new_green,
        uint8_t new_blue, uint8_t new_amplitude_red, uint8_t new_amplitude_green, uint8_t new_amplitude_blue,
        uint16_t new_period, uint16_t new_phase_offset) {
    clear_state();
    mode = OREOLED_MODE_RGB_EXTENDED;
    pattern = new_pattern;
    red = new_red;
    green = new_green;
    blue = new_blue;
    amplitude_red = new_amplitude_red;
    amplitude_green = new_amplitude_green;
    amplitude_blue = new_amplitude_blue;
    period = new_period;
    phase_offset = new_phase_offset;
}

// operator==
bool OreoLED_PX4::oreo_state::operator==(const OreoLED_PX4::oreo_state &os) {
   return ((os.mode==mode) && (os.pattern==pattern) && (os.macro==macro) && (os.red==red) && (os.green==green) && (os.blue==blue)
           && (os.amplitude_red==amplitude_red) && (os.amplitude_green==amplitude_green) && (os.amplitude_blue==amplitude_blue)
           && (os.period==period) && (os.repeat==repeat) && (os.phase_offset==phase_offset));
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_PX4
