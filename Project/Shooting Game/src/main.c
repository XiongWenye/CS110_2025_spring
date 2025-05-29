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

// Update enemies with movement and shooting
void update_enemies(void) {
    static int enemy_direction = 1;
    static int move_timer = 0;
    
    move_timer++;
    
    // Move enemies every 30 frames
    if (move_timer >= 30) {
        move_timer = 0;
        
        int should_move_down = 0;
        
        // Check if any enemy hits the edge
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active) {
                if ((enemies[i].x <= 0 && enemy_direction < 0) || 
                    (enemies[i].x >= SCREEN_WIDTH - ENEMY_SIZE && enemy_direction > 0)) {
                    should_move_down = 1;
                    break;
                }
            }
        }
        
        // Move enemies
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active) {
                // Clear old position
                clear_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE);
                
                if (should_move_down) {
                    enemies[i].y += 10;  // Move down
                } else {
                    enemies[i].x += enemy_direction * 5;  // Move horizontally
                }
            }
        }
        
        if (should_move_down) {
            enemy_direction *= -1;  // Reverse direction
        }
    }
    
    // Draw enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            draw_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, RED);
        }
    }
}

// Update enemy bullets system
void update_enemy_bullets(void) {
    // Enemy shooting logic
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].shoot_timer++;
            // Shoot every 180 frames (about 3 seconds at 60 FPS)
            if (enemies[i].shoot_timer >= 180) {
                enemies[i].shoot_timer = 0;
                
                // Find available bullet slot
                for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                    if (!enemy_bullets[j].active) {
                        enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                        enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                        enemy_bullets[j].active = 1;
                        enemy_bullets[j].speed = 1;
                        enemy_bullets[j].dx = 0;
                        enemy_bullets[j].dy = 1;  // Move down
                        break;
                    }
                }
            }
        }
    }
    
    // Update enemy bullets
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            // Clear old position
            clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            
            // Move bullet
            enemy_bullets[i].x += enemy_bullets[i].dx * enemy_bullets[i].speed;
            enemy_bullets[i].y += enemy_bullets[i].dy * enemy_bullets[i].speed;
            
            // Check if bullet is off screen
            if (enemy_bullets[i].y > SCREEN_HEIGHT || enemy_bullets[i].x < 0 || 
                enemy_bullets[i].x > SCREEN_WIDTH) {
                enemy_bullets[i].active = 0;
            } else {
                // Draw bullet
                draw_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
            }
        }
    }
}

// Check collisions between bullets and targets
void check_collisions(void) {
    // Check player bullets hitting enemies
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                if (enemies[j].active) {
                    // Simple collision detection
                    if (player_bullets[i].x >= enemies[j].x && 
                        player_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                        player_bullets[i].y >= enemies[j].y && 
                        player_bullets[i].y < enemies[j].y + ENEMY_SIZE) {
                        
                        // Hit! Remove bullet and enemy
                        player_bullets[i].active = 0;
                        enemies[j].active = 0;
                        
                        // Clear enemy from screen
                        clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                    }
                }
            }
        }
    }
    
    // Check enemy bullets hitting player
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            // Simple collision detection with player
            if (enemy_bullets[i].x >= player.x && 
                enemy_bullets[i].x < player.x + PLAYER_SIZE &&
                enemy_bullets[i].y >= player.y && 
                enemy_bullets[i].y < player.y + PLAYER_SIZE) {
                
                // Hit! Remove bullet (could add player damage system here)
                enemy_bullets[i].active = 0;
                // For now, just remove the bullet - you could add lives/health system
            }
        }
    }
}

// Main game loop
void game_loop(int difficulty) {
    init_game();
    
    // Clear screen
    LCD_Clear(BLACK);
    
    while (1) {  // Infinite loop - no exit mechanism
        // Update game objects
        update_player();
        update_player_bullets();
        update_enemies();
        update_enemy_bullets();
        check_collisions();
        
        // Frame rate control - restore normal frame rate
        delay_1ms(16); // Approximately 60 FPS
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();  // Call assembly function
  
  // Start game based on difficulty
  game_loop(difficulty);
  
  return 0;
}