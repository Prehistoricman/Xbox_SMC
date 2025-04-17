//Pico SDK docs: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html

#include <stdint.h>
#include "printf.h"
#include "pico/platform.h"

#define PIN_ICSP_DATA D1 //RB7
#define PIN_ICSP_CLK D2 //RB6
#define PIN_PWR_VDD D16 //Can connect directly to VDD
#define PIN_PWR_MCLR D20 //When high, enables the programming voltage VPP on the MCLR pin

static const uint16_t PIC_data_to_program[] = {
    //your PIC program data here
    0x0806, 0x3FFF, 0x0806, 0x3FFF, 0x0806, 0x3FFF //example data
};

static const uint32_t ICSP_CMD_LOAD_CONFIG = 0;
static const uint32_t ICSP_CMD_LOAD_DATA = 2;
static const uint32_t ICSP_CMD_READ_DATA = 4;
static const uint32_t ICSP_CMD_INC_ADDR = 6;
static const uint32_t ICSP_CMD_BEGIN_PROGRAM = 8;
static const uint32_t ICSP_CMD_END_PROGRAM = 14;

static char inputBuffer[1024];
static volatile bool dontEnableInterrupts = false;

void setup() {
    printf_init();
    Serial.begin(115200);
    
    gpio_set_dir(PIN_ICSP_DATA, GPIO_IN);
    gpio_set_slew_rate(PIN_ICSP_DATA, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_ICSP_DATA, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_function(PIN_ICSP_DATA, GPIO_FUNC_SIO);
    
    gpio_set_dir(PIN_ICSP_CLK, GPIO_OUT);
    gpio_set_slew_rate(PIN_ICSP_CLK, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_ICSP_CLK, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_function(PIN_ICSP_CLK, GPIO_FUNC_SIO);
    
    gpio_set_dir(PIN_PWR_VDD, GPIO_OUT);
    gpio_set_slew_rate(PIN_PWR_VDD, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_PWR_VDD, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_function(PIN_PWR_VDD, GPIO_FUNC_SIO);
    
    gpio_set_dir(PIN_PWR_MCLR, GPIO_OUT);
    gpio_set_slew_rate(PIN_PWR_MCLR, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_PWR_MCLR, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_function(PIN_PWR_MCLR, GPIO_FUNC_SIO);
    
    gpio_pull_up(PIN_ICSP_DATA);
    gpio_put(PIN_PWR_MCLR, 0);
    gpio_put(PIN_PWR_VDD, 1);
    delay(10);
    gpio_put(PIN_PWR_MCLR, 1);
}

static void __not_in_flash_func(busywait)(uint32_t cycles) { //13 cycles takes approx 1us
    for (volatile uint32_t x = 0; x < cycles; x++) {
        
    }
}

static void __not_in_flash_func(send_command)(uint32_t cmd) {
    //Send 6 bits of command, LSB first
    noInterrupts();
    gpio_set_dir(PIN_ICSP_DATA, GPIO_OUT);
    uint32_t cmd_send = cmd;
    for (uint32_t i = 0; i < 6; i++) {
        busywait(7);
        gpio_put(PIN_ICSP_CLK, 1);
        gpio_put(PIN_ICSP_DATA, cmd_send & 1);
        cmd_send >>= 1;
        busywait(7);
        gpio_put(PIN_ICSP_CLK, 0);
    }
    gpio_set_dir(PIN_ICSP_DATA, GPIO_IN);
    if (!dontEnableInterrupts) {
        interrupts();
    }
}

static void __not_in_flash_func(send_data)(uint32_t data) {
    //Send 0 bit, 14 bits of data LSB first, 0 bit
    noInterrupts();
    gpio_set_dir(PIN_ICSP_DATA, GPIO_OUT);
    uint32_t data_send = data;
    data_send <<= 1; //Shift a 0 bit into the LSB
    for (uint32_t i = 0; i < 16; i++) {
        gpio_put(PIN_ICSP_CLK, 1);
        gpio_put(PIN_ICSP_DATA, data_send & 1);
        busywait(7);
        gpio_put(PIN_ICSP_CLK, 0);
        busywait(7);
        data_send >>= 1;
    }
    gpio_set_dir(PIN_ICSP_DATA, GPIO_IN);
    if (!dontEnableInterrupts) {
        interrupts();
    }
}

static uint32_t __not_in_flash_func(receive_data)(void) {
    //Send 0 bit, 14 bits of data LSB first, 0 bit
    noInterrupts();
    gpio_set_dir(PIN_ICSP_DATA, GPIO_IN);
    uint32_t data = 0;
    for (uint32_t i = 0; i < 16; i++) {
        gpio_put(PIN_ICSP_CLK, 1);
        busywait(7);
        gpio_put(PIN_ICSP_CLK, 0);
        busywait(7);
        data = data >> 1 | ((uint32_t)gpio_get(PIN_ICSP_DATA) << 15);
    }
    data = (data >> 1) & 0x3FFF; //Delete unused bits
    if (!dontEnableInterrupts) {
        interrupts();
    }
    return data;
}


char helpstring[] = "All arguments in decimal."
"Commands:\n"
"cmd  Purpose                          Arg 1                        Arg 2                Arg 3                  \n"
"h    This help message                                                                                         \n"
"c    Set PC to config region          Optional data                                                            \n"
"i    Increment PC and read data       Optional byte count                                                      \n"
"r    Read data                        Optional repetition count                                                \n"
"w    Write (program) data             Data                         Repetition count     Program duration in us \n"
"p    Set VDD level                    VDD on or off (1/0)                                                      \n"
"v    Set VPP level                    Optional VPP on or off (1/0)                                             \n"
"!    Program image                    Must write 1337 to confirm                                               \n"
"b    Burnout attack on single word    Data to write                Optional time to burn each word (ms)        \n"
"B    Burnout attack on config word    Data to write                                                            \n"
"n    Auto Burnout attack              Index to start from                                                      \n"
"0    Send cmd (low-level)             cmd                                                                      \n"
"1    Send data (low-level)            Data                                                                     \n"
"2    Read data (low-level)                                                                                     \n"
"x    Experimental read command                                                                                 \n"
"F    Programs incrementing numbers    Word count                                                               \n"
"+    Pull up RB7 (ICSP data) pin                                                                               \n"
"-    Pull down RB7 (ICSP data) pin                                                                             \n"
;

void __not_in_flash_func(loop)() {
    Serial.print("Enter h for commands list... ");
    while (!Serial.available());
    int length = Serial.readBytesUntil('\n', inputBuffer, sizeof(inputBuffer));
    inputBuffer[length] = '\0';
    if (length == 0) {
        Serial.println();
        return;
    }
    xprintf(">%s\n", inputBuffer);

#define MAX_ARGUMENTS 32
    char* argv[MAX_ARGUMENTS];
    uint32_t argv_int[MAX_ARGUMENTS];
    uint8_t argc = 1;
    argv[0] = inputBuffer;
    //split the command string by spaces
    for (uint32_t i = 0; i < length; i++){
        if (inputBuffer[i] == ' '){ //space separates command arguments
            inputBuffer[i] = '\0';
            argv[argc] = inputBuffer + i + 1;
            argv_int[argc] = atoi(argv[argc]);
            argc++;
            if (argc > MAX_ARGUMENTS - 1){
                xprintf("ERROR too many arguments\n");
                return;
            }
        }
    }
    
    char cmd = inputBuffer[0];
    
    switch (cmd) {
    default:
        xprintf("Invalid command\n");
        break;
    case 'h':
        Serial.println(helpstring);
        break;
        
        //Legacy commands
    case '0':
        if (argc != 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        send_command(argv_int[1]);
        xprintf("Sent cmd %d\n", argv_int[1]);
        break;
    case '1':
        if (argc != 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        send_data(argv_int[1]);
        xprintf("Sent data 0x%X\n", argv_int[1]);
        break;
    case '2':
        xprintf("RX data 0x%X\n", receive_data());
        break;
    case 'x': {
        //Pause after x bits of cmd
        //Experiment to see when data is latched during a read
        //Send 6 bits of command, LSB first
        if (argc != 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        noInterrupts();
        gpio_set_dir(PIN_ICSP_DATA, GPIO_OUT);
        uint32_t cmd_send = ICSP_CMD_READ_DATA;
        for (uint32_t i = 0; i < 6; i++) {
            busywait(7);
            gpio_put(PIN_ICSP_CLK, 1);
            gpio_put(PIN_ICSP_DATA, cmd_send & 1);
            cmd_send >>= 1;
            busywait(7);
            gpio_put(PIN_ICSP_CLK, 0);
            if (i == argv_int[1]) {
                busywait(13 * 1000 * 1000);
            }
        }
        gpio_set_dir(PIN_ICSP_DATA, GPIO_IN);
        interrupts();
        xprintf("RX data 0x%X\n", receive_data());
    } break;
        
    case 'p':
        if (argc != 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        if (argv_int[1] == 0) {
            gpio_put(PIN_PWR_VDD, 0);
        } else {
            gpio_put(PIN_PWR_VDD, 1);
        }
        break;
    case 'v':
        if (argc != 2) {
            gpio_put(PIN_PWR_MCLR, 0);
            delay(10);
            gpio_put(PIN_PWR_MCLR, 1);
            xprintf("Power cycled VPP\n");
        } else {
            if (argv_int[1] == 0) {
                gpio_put(PIN_PWR_MCLR, 0);
            } else {
                gpio_put(PIN_PWR_MCLR, 1);
            }
        }
        break;
    case '+':
        gpio_pull_up(PIN_ICSP_DATA);
        break;
    case '-':
        gpio_pull_down(PIN_ICSP_DATA);
        break;
        
        
    case 'c': {
        //Go to config area
        uint32_t data = 0;
        if (argc >= 2) {
            data = argv_int[1];
        }
        send_command(ICSP_CMD_LOAD_CONFIG);
        send_data(data);
        xprintf("In config area\n");
    } break;
    case 'w': {
        //Program (write) cycle
        if (argc < 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        uint32_t loops = 1;
        uint32_t waittime_us = 100;
        if (argc >= 3) {
            loops = argv_int[2];
        }
        if (argc >= 4) {
            waittime_us = argv_int[3];
        }
        for (uint32_t i = 0; i < loops; i++) {
            send_command(ICSP_CMD_READ_DATA);
            xprintf("Data before prog: 0x%04X", receive_data());
            send_command(ICSP_CMD_LOAD_DATA);
            send_data(argv_int[1]);
            dontEnableInterrupts = true; //Critical section
            send_command(ICSP_CMD_BEGIN_PROGRAM);
            busywait(13 * waittime_us);
            send_command(ICSP_CMD_END_PROGRAM);
            dontEnableInterrupts = false;
            interrupts();
            interrupts(); //One call for every noInterrupts call
            send_command(ICSP_CMD_READ_DATA);
            xprintf(" Data after prog: 0x%04X\n", receive_data());
            
            if ((i % 20) == 0) {
                //Check Serial to see if user wants to stop
                if (Serial.available()) {
                    Serial.read();
                    xprintf("Stopped\n");
                    break;
                }
            }
        }
    } break;
    case 'r':
        if (argc < 2) {
            //Read data
            send_command(ICSP_CMD_READ_DATA);
            xprintf("Read data 0x%04X\n", receive_data());
        } else {
            for (uint32_t addr = 0; addr < argv_int[1]; addr++) {
                send_command(ICSP_CMD_READ_DATA);
                xprintf("Read data repeat %d = 0x%04X\n", addr, receive_data());
                delay(33);
                //Check Serial to see if user wants to stop
                if (Serial.available()) {
                    Serial.read();
                    xprintf("Stopped\n");
                    break;
                }
            }
        }
        break;
    case 'i': {
        uint32_t addrmax = 1;
        if (argc >= 2) {
            addrmax = argv_int[1];
        }
        uint32_t addr;
        for (addr = 0; addr < addrmax; addr++) {
            send_command(ICSP_CMD_READ_DATA);
            xprintf("Data at 0x%X = 0x%04X\n", addr, receive_data());
            send_command(ICSP_CMD_INC_ADDR);
        }
        send_command(ICSP_CMD_READ_DATA);
        xprintf("Data at 0x%X = 0x%04X\n", addr, receive_data());
    } break;
    
    
    
    case '!': {
        //Program whole PIC image
        if (argc < 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        if (argv_int[1] != 1337) {
            xprintf("Specify the argument as 1337 to confirm you want to execute programming\n");
            break;
        }
        //Power cycle, use normal mode (DATA low)
        gpio_pull_down(PIN_ICSP_DATA);
        delay(10);
        gpio_put(PIN_PWR_MCLR, 0);
        delay(10);
        gpio_put(PIN_PWR_MCLR, 1);
        delay(10);
        for (uint32_t i = 0; i < (sizeof(PIC_data_to_program) / sizeof(PIC_data_to_program[0])); i++) {
            send_command(ICSP_CMD_READ_DATA);
            uint16_t data = receive_data();
            uint16_t progdata = PIC_data_to_program[i];
            if ((data != 0x3FFF) && (data != progdata)) {
                xprintf("Data at 0x%X bad: 0x%04X!\n", i, data);
                break;
            }
            //Confirm with user
            if (i < 3) {
                xprintf("CONFIRM if data at addr %X = %X OK? (y/n)\n", i, progdata);
                while (!Serial.available()) {}
                Serial.readBytesUntil('\n', inputBuffer, sizeof(inputBuffer));
                if (inputBuffer[0] != 'y') {
                    xprintf("Stopped\n");
                    break;
                }
            }
            for (uint32_t rep = 0; rep < 3; rep++) {
                send_command(ICSP_CMD_LOAD_DATA);
                send_data(progdata);
                dontEnableInterrupts = true; //Critical section
                send_command(ICSP_CMD_BEGIN_PROGRAM);
                busywait(13 * 100);
                send_command(ICSP_CMD_END_PROGRAM);
                dontEnableInterrupts = false;
                interrupts();
                interrupts(); //One call for every noInterrupts call
            }
            send_command(ICSP_CMD_READ_DATA);
            uint16_t data_readback = receive_data();
            xprintf("Data at 0x%X programmed: 0x%04X\n", i, data_readback);
            
            if (data_readback != progdata) {
                xprintf("Program failure. Supposed to be 0x%04X\n", progdata);
                break;
            }
            
            send_command(ICSP_CMD_INC_ADDR);
            
            //Check Serial to see if user wants to stop
            if (Serial.available()) {
                Serial.read();
                xprintf("Stopped\n");
                break;
            }
        }
    } break;
    
    case 'b': { //B to Burn! Progams data until those data bits are burnt out
        //Program until the 0s in the input word read as 1
        //Keep programming until it's consistent for 30s of burn time
        //A problem with this algorithm is that the readback isn't really accurate until a power cycle has occurred
        if (argc < 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        uint32_t burntime = 5;
        if (argc >= 3) {
            burntime = argv_int[2];
        }
        uint32_t burnword = argv_int[1];
        uint32_t burn_check_mask = ~burnword & 0x3FFF;
        uint32_t burn_done_count = 0;
        while (true) {
            send_command(ICSP_CMD_READ_DATA);
            uint32_t preprog = receive_data();
            xprintf("Data before prog: 0x%04X", preprog);
            send_command(ICSP_CMD_LOAD_DATA);
            send_data(burnword);
            send_command(ICSP_CMD_BEGIN_PROGRAM);
            delay(burntime);
            send_command(ICSP_CMD_END_PROGRAM);
            send_command(ICSP_CMD_READ_DATA);
            uint32_t postprog = receive_data();
            xprintf(" Data after prog: 0x%04X", postprog);
            //Check if bits are burnt out
            if (((preprog & burn_check_mask) == burn_check_mask) && ((postprog & burn_check_mask) == burn_check_mask)) {
                burn_done_count++;
                xprintf(" B%d", burn_done_count); //Indicate it's burned
            } else {
                burn_done_count = 0;
            }
            xprintf("\n");
            if (burn_done_count >= (30000 / burntime)) {
                xprintf("Burnt in\n");
                break;
            }
            //Check Serial to see if user wants to stop
            if (Serial.available()) {
                Serial.read();
                xprintf("Stopped\n");
                break;
            }
        }
    } break;
    
    case 'n': { //Auto burner. Progams data until those data bits are burnt out
        //Program until the 0s in the input word read as 1
        //Keep programming until it's consistent for 30s of burn time
        //A problem with this algorithm is that the readback isn't really accurate until a power cycle has occurred
        if (argc < 2) {
            xprintf("Needs 1 argument (index to start on)\n");
            break;
        }
        gpio_pull_down(PIN_ICSP_DATA);
        delay(10);
        gpio_put(PIN_PWR_MCLR, 0);
        delay(10);
        gpio_put(PIN_PWR_MCLR, 1);
        delay(10);
        send_command(ICSP_CMD_LOAD_CONFIG);
        send_data(0);
        bool seen_data = false;
        //Check for some data in the first 0x10 words
        for (uint32_t i = 0; i < 0x10; i++) {
            send_command(ICSP_CMD_READ_DATA);
            uint32_t data = receive_data();
            if ((data != 0x3FFF) && (data != 0)) {
                seen_data = true;
            }
            send_command(ICSP_CMD_INC_ADDR);
        }
        if (!seen_data) {
            xprintf("Can't read data!\n");
            break;
        }
        //Increment PC to burning location
        for (uint32_t i = 0; i < (0x90 + argv_int[1]); i++) {
            send_command(ICSP_CMD_INC_ADDR);
        }
        
        uint32_t burntime_ms = 5;
        uint32_t burnword = 0x7F;
        uint32_t burn_check_mask = ~burnword & 0x3FFF;
        uint32_t burn_done_count = 0;
        bool stop = false;
        for (uint32_t addr = 0; addr < 16; addr++) {
            //Do a read, expect 3FFF
            send_command(ICSP_CMD_READ_DATA);
            uint32_t preprog = receive_data();
            if ((preprog != 0x3FFF) && (preprog != burnword)) {
                xprintf("Memory is not erased! Data at 0x%X = 0x%X\n", addr, preprog);
                break;
            }
            //Limit the max number of program cycles
            for (uint32_t prog_count = 0; prog_count < ((3 * 60 * 1000) / burntime_ms); prog_count++) {
                send_command(ICSP_CMD_READ_DATA);
                uint32_t preprog = receive_data();
                xprintf("Data before prog: 0x%04X", preprog);
                send_command(ICSP_CMD_LOAD_DATA);
                send_data(burnword);
                send_command(ICSP_CMD_BEGIN_PROGRAM);
                delay(burntime_ms);
                send_command(ICSP_CMD_END_PROGRAM);
                send_command(ICSP_CMD_READ_DATA);
                uint32_t postprog = receive_data();
                xprintf(" Data after prog: 0x%04X", postprog);
                //Check if bits are burnt out
                if (((preprog & burn_check_mask) == burn_check_mask) && ((postprog & burn_check_mask) == burn_check_mask)) {
                    burn_done_count++;
                    xprintf(" B%d", burn_done_count); //Indicate it's burned
                } else {
                    burn_done_count = 0;
                }
                xprintf("\n");
                if (burn_done_count >= (30000 / burntime_ms)) {
                    xprintf("Burnt in\n");
                    break;
                }
                //Check Serial to see if user wants to stop
                if (Serial.available()) {
                    Serial.read();
                    xprintf("Stopped\n");
                    stop = true;
                    break;
                }
            }
            if (stop) {
                break;
            }
            send_command(ICSP_CMD_INC_ADDR);
        }
    } break;
        
    case 'B': //B to Burn! Attempts to burn out the config word
        while (true) {
            //Power cycle
            gpio_put(PIN_PWR_MCLR, 0);
            delay(10);
            gpio_put(PIN_PWR_MCLR, 1);
            delay(2);
            xprintf("Power cycled VPP\n");
            //Try reading some data to see if it's protected
            send_command(ICSP_CMD_READ_DATA);
            uint32_t data = receive_data();
            xprintf("Data at 0 = 0x%04X\n", data);
            if (data == 0x3FFF) {
                //Correct, try next word
                send_command(ICSP_CMD_INC_ADDR);
                busywait(13);
                send_command(ICSP_CMD_READ_DATA);
                data = receive_data();
                xprintf("Data at 1 = 0x%04X\n", data);
                if (data == 0) {
                    //Correct, try next word
                    send_command(ICSP_CMD_INC_ADDR);
                    busywait(13);
                    send_command(ICSP_CMD_READ_DATA);
                    data = receive_data();
                    xprintf("Data at 2 = 0x%04X\n", data);
                    if (data == 1) {
                        xprintf("Unlocked!!!\n");
                        break;
                    }
                }
            }
            //Go to config word
            send_command(ICSP_CMD_LOAD_CONFIG);
            send_data(0);
            busywait(13);
            for (uint32_t addr = 0; addr < 7; addr++) {
                send_command(ICSP_CMD_INC_ADDR);
                busywait(13);
            }
            //Burn it until something goes wrong
            send_command(ICSP_CMD_READ_DATA);
            xprintf("Data before prog: 0x%04X", receive_data());
            while (true) {
                send_command(ICSP_CMD_LOAD_DATA);
                send_data(argv_int[1]);
                dontEnableInterrupts = true; //Critical section
                send_command(ICSP_CMD_BEGIN_PROGRAM);
                busywait(13 * 5000);
                send_command(ICSP_CMD_END_PROGRAM);
                dontEnableInterrupts = false;
                interrupts();
                interrupts(); //One call for every noInterrupts call
                send_command(ICSP_CMD_READ_DATA);
                uint32_t data = receive_data();
                xprintf(" Data after prog: 0x%04X\n", data);
                if ((data == 0) || (data == 0x3FFF)) {
                    //Stopped working. Reset and try again
                    break;
                }
            }
            //Check Serial to see if user wants to stop
            if (Serial.available()) {
                Serial.read();
                xprintf("Stopped\n");
                break;
            }
        }
        break;
        
    case 'F':
        //Write x number of ascending numbers
        if (argc < 2) {
            xprintf("Needs 1 argument\n");
            break;
        }
        for (uint32_t val = 0; val < argv_int[1]; val++) {
            send_command(ICSP_CMD_LOAD_DATA);
            send_data(val);
            dontEnableInterrupts = true; //Critical section
            send_command(ICSP_CMD_BEGIN_PROGRAM);
            busywait(13 * 200);
            send_command(ICSP_CMD_END_PROGRAM);
            dontEnableInterrupts = false;
            interrupts();
            interrupts(); //One call for every noInterrupts call
            send_command(ICSP_CMD_INC_ADDR);
        }
        break;

    }
}
