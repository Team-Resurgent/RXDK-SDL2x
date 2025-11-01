#pragma once
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <ctime>
#include <fstream>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>

#include "Player.hpp"
#include "Enemy.hpp"
#include "Entity.hpp"
#include "Effect.hpp"
#include "defs.hpp"

class Game {
private:
    struct {
        SDL_Renderer* renderer;
        SDL_Window* window;
        SDL_Texture* background;
        SDL_Texture* titleScreen;
        SDL_Texture* endScreen;
        Mix_Chunk* sounds[soundChannel - 1];
        Mix_Music* music;
        bool          running;
        SDL_GameController* controller = nullptr; // unused with SDL_Joystick path, harmless if left
    } app;

    struct {
        std::vector<Entity>            bullets;
        std::vector<Entity*>           fighters;
        std::vector<Entity>            powerUp;
        std::vector<Entity>            debrises;
        std::vector<std::vector<Effect>> effects;
    } Entities;

    TTF_Font* font;
    SDL_Texture* debrisTexture[4];
    std::stringstream healthText, scoreText, hiScoreText;
    std::fstream  file;

    Entity        playerBullet, enemyBullet, powerUp, debris;
    Enemy* enemy;
    Effect        explosion;

    int           enemySpawnTimer, enemyFire;
    int           lastY;
    int           gameTicks;

    int           backgroundX;
    int           score, highScore;

    void prepareScene();
    void initGame();
    void initPlayer();
    void titleScreen();
    void endScreen();
    void updateEntities();
    void enterAnimation();
    void getInput();
    void drawBackground();
    void addExplosion(int x, int y);
    void updateHUD();
    void updateScene();

    SDL_Texture* loadTexture(std::string path);
    bool detectCollision(int x1, int y1, int w1, int h1,
        int x2, int y2, int w2, int h2);

public:
    void start();

    // Kept public to allow the joystick helper functions in Game.cpp to synthesize input
    Player player;

    // Drawing helper: optional width/height lets you scale/tile
    void draw(SDL_Texture* texture, int x, int y, int w = -1, int h = -1);
};
