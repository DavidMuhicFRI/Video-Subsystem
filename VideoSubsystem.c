#include <stdint.h>
#include "sleep.h"

// Hardware Base Addresses
#define ADDR_SW      0xC0000000
#define ADDR_BTN     0xC0000008
#define ADDR_TIMER   0xC0000080 // Address for the APB Timer module
#define ADDR_VIDEO   0xC0000180 // Address for the APB Video Subsystem module

// Timer constants
#define TIMER_CONF   0x00
#define TIMER_LOW    0x04  // This matches COUNT_LOW

// Mapping to Verilog `defines
#define VIDEO_CONFIG        0x00
#define VIDEO_PATTERN       0x04
#define SPRITE_X            0x08
#define SPRITE_Y            0x0C
#define SNAKE_COLOUR        0x10
#define GRID_ADDR           0x14
#define GRID_DATA           0x18
#define HEAD_DIRECTION_ADDR 0x1C

// Buttons
#define BTN_CENTER 0x01
#define BTN_UP     0x02
#define BTN_LEFT   0x04
#define BTN_RIGHT  0x08
#define BTN_DOWN   0x10

// Switches
#define SWITCH_PATTERN 0x01 // 1st switch
#define SWITCH_COLOUR  0x02 // 2nd
#define SWITCH_SPEED   0x04 // 3rd

// Configs
#define CONFIG_GRAYSCALE 0x03 //we use 0x01 and 0x03 because video_enable signal is the first bit and if we had 0 and 1 we would turn off video
#define CONFIG_COLOUR    0x01

#define CONFIG_CHECKERBOARD 0x00
#define CONFIG_BARS         0x01

// Grid Size, make sure to match this to the VideoSubsystems GRID
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480
// current grid is 640x480 pixels

#define GRID_ELEMENT_SIZE 16 //how many pixels wide and high is each grid element
#define GRID_WIDTH (VIDEO_WIDTH / GRID_ELEMENT_SIZE)
#define GRID_HEIGHT (VIDEO_HEIGHT / GRID_ELEMENT_SIZE)

// Access Macros
#define IO_WRITE(base, offset) (*(volatile uint32_t *)((base) + (offset)))
#define IO_READ(addr) (*(volatile uint32_t *)(addr))

// Starting coordinates
#define HEAD_START_X (GRID_WIDTH / 2)
#define HEAD_START_Y (GRID_HEIGHT / 2)

// Snake length
#define SNAKE_START_LENGTH 4
#define SNAKE_MAX_LENGHT 200 //dont set this too high, we dont wanna break everything, its more for the demo

// Movement 
#define UP 0
#define DOWN 1
#define LEFT 2
#define RIGHT 3

// Colours
#define COLOUR_GREEN 0x0F0
#define COLOUR_WHITE 0xFFF
#define COLOUR_RED 0xF00

// Types in GRID
#define TYPE_EMPTY 0
#define TYPE_SNAKE 1
#define TYPE_APPLE 2
#define TYPE_TRANSPARENT 3

// Time helpers 
// Clock is 100MHz (100,000,000 ticks per second)
#define CLK_FREQ 100000000 
#define SPEED_NORMAL_TICKS (0.12 * CLK_FREQ) // 120 ms in clock ticks
#define SPEED_FAST_TICKS   (0.06 * CLK_FREQ) // 60 ms in clock ticks
#define LOOP_SLEEP_TIME 1000 // (1ms) for button debouncing/polling
#define DEATH_PAUSE_TIME 1000000 // 1s

// Screen saver
#define MAX_TIMES_WITHOUT_SNAKE_COMMAND (GRID_WIDTH)  // after this many snake game cycles without button presses, pause game

// Seed for random
uint32_t seed = 0;

uint32_t last_move_ticks;

// Game State
int in_ssaver_mode = 0; // Tracks if we are in screensaver state
int body_x[SNAKE_MAX_LENGHT];
int body_y[SNAKE_MAX_LENGHT];
int head_x, head_y;
int apple_x, apple_y;
int snake_length;
int no_input_cycles;
int current_direction, next_direction;
int ssaver_x, ssaver_y, ssaver_dir_x, ssaver_dir_y;

// We write to a GRID whats on each location-> 0=empty, 1=snake body, 2=apple, 3=transparent(for sprite shapes different than square)
void write_tile(int grid_x, int grid_y, int type) {
    // Safety check for grid boundaries
    if (grid_x < 0 || grid_x >= GRID_WIDTH || grid_y < 0 || grid_y >= GRID_HEIGHT) {
        return;
    }

    // Calculate the starting pixel coordinates
    int pixel_x_start = grid_x * GRID_ELEMENT_SIZE;
    int pixel_y_start = grid_y * GRID_ELEMENT_SIZE;

    // "Paint" a 16x16 square of pixels in the framebuffer
    for (int y = 0; y < GRID_ELEMENT_SIZE; y++) {
        for (int x = 0; x < GRID_ELEMENT_SIZE; x++) {
            // Calculate actual hardware address: (y_pixel * 640) + x_pixel
            uint32_t addr = ((pixel_y_start + y) * VIDEO_WIDTH) + (pixel_x_start + x);
            
            IO_WRITE(ADDR_VIDEO, GRID_ADDR) = addr;
            IO_WRITE(ADDR_VIDEO, GRID_DATA) = (uint32_t)type;
        }
    }
}

// For drawing tiles (16x16), not pixels
void clear_screen() {
    for(int y = 0; y < GRID_HEIGHT; y++) {
        for(int x = 0; x < GRID_WIDTH; x++) {
            write_tile(x, y, TYPE_EMPTY);
        }
    }
}

// Helper function for drawing the snake on the screen on reset
void redraw_snake() {
    IO_WRITE(ADDR_VIDEO, SNAKE_COLOUR) = COLOUR_GREEN;
    for(int i = 0; i < snake_length; i++) {
        body_x[i] = head_x - i; // Places tail to the left of the head
        body_y[i] = head_y;
        if(i == 0) {
            write_tile(body_x[i], body_y[i], TYPE_TRANSPARENT);
        } else {
            write_tile(body_x[i], body_y[i], TYPE_SNAKE);
        }
    }
    IO_WRITE(ADDR_VIDEO, SPRITE_X) = head_x * GRID_ELEMENT_SIZE;
    IO_WRITE(ADDR_VIDEO, SPRITE_Y) = head_y * GRID_ELEMENT_SIZE;
    IO_WRITE(ADDR_VIDEO, HEAD_DIRECTION_ADDR) = current_direction;
}

// Check if the newly generated apple is in the snake body
int is_apple_in_snake(int x, int y) {
    // 1. Check if apple spawns on the Head (Critical Fix)
    if (x == head_x && y == head_y) return 1;
    // 2. Check if apple spawns on the Body
    for(int i = 0; i < snake_length; i++) {
        if(body_x[i] == x && body_y[i] == y) {
            return 1;
        }
    }
    return 0;
}

// Script that generates the new apple location based on the seed wit h security check
void generate_apple() {
    int new_x, new_y;
    do {
        seed += 7; 
        new_x = (seed % (GRID_WIDTH - 2)) + 1;
        new_y = (seed / 40 % (GRID_HEIGHT - 2)) + 1;
    } while(is_apple_in_snake(new_x, new_y));
    apple_x = new_x;
    apple_y = new_y;
    write_tile(apple_x, apple_y, TYPE_APPLE);
}

void display_death(){
    IO_WRITE(ADDR_VIDEO, SNAKE_COLOUR) = COLOUR_RED;
    usleep(DEATH_PAUSE_TIME);
}

void move_snake_body(){
    write_tile(body_x[0], body_y[0], TYPE_SNAKE);
}

void move_snake(){ 
    move_snake_body();
    body_x[0] = head_x; 
    body_y[0] = head_y;
    write_tile(head_x, head_y, TYPE_TRANSPARENT);
}

// Function to draw the "PAUSE" text. Type can be TYPE_SNAKE (to draw) or TYPE_EMPTY (to erase) - Used for differential drawing
void draw_pause_text(int col, int line, int type) {

    // Letter 'P'
    write_tile(col, line+0, type); write_tile(col, line+1, type); write_tile(col, line+2, type); write_tile(col, line+3, type); write_tile(col, line+4, type);
    write_tile(col+1, line+0, type); write_tile(col+1, line+2, type);
    write_tile(col+2, line+0, type); write_tile(col+2, line+1, type); write_tile(col+2, line+2, type);

    // Letter 'A'
    col += 4;
    write_tile(col, line+1, type); write_tile(col, line+2, type); write_tile(col, line+3, type); write_tile(col, line+4, type);
    write_tile(col+1, line+0, type); write_tile(col+1, line+3, type);
    write_tile(col+2, line+1, type); write_tile(col+2, line+2, type); write_tile(col+2, line+3, type); write_tile(col+2, line+4, type);

    // Letter 'U'
    col += 4;
    write_tile(col, line+0, type); write_tile(col, line+1, type); write_tile(col, line+2, type); write_tile(col, line+3, type); write_tile(col, line+4, type);
    write_tile(col+1, line+4, type);
    write_tile(col+2, line+0, type); write_tile(col+2, line+1, type); write_tile(col+2, line+2, type); write_tile(col+2, line+3, type); write_tile(col+2, line+4, type);

    // Letter 'S'
    col += 4;
    write_tile(col, line+0, type); write_tile(col, line+1, type); write_tile(col, line+2, type); write_tile(col, line+4, type);
    write_tile(col+1, line+0, type); write_tile(col+1, line+2, type); write_tile(col+1, line+4, type);
    write_tile(col+2, line+0, type); write_tile(col+2, line+2, type); write_tile(col+2, line+3, type); write_tile(col+2, line+4, type);

    // Letter 'E'
    col += 4;
    write_tile(col, line+0, type); write_tile(col, line+1, type); write_tile(col, line+2, type); write_tile(col, line+3, type); write_tile(col, line+4, type);
    write_tile(col+1, line+0, type); write_tile(col+1, line+2, type); write_tile(col+1, line+4, type);
    write_tile(col+2, line+0, type); write_tile(col+2, line+2, type); write_tile(col+2, line+4, type);
}

void reset_game() {
    IO_WRITE(ADDR_TIMER, TIMER_CONF) = 0x01; // Start the timer
    
    seed ^= IO_READ(ADDR_TIMER + TIMER_LOW);
    
    // Clear the whole screen pixel-by-pixel (640x480)
    for(int i = 0; i < (VIDEO_WIDTH * VIDEO_HEIGHT); i++) {
        IO_WRITE(ADDR_VIDEO, GRID_ADDR) = i;
        IO_WRITE(ADDR_VIDEO, GRID_DATA) = TYPE_EMPTY;
    }
    
    // Reset Game Logic
    head_x = HEAD_START_X;
    head_y = HEAD_START_Y;
    snake_length = SNAKE_START_LENGTH; 
    current_direction = RIGHT;
    next_direction = RIGHT;
    
    // After setting the body, redraw the snake
    redraw_snake();
    
    // Init the Apple block
    generate_apple();
    
    last_move_ticks = IO_READ(ADDR_TIMER + TIMER_LOW);
}

void reset_screensaver(){
    ssaver_x = 0;
    ssaver_y = 0;
    ssaver_dir_x = 1;
    ssaver_dir_y = 1;
    
    no_input_cycles = 0;
    in_ssaver_mode = 0;
}

void reset_everything(){
    reset_game();
    reset_screensaver();
}

int main() {
    
    reset_everything();
    
    while(1) {
        seed++;
        
        // Read Switches and Buttons
        uint32_t switches = IO_READ(ADDR_SW);
        uint32_t btns = IO_READ(ADDR_BTN);

        // Center Button: Hard Reset
        if (btns & BTN_CENTER) {
            reset_everything();
            continue;
        }
        
        // SWITCH 0: Pattern (0=Bars, 1=Checkerboard)
        IO_WRITE(ADDR_VIDEO, VIDEO_PATTERN) = (switches & SWITCH_PATTERN); 
        
        // SWITCH 1: Grayscale Toggle
        if (switches & SWITCH_COLOUR){ 
            IO_WRITE(ADDR_VIDEO, VIDEO_CONFIG) = CONFIG_GRAYSCALE;
        } else {
            IO_WRITE(ADDR_VIDEO, VIDEO_CONFIG) = CONFIG_COLOUR;   
        }

        // SWITCH 2: Speed Control
        uint32_t speed_threshold = (switches & SWITCH_SPEED) ? (SPEED_FAST_TICKS) : (SPEED_NORMAL_TICKS);

        // --- Movement Buffer ---
        if ((btns & BTN_UP) && current_direction != DOWN) next_direction = UP;
        if ((btns & BTN_DOWN) && current_direction != UP) next_direction = DOWN;
        if ((btns & BTN_LEFT) && current_direction != RIGHT) next_direction = LEFT;
        if ((btns & BTN_RIGHT) && current_direction != LEFT) next_direction = RIGHT;

        uint32_t current_ticks = IO_READ(ADDR_TIMER + TIMER_LOW);

        if (in_ssaver_mode) {
            // Exit screensaver on any directional input
            if (btns & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) {
                reset_everything();
            } else {
                if ((current_ticks - last_move_ticks) >= SPEED_NORMAL_TICKS) {
                    last_move_ticks = current_ticks;

                    draw_pause_text(ssaver_x, ssaver_y, TYPE_EMPTY);
                    
                    if (ssaver_x + ssaver_dir_x > (GRID_WIDTH - 19)) ssaver_dir_x = -1;
                    else if (ssaver_x + ssaver_dir_x < 0) ssaver_dir_x = 1;
                    if (ssaver_y + ssaver_dir_y > (GRID_HEIGHT - 5)) ssaver_dir_y = -1;
                    else if (ssaver_y + ssaver_dir_y < 0) ssaver_dir_y = 1;
                    
                    ssaver_x += ssaver_dir_x;
                    ssaver_y += ssaver_dir_y;
                    
                    IO_WRITE(ADDR_VIDEO, SNAKE_COLOUR) = COLOUR_WHITE;
                    draw_pause_text(ssaver_x, ssaver_y, TYPE_SNAKE);
                }
            }
        }else{
            if ((current_ticks - last_move_ticks) >= speed_threshold) {
                last_move_ticks = current_ticks;

                if (!(btns & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) no_input_cycles++;
                else no_input_cycles = 0;

                if (no_input_cycles >= MAX_TIMES_WITHOUT_SNAKE_COMMAND) {
                    // Go to screen saver
                    in_ssaver_mode = 1;
                    clear_screen();
                    continue;
                }

                current_direction = next_direction;

                // delete the last body part on the Grid
                write_tile(body_x[snake_length-1], body_y[snake_length-1], TYPE_EMPTY);

                // move the rest of the body
                for(int i = snake_length-1; i > 0; i--) {
                    body_x[i] = body_x[i-1];
                    body_y[i] = body_y[i-1];
                }

                switch(current_direction){
                    case UP:    head_y--; break;
                    case DOWN:  head_y++; break;
                    case LEFT:  head_x--; break;
                    case RIGHT: head_x++; break;
                }

                // Update Sprite Position
                IO_WRITE(ADDR_VIDEO, SPRITE_X) = head_x * GRID_ELEMENT_SIZE;
                IO_WRITE(ADDR_VIDEO, SPRITE_Y) = head_y * GRID_ELEMENT_SIZE;
                IO_WRITE(ADDR_VIDEO, HEAD_DIRECTION_ADDR) = current_direction;

                // out of bounds check
                if(head_x < 0 || head_x >= GRID_WIDTH || head_y < 0 || head_y >= GRID_HEIGHT) {
                    //push the head back into bounds
                    if (head_x < 0) head_x = 0;
                    if (head_x >= GRID_WIDTH) head_x = GRID_WIDTH - 1;
                    if (head_y < 0) head_y = 0;
                    if (head_y >= GRID_HEIGHT) head_y = GRID_HEIGHT - 1;
                    
                    IO_WRITE(ADDR_VIDEO, SPRITE_X) = head_x * GRID_ELEMENT_SIZE;
                    IO_WRITE(ADDR_VIDEO, SPRITE_Y) = head_y * GRID_ELEMENT_SIZE;

                    write_tile(body_x[1], body_y[1], TYPE_TRANSPARENT);
                    
                    display_death();
                    reset_game();
                    continue;
                } 
            
                // check if we hit ourselves
                for(int i=2; i<snake_length; i++) {
                    if(head_x == body_x[i] && head_y == body_y[i]) {
                        move_snake_body();
                        display_death();
                        reset_game();
                        goto loop_end;
                    }
                }

                // check if we collected an apple
                if(head_x == apple_x && head_y == apple_y) {
                    if(snake_length < SNAKE_MAX_LENGHT - 1) snake_length++;
                    generate_apple();
                    IO_WRITE(ADDR_VIDEO, SNAKE_COLOUR) = COLOUR_WHITE; 
                } else {
                    IO_WRITE(ADDR_VIDEO, SNAKE_COLOUR) = COLOUR_GREEN;
                }

                move_snake();
            }
        }
        loop_end: usleep(LOOP_SLEEP_TIME);
    }
}