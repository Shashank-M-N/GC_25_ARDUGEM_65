#include <TFT_22_ILI9225.h>

// Define TFT display pins
#define TFT_RST 8
#define TFT_RS 9
#define TFT_CS 10
#define TFT_LED 11

// Initialize TFT display
TFT_22_ILI9225 display(TFT_RST, TFT_RS, TFT_CS, TFT_LED);

// Rotary encoder pins
#define ENC1_CLK 2
#define ENC1_DT 4
#define ENC2_CLK 3
#define ENC2_DT 5
#define ENC1_SW 6
#define ENC2_SW 7

#define ROTATION_THRESHOLD 2  // Turn after 2 rotations

volatile int encoder1Pos = 0, encoder2Pos = 0;
volatile int encoder1Steps = 0, encoder2Steps = 0;
volatile int lastEncoder1Dir = 0, lastEncoder2Dir = 0;

// Snake direction variables
int dir1X = 1, dir1Y = 0;
int dir2X = -1, dir2Y = 0;

// Game display constants
#define MAX_LENGTH 50
#define GRID_SIZE 5
#define SCREEN_WIDTH 176
#define SCREEN_HEIGHT 220

// Define padding areas
#define TOP_PADDING 20     // Top padding (for timer)
#define BOTTOM_PADDING 20  // Bottom padding
// Effective game grid dimensions (without paddings)
#define GRID_WIDTH (SCREEN_WIDTH / GRID_SIZE)
#define GRID_HEIGHT ((SCREEN_HEIGHT - TOP_PADDING - BOTTOM_PADDING) / GRID_SIZE)

// Colors (565 format)
#define COLOR_BROWN 0xA145
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_BLUE 0x001F     // Snake 1 body
#define COLOR_RED 0xF800      // Snake 2 body
#define COLOR_GREEN 0x07E0
#define COLOR_GRAY 0x7BEF

// New head colors with better contrast:
#define COLOR_CYAN 0x07FF     // Bright cyan for snake 1 head
#define COLOR_MAGENTA 0xF81F  // Bright magenta for snake 2 head

// Snake data arrays
int snake1X[MAX_LENGTH], snake1Y[MAX_LENGTH];
int snake2X[MAX_LENGTH], snake2Y[MAX_LENGTH];
int snake1Length = 5, snake2Length = 5;
bool gameStarted = false;
bool snake1Wins = false;
bool snake2Wins = false;

// Previous snake positions for incremental updates
int prevSnake1X[MAX_LENGTH], prevSnake1Y[MAX_LENGTH];
int prevSnake2X[MAX_LENGTH], prevSnake2Y[MAX_LENGTH];
int prevSnake1Length = 0, prevSnake2Length = 0;

// Walls data arrays (5 vertical and 5 horizontal walls)
int wallhX[5], wallhY[5];  // Horizontal walls
int wallvX[5], wallvY[5];  // Vertical walls

// Flags for collision update
bool collisionOccurred = false;
unsigned long wallRespawnTimes[10] = {0};
const unsigned long wallRespawnDelay = 10000;

unsigned long gameStartTime;               // Stores when the game started
const unsigned long gameDuration = 30000;  // 30 seconds in milliseconds

// Food variables
#define FOOD_COUNT 10
int foodX[FOOD_COUNT], foodY[FOOD_COUNT];

// --- Rotary Encoder ISRs ---
void encoder1ISR() {
  int stateA = digitalRead(ENC1_CLK);
  int stateB = digitalRead(ENC1_DT);
  int direction = (stateA == stateB) ? -1 : 1;
  
  if (direction != lastEncoder1Dir)
    encoder1Steps = 0;
  encoder1Steps += direction;
  lastEncoder1Dir = direction;
  
  if (abs(encoder1Steps) >= ROTATION_THRESHOLD) {
    encoder1Pos += direction;
    encoder1Steps = 0;
  }
}

void encoder2ISR() {
  int stateA = digitalRead(ENC2_CLK);
  int stateB = digitalRead(ENC2_DT);
  int direction = (stateA == stateB) ? -1 : 1;
  
  if (direction != lastEncoder2Dir)
    encoder2Steps = 0;
  encoder2Steps += direction;
  lastEncoder2Dir = direction;
  
  if (abs(encoder2Steps) >= ROTATION_THRESHOLD) {
    encoder2Pos += direction;
    encoder2Steps = 0;
  }
}

// --- Update Directions Based on Encoder Input ---
void updateDirection() {
  Serial.print("Encoder1: ");
  Serial.println(encoder1Pos);
  Serial.print("Encoder2: ");
  Serial.println(encoder2Pos);
  
  if (encoder2Pos > 0) {
    int temp = dir2X;
    dir2X = -dir2Y;
    dir2Y = temp;
    encoder2Pos = 0;
  } else if (encoder2Pos < 0) {
    int temp = dir2X;
    dir2X = dir2Y;
    dir2Y = -temp;
    encoder2Pos = 0;
  }
  
  if (encoder1Pos > 0) {
    int temp = dir1X;
    dir1X = -dir1Y;
    dir1Y = temp;
    encoder1Pos = 0;
  } else if (encoder1Pos < 0) {
    int temp = dir1X;
    dir1X = dir1Y;
    dir1Y = -temp;
    encoder1Pos = 0;
  }
}

// --- Drawing Helper ---
void drawSegment(int gridX, int gridY, uint16_t color) {
  display.fillRectangle(gridX * GRID_SIZE,
                        gridY * GRID_SIZE + TOP_PADDING,
                        (gridX * GRID_SIZE) + (GRID_SIZE - 1),
                        (gridY * GRID_SIZE) + (GRID_SIZE - 1) + TOP_PADDING,
                        color);
}

// Checks if a given position is occupied by a snake, food, or wall.
bool isPositionOccupied(int x, int y) {
  for (int i = 0; i < snake1Length; i++) {
    if (snake1X[i] == x && snake1Y[i] == y)
      return true;
  }
  for (int i = 0; i < snake2Length; i++) {
    if (snake2X[i] == x && snake2Y[i] == y)
      return true;
  }
  for (int i = 0; i < FOOD_COUNT; i++) {
    if (foodX[i] == x && foodY[i] == y)
      return true;
  }
  // Check vertical walls (each covers 3 grid cells)
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (x == wallvX[i] && y == wallvY[i] + j)
        return true;
    }
  }
  // Check horizontal walls (each covers 3 grid cells)
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (x == wallhX[i] + j && y == wallhY[i])
        return true;
    }
  }
  return false;
}

// --- Food Functions ---
void spawnFood() {
  for (int i = 0; i < FOOD_COUNT; i++) {
    int x, y;
    bool validPosition;
    do {
      validPosition = true;
      x = random(0, GRID_WIDTH);
      y = random(0, GRID_HEIGHT);
      
      // Check if the position is occupied by any walls
      for (int j = 0; j < 5; j++) {
        for (int k = 0; k < 3; k++) {
          if ((x == wallvX[j] && y == wallvY[j] + k) ||
              (x == wallhX[j] + k && y == wallhY[j])) {
            validPosition = false;
            break;
          }
        }
        if (!validPosition)
          break;
      }
      
      // Check if the position is occupied by a snake
      if (validPosition && isPositionOccupied(x, y)) {
        validPosition = false;
      }
    } while (!validPosition);
    
    foodX[i] = x;
    foodY[i] = y;
  }
}

void checkFoodCollision(int &length, int snakeX[], int snakeY[]) {
  for (int i = 0; i < FOOD_COUNT; i++) {
    if (snakeX[0] == foodX[i] && snakeY[0] == foodY[i]) {
      if (length < MAX_LENGTH)
        length++;
      int newX, newY;
      collisionOccurred = true;
      do {
        newX = random(0, GRID_WIDTH);
        newY = random(0, GRID_HEIGHT);
      } while (isPositionOccupied(newX, newY));
      foodX[i] = newX;
      foodY[i] = newY;
    }
  }
}

void drawFood() {
  for (int i = 0; i < FOOD_COUNT; i++) {
    drawSegment(foodX[i], foodY[i], COLOR_GREEN);
  }
}

// --- Wall Functions ---
void spawnWalls() {
  // Spawn vertical walls
  for (int i = 0; i < 5; i++) {
    bool validPosition;
    do {
      validPosition = true;
      wallvX[i] = random(1, GRID_WIDTH - 1);
      wallvY[i] = random(1, GRID_HEIGHT - 2);
      for (int j = 0; j < i; j++) {
        if (wallvX[i] == wallvX[j] && abs(wallvY[i] - wallvY[j]) <= 2) {
          validPosition = false;
          break;
        }
      }
    } while (!validPosition);
  }
  // Spawn horizontal walls
  for (int i = 0; i < 5; i++) {
    bool validPosition;
    do {
      validPosition = true;
      wallhX[i] = random(1, GRID_WIDTH - 2);
      wallhY[i] = random(1, GRID_HEIGHT - 1);
      for (int j = 0; j < i; j++) {
        if (wallhY[i] == wallhY[j] && abs(wallhX[i] - wallhX[j]) <= 2) {
          validPosition = false;
          break;
        }
      }
    } while (!validPosition);
  }
}

void checkWallCollision(int &length, int snakeX[], int snakeY[]) {
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (snakeX[0] == wallvX[i] && snakeY[0] == wallvY[i] + j) {
        length = 5;
        collisionOccurred = true;
        return;
      }
    }
  }
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (snakeX[0] == wallhX[i] + j && snakeY[0] == wallhY[i]) {
        length = 5;
        collisionOccurred = true;
        return;
      }
    }
  }
}

void drawWalls() {
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      drawSegment(wallvX[i], wallvY[i] + j, COLOR_GRAY);
    }
  }
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      drawSegment(wallhX[i] + j, wallhY[i], COLOR_GRAY);
    }
  }
}

// --- Test Display Function ---
void testDisplay() {
  display.clear();
  display.setBackgroundColor(COLOR_BLACK);
  display.setFont(Terminal6x8);
  display.drawText(10, TOP_PADDING + 10, "Display Test", COLOR_WHITE);
  display.drawText(10, TOP_PADDING + 30, "Working!", COLOR_GREEN);
  delay(2000);
}

// --- Move Snake (with grid wrapping) ---
void moveSnake(int snakeX[], int snakeY[], int &length, int dirX, int dirY) {
  int oldTailX = snakeX[length - 1];
  int oldTailY = snakeY[length - 1];
  
  for (int i = length - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }
  snakeX[0] += dirX;
  snakeY[0] += dirY;
  
  if (snakeX[0] < 0)
    snakeX[0] = GRID_WIDTH - 1;
  if (snakeX[0] >= GRID_WIDTH)
    snakeX[0] = 0;
  if (snakeY[0] < 0)
    snakeY[0] = GRID_HEIGHT - 1;
  if (snakeY[0] >= GRID_HEIGHT)
    snakeY[0] = 0;
}

// --- Check for Snake-to-Snake Collision ---
void checkSnakeCollision() {
  for (int i = 0; i < snake2Length; i++) {
    if (snake1X[0] == snake2X[i] && snake1Y[0] == snake2Y[i]) {
      snake1Length = 5;
      collisionOccurred = true;
    }
  }
  for (int i = 0; i < snake1Length; i++) {
    if (snake2X[0] == snake1X[i] && snake2Y[0] == snake1Y[i]) {
      snake2Length = 5;
      collisionOccurred = true;
    }
  }
}

// --- Display Timer (in seconds) ---
void displayTime(int secondsRemaining) {
  display.fillRectangle(0, 0, SCREEN_WIDTH, TOP_PADDING, COLOR_BROWN);
  display.setFont(Terminal6x8);
  display.drawText(50, 2, "Time: " + String(secondsRemaining), COLOR_WHITE);
}

// --- Draw Bottom Padding ---
void displayBottomPadding() {
  display.fillRectangle(0, SCREEN_HEIGHT - BOTTOM_PADDING, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BROWN);
}

// --- Incremental Update Function ---
void updateSnakeDisplay(int prevLength, int prevX[], int prevY[],
                        int newLength, int snakeX[], int snakeY[],
                        uint16_t headColor, uint16_t bodyColor) {
  // Erase cells that are no longer occupied
  for (int i = 0; i < prevLength; i++) {
    bool stillPresent = false;
    for (int j = 0; j < newLength; j++) {
      if (prevX[i] == snakeX[j] && prevY[i] == snakeY[j]) {
        stillPresent = true;
        break;
      }
    }
    if (!stillPresent) {
      drawSegment(prevX[i], prevY[i], COLOR_BLACK);
    }
  }
  // Draw snake using new positions
  for (int i = 0; i < newLength; i++) {
    if (i == 0)
      drawSegment(snakeX[i], snakeY[i], headColor);
    else
      drawSegment(snakeX[i], snakeY[i], bodyColor);
  }
}

// --- End Screen ---
void showEndScreen() {
  if (snake1Length > snake2Length)
    snake1Wins = true;
  else if (snake2Length > snake1Length)
    snake2Wins = true;
  
  gameStarted = false;
  
  display.clear();
  display.setBackgroundColor(COLOR_BLACK);
  
  if (snake1Wins)
    display.drawText(50, (SCREEN_HEIGHT / 2), "Snake 1 Wins!", COLOR_WHITE);
  else if (snake2Wins)
    display.drawText(50, (SCREEN_HEIGHT / 2), "Snake 2 Wins!", COLOR_WHITE);
  else
    display.drawText(50, (SCREEN_HEIGHT / 2), "It's a draw", COLOR_WHITE);

  // Reset variables to initial values
  snake1Length = 5;
  snake2Length = 5;
  dir1X = 1; dir1Y = 0;
  dir2X = -1; dir2Y = 0;
  encoder1Pos = 0; encoder2Pos = 0;
  encoder1Steps = 0; encoder2Steps = 0;
  lastEncoder1Dir = 0; lastEncoder2Dir = 0;
  snake1Wins = false;
  snake2Wins = false;
  collisionOccurred = false;
  for (int i = 0; i < MAX_LENGTH; i++) {
    snake1X[i] = 0; snake1Y[i] = 0;
    snake2X[i] = 0; snake2Y[i] = 0;
    prevSnake1X[i] = 0; prevSnake1Y[i] = 0;
    prevSnake2X[i] = 0; prevSnake2Y[i] = 0;
  }
  for (int i = 0; i < 5; i++) {
    wallhX[i] = 0; wallhY[i] = 0;
    wallvX[i] = 0; wallvY[i] = 0;
  }
  for (int i = 0; i < FOOD_COUNT; i++) {
    foodX[i] = 0; foodY[i] = 0;
  }
}

// --- Start Screen ---
void showStartScreen() {
  Serial.print("here");
  display.drawText(18, (TOP_PADDING + (SCREEN_HEIGHT - BOTTOM_PADDING)) / 2,
                   "Press Button to Start", COLOR_WHITE);
}

// --- Check if Start Button is Pressed ---
bool checkStartPressed() {
  return (digitalRead(ENC1_SW) == LOW && digitalRead(ENC2_SW) == LOW);
}

// --- Main Game Loop ---
void gameLoop() {
  gameStartTime = millis();
  collisionOccurred = false;
  
  // Initialize snake positions
  for (int i = 0; i < snake1Length; i++) {
    snake1X[i] = GRID_WIDTH / 4 + i;
    snake1Y[i] = (3 * GRID_HEIGHT) / 4;
  }
  for (int i = 0; i < snake2Length; i++) {
    snake2X[i] = (3 * GRID_WIDTH) / 4 - i;
    snake2Y[i] = GRID_HEIGHT / 4;
  }
  
  // Initialize previous snake arrays
  prevSnake1Length = snake1Length;
  for (int i = 0; i < snake1Length; i++) {
    prevSnake1X[i] = snake1X[i];
    prevSnake1Y[i] = snake1Y[i];
  }
  prevSnake2Length = snake2Length;
  for (int i = 0; i < snake2Length; i++) {
    prevSnake2X[i] = snake2X[i];
    prevSnake2Y[i] = snake2Y[i];
  }
  
  spawnWalls();
  spawnFood();
  drawWalls();
  drawFood();
  
  Serial.println("Game started");
  
  // Draw initial snakes
  for (int i = 0; i < snake1Length; i++) {
    if (i == 0)
      drawSegment(snake1X[i], snake1Y[i], COLOR_CYAN);
    else
      drawSegment(snake1X[i], snake1Y[i], COLOR_BLUE);
  }
  for (int i = 0; i < snake2Length; i++) {
    if (i == 0)
      drawSegment(snake2X[i], snake2Y[i], COLOR_MAGENTA);
    else
      drawSegment(snake2X[i], snake2Y[i], COLOR_RED);
  }
  
  displayBottomPadding();
  
  while (true) {
    unsigned long elapsed = millis() - gameStartTime;
    if (elapsed >= gameDuration)
      break;
    int secondsRemaining = (gameDuration - elapsed) / 1000;
    displayTime(secondsRemaining);
    
    // Save current snake positions before moving
    prevSnake1Length = snake1Length;
    for (int i = 0; i < snake1Length; i++) {
      prevSnake1X[i] = snake1X[i];
      prevSnake1Y[i] = snake1Y[i];
    }
    prevSnake2Length = snake2Length;
    for (int i = 0; i < snake2Length; i++) {
      prevSnake2X[i] = snake2X[i];
      prevSnake2Y[i] = snake2Y[i];
    }
    
    updateDirection();
    
    // Move snakes
    moveSnake(snake1X, snake1Y, snake1Length, dir1X, dir1Y);
    moveSnake(snake2X, snake2Y, snake2Length, dir2X, dir2Y);
    
    // Extra move if button pressed
    if (digitalRead(ENC1_SW) == LOW) {
      moveSnake(snake1X, snake1Y, snake1Length, dir1X, dir1Y);
    }
    if (digitalRead(ENC2_SW) == LOW) {
      moveSnake(snake2X, snake2Y, snake2Length, dir2X, dir2Y);
    }
    
    // Check collisions
    checkFoodCollision(snake1Length, snake1X, snake1Y);
    checkFoodCollision(snake2Length, snake2X, snake2Y);
    checkWallCollision(snake1Length, snake1X, snake1Y);
    checkWallCollision(snake2Length, snake2X, snake2Y);
    checkSnakeCollision();
    
    // Incremental update: update only changed cells
    updateSnakeDisplay(prevSnake1Length, prevSnake1X, prevSnake1Y,
                       snake1Length, snake1X, snake1Y,
                       COLOR_CYAN, COLOR_BLUE);
    updateSnakeDisplay(prevSnake2Length, prevSnake2X, prevSnake2Y,
                       snake2Length, snake2X, snake2Y,
                       COLOR_MAGENTA, COLOR_RED);
    
    // Redraw food and bottom padding
    drawFood();
    displayBottomPadding();
    
    delay(100);
  }
  showEndScreen();
}

void setup() {
  Serial.begin(9600);
  display.begin();
  testDisplay();
  display.setBackgroundColor(COLOR_BLACK);
  display.clear();
  display.setFont(Terminal6x8);
  
  pinMode(ENC1_CLK, INPUT);
  pinMode(ENC1_DT, INPUT);
  pinMode(ENC2_CLK, INPUT);
  pinMode(ENC2_DT, INPUT);
  pinMode(ENC1_SW, INPUT_PULLUP);
  pinMode(ENC2_SW, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(ENC1_CLK), encoder1ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC2_CLK), encoder2ISR, CHANGE);
  
  showStartScreen();
}

void loop() {
  if (!gameStarted && checkStartPressed()) {
    gameStarted = true;
    display.clear();
    gameLoop();
  }
}