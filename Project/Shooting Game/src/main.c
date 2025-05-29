#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 4
#define ENEMY_SIZE 3
#define BULLET_SIZE 2
#define MAX_BULLETS 10
#define MAX_ENEMY_BULLETS 15
#define MAX_ENEMIES 5

// Player structure
typedef struct {
    int x, y;
    int speed;
} Player;

// Bullet structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int dx, dy;  // Direction vectors
} Bullet;

// Enemy structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int shoot_timer;
} Enemy;

// Game state
Player player;
Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
int game_running = 1;

void Inp_init(void) {
  rcu_periph_clock_enable(RCU_GPIOA);
  rcu_periph_clock_enable(RCU_GPIOC);

  gpio_init(GPIOA, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ,
            GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
  gpio_init(GPIOC, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ,
            GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
}

void IO_init(void) {
  Inp_init(); // inport init
  Lcd_Init(); // LCD init
}

// Safe LCD drawing function with bounds checking
void safe_draw_pixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        LCD_DrawPoint(x, y, color);
    }
}

// Draw a filled rectangle with bounds checking
void draw_rect(int x, int y, int width, int height, u16 color) {
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            safe_draw_pixel(x + i, y + j, color);
        }
    }
}

// Clear a rectangle (erase)
void clear_rect(int x, int y, int width, int height) {
    draw_rect(x, y, width, height, BLACK);
}

// Initialize game objects 
void init_game(void) {
    // Initialize player
    player.x = SCREEN_WIDTH / 2;
    player.y = SCREEN_HEIGHT - 20;
    player.speed = 2;
    
    // Initialize bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        player_bullets[i].active = 0;
        player_bullets[i].dx = 0;
        player_bullets[i].dy = 0;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
        enemy_bullets[i].dx = 0;
        enemy_bullets[i].dy = 0;
    }
    
    // Initialize enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].x = 20 + i * 25;
        enemies[i].y = 20;
        enemies[i].active = 1;
        enemies[i].speed = 1;
        enemies[i].shoot_timer = 0;
    }
    
    game_running = 1;
}

// Handle player input and movement
void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    
    // 添加静态变量来防抖动
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int debounce_counter = 0;
    
    // 防抖动计数器
    if (debounce_counter > 0) {
        debounce_counter--;
    }
    
    // Clear previous player position
    if (prev_x >= 0 && prev_y >= 0) {
        clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    
    // Handle movement - 只在没有防抖动时检查移动
    if (debounce_counter == 0) {
        if (Get_Button(JOY_LEFT) && player.x > 0) {
            player.x -= player.speed;
        }
        if (Get_Button(JOY_RIGHT) && player.x < SCREEN_WIDTH - PLAYER_SIZE) {
            player.x += player.speed;
        }
        if (Get_Button(JOY_UP) && player.y > 0) {
            player.y -= player.speed;
        }
        if (Get_Button(JOY_DOWN) && player.y < SCREEN_HEIGHT - PLAYER_SIZE) {
            player.y += player.speed;
        }
    }

    // Handle BUTTON_1 with proper debouncing
    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 10;  // 10 frame debounce
            debounce_counter = 5;   // 5 frame delay for all inputs
            
            // Find available bullet slot
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!player_bullets[i].active) {
                    player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                    player_bullets[i].y = player.y;
                    player_bullets[i].active = 1;
                    player_bullets[i].speed = 3;
                    player_bullets[i].dx = 0;
                    player_bullets[i].dy = -1;  // Move up
                    break;
                }
            }
        }
    } else {
        button1_debounce = 0;  // Reset when button is released
    }

    // Handle BUTTON_2 with proper debouncing
    if (Get_Button(BUTTON_2)) {
        if (button2_debounce == 0) {
            button2_debounce = 20;  // 20 frame debounce for spread shot
            debounce_counter = 10;  // 10 frame delay for all inputs
            
            // Shoot 8 bullets in different directions
            int directions[8][2] = {
                {0, -1},   // Up
                {1, -1},   // Up-Right
                {1, 0},    // Right
                {1, 1},    // Down-Right
                {0, 1},    // Down
                {-1, 1},   // Down-Left
                {-1, 0},   // Left
                {-1, -1}   // Up-Left
            };
            
            int bullets_fired = 0;
            for (int dir = 0; dir < 8 && bullets_fired < 8; dir++) {
                for (int i = 0; i < MAX_BULLETS && bullets_fired < 8; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                        player_bullets[i].y = player.y;
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 2;
                        player_bullets[i].dx = directions[dir][0];
                        player_bullets[i].dy = directions[dir][1];
                        bullets_fired++;
                        break;
                    }
                }
            }
        }
    } else {
        button2_debounce = 0;  // Reset when button is released
    }
    
    // Decrease debounce counters
    if (button1_debounce > 0) button1_debounce--;
    if (button2_debounce > 0) button2_debounce--;
    
    // Draw player
    draw_rect(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
    
    prev_x = player.x;
    prev_y = player.y;
}

// Update player bullets
void update_player_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            // Clear old position
            clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            
            // Move bullet
            player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
            player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
            
            // Check if bullet is off screen
            if (player_bullets[i].y < 0 || player_bullets[i].y > SCREEN_HEIGHT ||
                player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH) {
                player_bullets[i].active = 0;
            } else {
                // Draw bullet
                u16 bullet_color = (player_bullets[i].dy == -1 && player_bullets[i].dx == 0) ? WHITE : CYAN;
                draw_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE, bullet_color);
            }
        }
    }
}

// Update enemies (simplified)
void update_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            draw_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, RED);
        }
    }
}

// Update enemy bullets (simplified)
void update_enemy_bullets(void) {
    // Simple enemy bullet system - 暂时禁用以避免复杂性
    /*
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            
            enemy_bullets[i].y += enemy_bullets[i].speed;
            
            if (enemy_bullets[i].y > SCREEN_HEIGHT) {
                enemy_bullets[i].active = 0;
            } else {
                draw_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
            }
        }
    }
    */
}

// Main game loop
void game_loop(int difficulty) {
    init_game();
    
    // Clear screen
    LCD_Clear(BLACK);
    
    int frame_counter = 0;
    
    while (game_running) {
        // Update game objects
        update_player();
        update_player_bullets();
        update_enemies();
        // update_enemy_bullets(); // 暂时禁用
        
        // Frame rate control - 更慢的帧率
        delay_1ms(100); // 10 FPS for stability
        
        frame_counter++;
        
        // Exit condition - 添加帧计数器避免立即退出
        if (frame_counter > 10 && Get_Button(JOY_CTR)) {
            game_running = 0;
        }
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();  // Call assembly function
  
  // Start game based on difficulty
  game_loop(difficulty);
  
  return 0;
}