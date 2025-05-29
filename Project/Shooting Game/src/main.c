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
    int dx, dy;  // Direction vectors (new fields)
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

// Initialize game objects 
void init_game(void) {
    // Initialize player
    player.x = SCREEN_WIDTH / 2;
    player.y = SCREEN_HEIGHT - 20;
    player.speed = 2;
    
    // Initialize bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        player_bullets[i].active = 0;
        player_bullets[i].dx = 0;  // 初始化方向
        player_bullets[i].dy = 0;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
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

// Draw a filled rectangle (simple pixel representation)
void draw_rect(int x, int y, int width, int height, u16 color) {
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            if (x + i < SCREEN_WIDTH && y + j < SCREEN_HEIGHT && 
                x + i >= 0 && y + j >= 0) {
                LCD_DrawPoint(x + i, y + j, color);
            }
        }
    }
}

// Clear a rectangle (erase)
void clear_rect(int x, int y, int width, int height) {
    draw_rect(x, y, width, height, BLACK);
}

// Handle player input and movement
void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    
    // Clear previous player position
    if (prev_x != -1) {
        clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    
    // Handle movement
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
    
    // Handle shooting
    if (Get_Button(BUTTON_1)) {
        // Find available bullet slot
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!player_bullets[i].active) {
                player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                player_bullets[i].y = player.y;
                player_bullets[i].active = 1;
                player_bullets[i].speed = 3;
                break;
            }
        }
    }

    // BUTTON_2 shoots bullets in all directions like a circle
    static int button2_debounce = 0;
    if (Get_Button(BUTTON_2)) {
        if (!button2_debounce) {
            button2_debounce = 1;
            
            // Shoot 8 bullets in different directions (circle pattern)
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
                // Find available bullet slot
                for (int i = 0; i < MAX_BULLETS && bullets_fired < 8; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                        player_bullets[i].y = player.y;
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 2;
                        // Store direction in unused fields (we'll need to modify the structure)
                        player_bullets[i].dx = directions[dir][0];
                        player_bullets[i].dy = directions[dir][1];
                        bullets_fired++;
                        break;
                    }
                }
            }
        }
    } else {
        button2_debounce = 0;
    }
    
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
            
            // Move bullet based on direction
            if (player_bullets[i].dx == 0 && player_bullets[i].dy == 0) {
                // Regular bullet (BUTTON_1) - moves straight up
                player_bullets[i].y -= player_bullets[i].speed;
            } else {
                // Directional bullet (BUTTON_2) - moves in specified direction
                player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
                player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
            }
            
            // Check if bullet is off screen
            if (player_bullets[i].y < 0 || player_bullets[i].y > SCREEN_HEIGHT ||
                player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH) {
                player_bullets[i].active = 0;
            } else {
                // Draw bullet
                u16 bullet_color = (player_bullets[i].dx == 0 && player_bullets[i].dy == 0) ? WHITE : CYAN;
                draw_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE, bullet_color);
            }
        }
    }
}

// Update enemies
void update_enemies(void) {
    static int enemy_direction = 1;
    static int move_timer = 0;
    
    move_timer++;
    
    // Move enemies every 30 frames
    if (move_timer >= 30) {
        move_timer = 0;
        
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active) {
                // Clear old position
                clear_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE);
                
                // Move horizontally
                enemies[i].x += enemy_direction * enemies[i].speed * 5;
                
                // Check boundaries and move down
                if (enemies[i].x <= 0 || enemies[i].x >= SCREEN_WIDTH - ENEMY_SIZE) {
                    enemy_direction *= -1;
                    enemies[i].y += 10;
                }
            }
        }
    }
    
    // Draw enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            draw_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, RED);
        }
    }
}

// Update enemy shooting
void update_enemy_bullets(void) {
    // Enemy shooting logic
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].shoot_timer++;
            // Shoot every 120 frames (about 2 seconds at 60 FPS)
            if (enemies[i].shoot_timer >= 120) {
                enemies[i].shoot_timer = 0;
                
                // Find available bullet slot
                for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                    if (!enemy_bullets[j].active) {
                        enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                        enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                        enemy_bullets[j].active = 1;
                        enemy_bullets[j].speed = 2;
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
            
            // Move bullet down
            enemy_bullets[i].y += enemy_bullets[i].speed;
            
            // Check if bullet is off screen
            if (enemy_bullets[i].y > SCREEN_HEIGHT) {
                enemy_bullets[i].active = 0;
            } else {
                // Draw bullet
                draw_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
            }
        }
    }
}

// Main game loop
void game_loop(int difficulty) {
    init_game();
    
    // Clear screen
    LCD_Clear(BLACK);
    
    while (game_running) {
        // Update game objects
        update_player();
        update_player_bullets();
        update_enemies();
        update_enemy_bullets();
        
        // Small delay to control frame rate
        delay_1ms(16); // Approximately 60 FPS
        
        // Exit condition (optional)
        if (Get_Button(BUTTON_1)) {
            game_running = 0;
        }
    }
}

void Board_self_test(void) {
  while (1) {
    LCD_ShowString(60, 25, (u8*)"TEST (25s)", WHITE);
    if (Get_Button(JOY_LEFT)) {
      LCD_ShowString(5, 25, (u8*)"L", BLUE);
    }
    if (Get_Button(JOY_DOWN)) {
      LCD_ShowString(25, 45, (u8*)"D", BLUE);
      LCD_ShowString(60, 25, (u8*)"TEST", GREEN);
    }
    if (Get_Button(JOY_UP)) {
      LCD_ShowString(25, 5, (u8*)"U", BLUE);
    }
    if (Get_Button(JOY_RIGHT)) {
      LCD_ShowString(45, 25, (u8*)"R", BLUE);
    }
    if (Get_Button(JOY_CTR)) {
      LCD_ShowString(25, 25, (u8*)"C", BLUE);
    }
    if (Get_Button(BUTTON_1)) {
      LCD_ShowString(60, 5, (u8*)"SW1", BLUE);
    }
    if (Get_Button(BUTTON_2)) {
      LCD_ShowString(60, 45, (u8*)"SW2", BLUE);
    }
    draw();
    delay_1ms(10);
    LCD_Clear(BLACK);
  }
}

int main(void) {
  IO_init();
  timer_parameter_struct timer_initpara;
  timer_init(TIMER1, &timer_initpara);  // Initialize the timer for FPS calculation
  
  int difficulty = select();  // Call assembly function
  
  // Handle the returned difficulty level and start game
  switch(difficulty) {
      case 0:
          // Start easy mode
          game_loop(0);
          break;
      case 1: 
          // Start normal mode
          game_loop(1);
          break;
      case 2:
          // Start hard mode
          game_loop(2);
          break;
  }
  
  return 0;
}