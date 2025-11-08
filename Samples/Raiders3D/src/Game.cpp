#include "Game.hpp"
using namespace std;

extern "C" void cdecl __CxxFrameHandler3() {}

// ---------------- Xbox joystick support ----------------
static SDL_Joystick* gJoystick = nullptr;

// Deadzone with hysteresis (enter must exceed ENTER, release falls below EXIT)
static const int DZ_ENTER = 12000;  // enter movement
static const int DZ_EXIT = 8000;   // release movement

// XInput-style Y is positive-up in your driver; set +1 to match that
// If you switch to stock SDL (up = negative), change to -1.
static const int Y_SIGN = +1;

// Button mappings that match the custom XBOX driver you posted
static const int BTN_A = 0;
static const int BTN_B = 1;
static const int BTN_X = 2;
static const int BTN_Y = 3;
static const int BTN_BLACK = 4;   // original Xbox "Black"
static const int BTN_WHITE = 5;   // original Xbox "White"
static const int BTN_LT = 6;   // left trigger (reported as button)
static const int BTN_RT = 7;   // right trigger (reported as button)
static const int BTN_START = 8;
static const int BTN_BACK = 9;
static const int BTN_LS = 10;  // left stick click
static const int BTN_RS = 11;  // right stick click
// D-pad is a hat via SDL_JOYHATMOTION

// Held states to avoid repeat spam
static bool gLeft = false, gRight = false, gUp = false, gDown = false, gFire = false;

// --- Utilities --------------------------------------------------------------

static void pushKeyEvent(bool down, SDL_Scancode sc) {
    SDL_Event ev{};
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;

    SDL_KeyboardEvent& ke = ev.key;
    ke.type = ev.type;
    ke.state = down ? SDL_PRESSED : SDL_RELEASED;
    ke.repeat = 0;
    ke.keysym.scancode = sc;
    ke.keysym.sym = SDL_GetKeyFromScancode(sc);
    ke.keysym.mod = KMOD_NONE;

    SDL_PushEvent(&ev);
}

static void setHeld(bool& held, bool want, SDL_Scancode sc) {
    if (want != held) {
        pushKeyEvent(want, sc);
        held = want;
    }
}

/// Call Player directly (no event queue) for critical inputs like fire.
static void directPlayerKey(Game* g, bool down, SDL_Scancode sc) {
    SDL_KeyboardEvent ke{};
    ke.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ke.state = down ? SDL_PRESSED : SDL_RELEASED;
    ke.repeat = 0;
    ke.keysym.scancode = sc;
    ke.keysym.sym = SDL_GetKeyFromScancode(sc);
    ke.keysym.mod = KMOD_NONE;
    if (down) g->player.keyDown(&ke); else g->player.keyUp(&ke);
}

// Fire = LSHIFT (matches Player.cpp)
static void setFireHeld(Game* g, bool want) {
    if (want != gFire) {
        directPlayerKey(g, want, SDL_SCANCODE_LSHIFT);
        gFire = want;
    }
}

// Helpers with hysteresis
static bool want_neg(int value, bool currently_on) {
    if (!currently_on) return value < -DZ_ENTER;
    else               return value <= -DZ_EXIT;
}
static bool want_pos(int value, bool currently_on) {
    if (!currently_on) return value > DZ_ENTER;
    else               return value >= DZ_EXIT;
}

// Poll joystick each frame to maintain held states
// Poll joystick each frame to maintain held states
static void pollJoystickState(Game* g) {
    if (!gJoystick) return;

    // 1) Refresh SDL's internal joystick state
    SDL_JoystickUpdate();

    // 2) Read axes
    const Sint16 x = SDL_JoystickGetAxis(gJoystick, 0);
    const Sint16 y = SDL_JoystickGetAxis(gJoystick, 1);
    const int yAdj = y * Y_SIGN; // flip if your driver uses opposite sign

    // Axis intents with hysteresis
    bool wantLeft_axis = want_neg(x, gLeft);
    bool wantRight_axis = want_pos(x, gRight);
    bool wantUp_axis = want_pos(yAdj, gUp);    // up after adjust is positive
    bool wantDown_axis = want_neg(yAdj, gDown);  // down after adjust is negative

    // Don't allow both directions from axes at once
    if (wantLeft_axis && wantRight_axis) { wantLeft_axis = wantRight_axis = false; }
    if (wantUp_axis && wantDown_axis) { wantUp_axis = wantDown_axis = false; }

    // 3) Read hat (if present) -> hat intents (no hysteresis needed)
    bool wantLeft_hat = false, wantRight_hat = false, wantUp_hat = false, wantDown_hat = false;
    if (SDL_JoystickNumHats(gJoystick) > 0) {
        const Uint8 hat = SDL_JoystickGetHat(gJoystick, 0);
        wantLeft_hat = (hat & SDL_HAT_LEFT) != 0;
        wantRight_hat = (hat & SDL_HAT_RIGHT) != 0;
        wantUp_hat = (hat & SDL_HAT_UP) != 0;
        wantDown_hat = (hat & SDL_HAT_DOWN) != 0;
    }

    // 4) Combine sources (axis OR hat) and apply once per direction
    const bool wantLeft = wantLeft_axis || wantLeft_hat;
    const bool wantRight = wantRight_axis || wantRight_hat;
    const bool wantUp = wantUp_axis || wantUp_hat;
    const bool wantDown = wantDown_axis || wantDown_hat;

    setHeld(gLeft, wantLeft, SDL_SCANCODE_LEFT);
    setHeld(gRight, wantRight, SDL_SCANCODE_RIGHT);
    setHeld(gUp, wantUp, SDL_SCANCODE_UP);
    setHeld(gDown, wantDown, SDL_SCANCODE_DOWN);

    // 5) Fire on A or RT (unchanged)
    const bool wantFire = SDL_JoystickGetButton(gJoystick, BTN_A) ||
        SDL_JoystickGetButton(gJoystick, BTN_RT);
    setFireHeld(g, wantFire);
}


// ---------------------------------------------------------------------------

void Game::start() {
    initGame();
    while (true) {
        titleScreen();
        while (app.running) {
            if (!player.enterStatus()) {
                enterAnimation();
                continue;
            }
            prepareScene();
            drawBackground();
            if (player.getHP() > 0) {
                getInput();
            }
            updateEntities();
            updateHUD();
            updateScene();
        }
    }
}

void Game::titleScreen() {
    SDL_Event e;

    if (!Mix_PlayingMusic() && app.music) {
        Mix_PlayMusic(app.music, -1);
    }

    while (true) {
        drawBackground();
        draw(app.titleScreen, 0, 0, WIDTH, HEIGHT);
        updateScene();

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                SDL_Quit();
                break;

            case SDL_JOYBUTTONDOWN:
                if (e.jbutton.button == BTN_START || e.jbutton.button == BTN_A) {      // Start game
                    initPlayer();
                    app.running = true;
                    if (app.sounds[SOUND_BUTTON]) Mix_PlayChannel(CH_MENU, app.sounds[SOUND_BUTTON], 0);
                    return;
                }
                if (e.jbutton.button == BTN_Y) {      // Toggle music
                    if (app.sounds[SOUND_BUTTON]) Mix_PlayChannel(CH_MENU, app.sounds[SOUND_BUTTON], 0);
                    if (Mix_PausedMusic()) Mix_ResumeMusic(); else Mix_PauseMusic();
                }
                if (e.jbutton.button == BTN_BACK) {
                    if (app.sounds[SOUND_BUTTON]) Mix_PlayChannel(CH_MENU, app.sounds[SOUND_BUTTON], 0);
                    SDL_Quit();
                }
                break;

            default: break;
            }
        }

        // keep polling so you can navigate with D-pad later if needed
        pollJoystickState(this);
    }
}

void Game::endScreen() {
    SDL_Event e;
    scoreText << "Score  : " << score;

    if (!Mix_PlayingMusic() && app.music) {
        Mix_PlayMusic(app.music, -1);
    }

    while (true) {
        drawBackground();
        draw(app.endScreen, 0, 0, WIDTH, HEIGHT);
        SDL_Surface* scoreSurface = TTF_RenderText_Solid(font, scoreText.str().c_str(), { 255, 255, 255, 0 });
        SDL_Texture* scoreTXT = SDL_CreateTextureFromSurface(app.renderer, scoreSurface);
        draw(scoreTXT, 0, 0);
        SDL_FreeSurface(scoreSurface);
        SDL_DestroyTexture(scoreTXT);
        updateScene();

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                SDL_Quit();
                break;

            case SDL_JOYBUTTONDOWN:
                if (e.jbutton.button == BTN_A) { // restart
                    initPlayer();
                    app.running = true;
                    if (app.sounds[SOUND_BUTTON]) Mix_PlayChannel(CH_MENU, app.sounds[SOUND_BUTTON], 0);
                    return;
                }
                if (e.jbutton.button == BTN_START || e.jbutton.button == BTN_BACK) { // exit to title
                    if (app.sounds[SOUND_BUTTON]) Mix_PlayChannel(CH_MENU, app.sounds[SOUND_BUTTON], 0);
                    return;
                }
                break;

            default: break;
            }
        }

        pollJoystickState(this);
    }
}

void Game::enterAnimation() {
    prepareScene();
    drawBackground();
    player.setX(player.getX() - 15);
    draw(player.getTexture(), player.getX(), player.getY());
    if (player.getX() < 150) {
        player.setEnterStatus(true);
    }
    updateScene();
}

void Game::initGame() {
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) < 0) {
        cout << "Could not initialize SDL: " << SDL_GetError() << endl;
        SDL_Quit();
    }

    // Open first joystick (if present)
    const int njoy = SDL_NumJoysticks();
    if (njoy > 0) {
        gJoystick = SDL_JoystickOpen(0);
        if (gJoystick) {
            SDL_JoystickEventState(SDL_ENABLE); // events still enabled for buttons/hat; we ignore axis events
        }
        else {
            SDL_Log("SDL_JoystickOpen(0) failed: %s", SDL_GetError());
        }
    }
    else {
        SDL_Log("No joystick detected.");
    }

    app.window = SDL_CreateWindow("Space Impact V1.5", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
    if (!app.window) {
        cout << "Could not create window : " << SDL_GetError() << endl;
        SDL_Quit();
    }
    SDL_Surface* sf = IMG_Load(icon);
    SDL_SetWindowIcon(app.window, sf);
    if (sf) SDL_FreeSurface(sf);

    app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");   // nicer scaling or use Nearest for pixel art
    SDL_RenderSetLogicalSize(app.renderer, WIDTH, HEIGHT);  // Resolution
    if (!app.renderer) {
        cout << "Could not create renderer : " << SDL_GetError() << endl;
        SDL_Quit();
    }

    // Robust SDL_image init
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        cout << "Could not initialize SDL_image: " << IMG_GetError() << endl;
        SDL_Quit();
    }

    if (TTF_Init() != 0) {
        cout << "Could not initialize TTF : " << TTF_GetError() << endl;
        SDL_Quit();
    }
    font = TTF_OpenFont("D:\\resources\\fonts\\myriadProRegular.ttf", 22);
    score = 0;

    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) != 0) {
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
        // don't exit; game can run silently
    }

    Mix_AllocateChannels(soundChannel);

    // Music & SFX
    app.music = Mix_LoadMUS(bgSound);
    if (!app.music) {
        SDL_Log("Mix_LoadMUS('%s') failed: %s", bgSound, Mix_GetError());
    }
    app.sounds[SOUND_FIRE] = Mix_LoadWAV(fireSound);
    app.sounds[SOUND_EXPLOSION] = Mix_LoadWAV(explosionSound);
    app.sounds[SOUND_BUTTON] = Mix_LoadWAV(btSound);

    app.background = loadTexture(backgroundTexture);
    backgroundX = 0;
    app.titleScreen = loadTexture(titleScreenTexture);
    app.endScreen = loadTexture(endScreenTexture);
    app.running = false;

    file.open("scores.txt", ios::in);
    file >> highScore;

    enemySpawnTimer = 60;

    player.setIdentity(pPlane);
    player.setTexture(loadTexture(playerTexture));

    playerBullet.setDX(playerBulletSpeed);
    playerBullet.updateHP(bulletHP);
    playerBullet.setIdentity(normalBullet);
    playerBullet.setTexture(loadTexture(waveBulletTexture));

    enemyBullet.setDX(enemyBulletSpeed);
    enemyBullet.setHP(1);
    enemyBullet.setIdentity(eBullet);
    enemyBullet.setTexture(loadTexture(enemyBulletTexture));
    enemyFire = 0;

    debris.setHP(1);
    debris.setIdentity(shipDebris);
    debrisTexture[0] = loadTexture("D:\\resources\\sprites\\debris1.png");
    debrisTexture[1] = loadTexture("D:\\resources\\sprites\\debris2.png");
    debrisTexture[2] = loadTexture("D:\\resources\\sprites\\debris3.png");
    debrisTexture[3] = loadTexture("D:\\resources\\sprites\\debris4.png");

    explosion.setTexture(loadTexture(explosionTexture));

    if (app.music) Mix_PlayMusic(app.music, -1);
    gameTicks = 0;
}

void Game::initPlayer() {
    player.setX(WIDTH / 2);
    player.setY(HEIGHT / 2 - 50);
    player.setHP(10);
    player.setDieStatus(false);
    player.setEnterStatus(false);
    player.setBulletType(normalBullet);
    player.resetInput();

    // clear held joystick states
    gLeft = gRight = gUp = gDown = gFire = false;
}

void Game::getInput() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            SDL_Quit();
            break;

            // Forward real keyboard events to Player
        case SDL_KEYDOWN:
            player.keyDown(&e.key);
            break;
        case SDL_KEYUP:
            player.keyUp(&e.key);
            break;

            // We **do not** convert SDL_JOYAXISMOTION here. (Polling handles axes.)
        case SDL_JOYHATMOTION:
            // hat->arrows also handled in pollJoystickState; ignoring here is fine
            break;

            // Buttons
        case SDL_JOYBUTTONDOWN:
            if (e.jbutton.button == BTN_A || e.jbutton.button == BTN_RT) {
                setFireHeld(this, true);   // fire down
            }
            if (e.jbutton.button == BTN_START) {
                pushKeyEvent(true, SDL_SCANCODE_ESCAPE); // pause/esc
            }
            break;

        case SDL_JOYBUTTONUP:
            if (e.jbutton.button == BTN_A || e.jbutton.button == BTN_RT) {
                setFireHeld(this, false);  // fire up
            }
            if (e.jbutton.button == BTN_START) {
                pushKeyEvent(false, SDL_SCANCODE_ESCAPE);
            }
            break;

        default: break;
        }
    }

    // Poll once per frame (covers missed pad events and keeps held states)
    pollJoystickState(this);
}

void Game::updateEntities() {
    if (player.getHP() > 0) {
        player.move();
        draw(player.getTexture(), player.getX(), player.getY()); // Draw player plane
    }
    // Player fire
    if (player.fireStatus() == true && player.getReload() == 0) {
        if (player.getAmmo() == 0) {
            player.setBulletType(normalBullet);
        }
        switch (player.getBulletType()) {
        case normalBullet:
            playerBullet.setIdentity(normalBullet);
            playerBullet.setTexture(loadTexture(normalBulletTexture));
            playerBullet.setX(player.getX() + 50);
            playerBullet.setY(player.getY() + 45);
            playerBullet.setDX(playerBulletSpeed);
            player.setReload(5);
            Entities.bullets.push_back(playerBullet);
            break;
        case waveBullet:
            playerBullet.setIdentity(waveBullet);
            playerBullet.setTexture(loadTexture(waveBulletTexture));
            playerBullet.setX(player.getX() + 65);
            playerBullet.setY(player.getY() + 40);
            playerBullet.setDX(15);
            player.setReload(4);
            player.updateAmmo(-1);
            Entities.bullets.push_back(playerBullet);
            break;
        }
        if (app.sounds[SOUND_FIRE]) Mix_PlayChannel(CH_PLAYER, app.sounds[SOUND_FIRE], 0);
    }

    // Spawn enemy
    if (enemySpawnTimer == 0) {
        enemy = new Enemy();
        enemy->setX(WIDTH - 80);
        enemy->setDX(enemySpeed);
        enemy->setTexture(loadTexture(enemyTexture));
        enemy->setHP(gameTicks / 1000 + 5);
        enemyBullet.setHP(1 + gameTicks / 2000);
        enemySpawnTimer = 60;
        srand(time(NULL));
        enemy->setIdentity((rand() % 100 < 30) ? ePlane : ePlane2);
        int y = rand() % HEIGHT;
        while (abs(y - lastY) < 200 || y + 105 > HEIGHT) {
            y = rand() % HEIGHT;
        }
        lastY = y;
        enemy->setY(y);
        Entities.fighters.push_back(enemy);
    }

    // Bullets
    for (auto i = Entities.bullets.begin(); i != Entities.bullets.end(); ) {
        if (i->getX() > WIDTH || i->getX() < 0 || i->getHP() <= 0) {
            i = Entities.bullets.erase(i);
        }
        else {
            if (i->getIdentity() == waveBullet) {
                i->setDY(15 * sin(gameTicks * 0.5 * 3.14 / 5));
            }
            i->move();
            draw(i->getTexture(), i->getX(), i->getY());
            ++i;
        }
    }

    // Fighters
    for (auto i = Entities.fighters.begin(); i != Entities.fighters.end(); ) {
        if ((*i)->getX() <= 0) {
            i = Entities.fighters.erase(i);
        }
        else if ((*i)->getHP() <= 0) {
            if (app.sounds[SOUND_EXPLOSION]) Mix_PlayChannel(CH_OTHER, app.sounds[SOUND_EXPLOSION], 0);
            addExplosion((*i)->getX(), ((*i)->getY()));
            int debrisCount = rand() % 4 + 1;
            debris.setX((*i)->getX() + 40);
            debris.setY((*i)->getY() + 40);
            for (int j = 0; j < debrisCount; j++) {
                debris.setTexture(debrisTexture[j]);
                debris.setDX((rand() % 2) % 2 ? 1 : -1);
                debris.setDY((rand() % 2) % 2 ? 1 : -1);
                Entities.debrises.push_back(debris);
            }
            srand(time(NULL));
            int drop = rand() % 100;
            if (drop < 40) {
                int type = rand() % 2;
                if (type == 0) {
                    powerUp.setIdentity(bonusHP);
                    powerUp.setTexture(loadTexture(bonusHPTexture));
                }
                else {
                    powerUp.setIdentity(enchanceATK);
                    powerUp.setTexture(loadTexture(enchanceAttackTexture));
                }
                powerUp.setX((*i)->getX());
                powerUp.setY((*i)->getY());
                powerUp.setDX(rand() % 2 == 1 ? powerUpSPD : -powerUpSPD);
                powerUp.setDY(rand() % 2 == 1 ? powerUpSPD : -powerUpSPD);
                Entities.powerUp.push_back(powerUp);
            }
            score += 5 + gameTicks / 500;
            i = Entities.fighters.erase(i);
        }
        else {
            int w, h, wP, hP;
            Enemy* p = dynamic_cast<Enemy*>(*i);
            SDL_QueryTexture((*i)->getTexture(), NULL, NULL, &w, &h);
            SDL_QueryTexture(player.getTexture(), NULL, NULL, &wP, &hP);
            if (detectCollision((*i)->getX(), (*i)->getY(), w, h, player.getX(), player.getY(), wP, hP)) {
                player.updateHP(-2);
                (*i)->updateHP(-5);
            }
            int num = rand() % 200;
            p->updateTicks();
            if (num < 5 && p->getChangeMovement() == false) {
                p->setIdentity(p->getIdentity() == ePlane ? ePlane2 : ePlane);
                p->setChangeMovement(true);
            }
            if (p->getIdentity() == ePlane2) {
                p->setDY(5 * sin(p->getTIcks() * 0.5 * 3.14 / 15));
            }
            else {
                p->setDY(0);
            }
            (*i)->move();
            if (p->getReload() == 0) {
                p->setReload((75 - gameTicks / 1000 < 30 ? 30 : 75 - gameTicks / 1000));
                enemyBullet.setX((*i)->getX() - 50);
                enemyBullet.setY((*i)->getY() + 35);
                Entities.bullets.push_back(enemyBullet);
            }
            else {
                p->setReload(p->getReload() - 1);
            }
            draw((*i)->getTexture(), (*i)->getX(), (*i)->getY());
            ++i;
        }
    }

    // Power-ups
    for (auto i = Entities.powerUp.begin(); i != Entities.powerUp.end(); ) {
        int powerUpW, powerUpH, playerW, playerH;
        SDL_QueryTexture(i->getTexture(), NULL, NULL, &powerUpW, &powerUpH);
        SDL_QueryTexture(player.getTexture(), NULL, NULL, &playerW, &playerH);
        if (detectCollision(i->getX(), i->getY(), powerUpW, powerUpH, player.getX(), player.getY(), playerW, playerH)) {
            switch (i->getIdentity()) {
            case bonusHP:     player.updateHP(2); break;
            case enchanceATK: player.updateAmmo(50); player.setBulletType(waveBullet); break;
            }
            score += 25;
            i = Entities.powerUp.erase(i);
        }
        else {
            if (i->getX() <= 0) i->setDX(powerUpSPD);
            if (i->getX() >= WIDTH - powerUpW) i->setDX(-powerUpSPD);
            if (i->getY() <= 0) i->setDY(powerUpSPD);
            if (i->getY() >= HEIGHT - powerUpH) i->setDY(-powerUpSPD);
            i->move();
            draw(i->getTexture(), i->getX(), i->getY());
            ++i;
        }
    }

    // Explosion effects
    for (auto i = Entities.effects.begin(); i != Entities.effects.end(); ) {
        bool remove = false;
        for (auto j = i->begin(); j != i->end(); ++j) {
            if (j->getA() <= 0) { remove = true; break; }
            SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_ADD);
            SDL_SetTextureBlendMode(j->getTexture(), SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(j->getTexture(), j->getR(), j->getG(), j->getB());
            SDL_SetTextureAlphaMod(j->getTexture(), j->getA());
            draw(j->getTexture(), j->getX(), j->getY());
            j->updateA(-15);
        }
        if (remove) i = Entities.effects.erase(i); else ++i;
    }

    // Debris
    for (auto i = Entities.debrises.begin(); i != Entities.debrises.end();) {
        int w1, h1;
        SDL_QueryTexture(i->getTexture(), NULL, NULL, &w1, &h1);
        if (i->getX() <= 0 || i->getX() >= WIDTH - 20 || i->getY() <= 0 || i->getY() >= HEIGHT - 20 || i->getHP() <= 0) {
            i = Entities.debrises.erase(i);
        }
        else {
            for (auto j = Entities.fighters.begin(); j != Entities.fighters.end(); ++j) {
                if ((*j)->getHP() <= 0) continue;
                int w2, h2;
                SDL_QueryTexture((*j)->getTexture(), NULL, NULL, &w2, &h2);
                if (detectCollision(i->getX(), i->getY(), w1, h1, (*j)->getX(), (*j)->getY(), w2, h2)) {
                    i->updateHP(-1);
                    (*j)->updateHP(-1);
                }
            }
            int wP, hP;
            SDL_QueryTexture(player.getTexture(), NULL, NULL, &wP, &hP);
            if (detectCollision(i->getX(), i->getY(), w1, h1, player.getX(), player.getY(), wP, hP)) {
                player.updateHP(-1);
                i->updateHP(-1);
            }
            i->move();
            draw(i->getTexture(), i->getX(), i->getY());
            ++i;
        }
    }

    // Bullet collisions
    for (auto i = Entities.bullets.begin(); i != Entities.bullets.end(); ++i) {
        int w1, h1;
        SDL_QueryTexture(i->getTexture(), NULL, NULL, &w1, &h1);
        for (auto j = Entities.fighters.begin(); j != Entities.fighters.end(); ++j) {
            if ((*j)->getHP() <= 0) continue;
            int w2, h2, wP, hP;
            SDL_QueryTexture((*j)->getTexture(), NULL, NULL, &w2, &h2);
            SDL_QueryTexture(player.getTexture(), NULL, NULL, &wP, &hP);
            if (detectCollision(i->getX(), i->getY(), w1, h1, (*j)->getX(), (*j)->getY(), w2, h2) && i->getDX() > 0) {
                i->updateHP(-1);
                (*j)->updateHP(-1);
            }
            if (detectCollision(i->getX(), i->getY(), w1, h1, player.getX(), player.getY(), wP, hP) && i->getIdentity() == eBullet) {
                player.updateHP(-1);
                i->updateHP(-1);
            }
        }
        for (auto j = Entities.debrises.begin(); j != Entities.debrises.end(); ++j) {
            if (j->getHP() <= 0) continue;
            int w2, h2;
            SDL_QueryTexture(j->getTexture(), NULL, NULL, &w2, &h2);
            if (detectCollision(i->getX(), i->getY(), w1, h1, j->getX(), j->getY(), w2, h2)) {
                i->updateHP(-1);
                j->updateHP(-1);
            }
        }
        if (player.getHP() <= 0 && !player.died()) {
            if (app.sounds[SOUND_EXPLOSION]) Mix_PlayChannel(CH_OTHER, app.sounds[SOUND_EXPLOSION], 0);
            addExplosion(player.getX(), player.getY());
            player.setDieStatus(true);
        }
    }
}

void Game::drawBackground() {
    int w, h;
    SDL_QueryTexture(app.background, nullptr, nullptr, &w, &h);
    if (--backgroundX < -w) backgroundX = 0;
    draw(app.background, backgroundX, 0, w, HEIGHT);         // scale vertically
    draw(app.background, backgroundX + w, 0, w, HEIGHT);     // tile horizontally
}

void Game::addExplosion(int x, int y) {
    explosion.setX(x - 20);
    explosion.setY(y);
    explosion.setDX(0);
    explosion.setDY(0);
    std::vector<Effect> temp;
    for (int j = 0; j < 15; j++) {
        switch (rand() % 4) {
        case 0: explosion.setRGBA(255, 255, 0, 200); break;   // YELLOW
        case 1: explosion.setRGBA(255, 0, 0, 200); break;     // RED
        case 2: explosion.setRGBA(255, 128, 0, 200); break;   // ORANGE
        case 3: explosion.setRGBA(255, 255, 255, 200); break; // WHITE
        }
        temp.push_back(explosion);
    }
    Entities.effects.push_back(temp);
}

void Game::updateHUD() {
    healthText << "Health : " << player.getHP();
    scoreText << "Score   : " << score;
    hiScoreText << "High Score : " << highScore;
    SDL_Surface* lifeSurface = TTF_RenderText_Solid(font, healthText.str().c_str(), { 255, 255, 255, 0 });
    SDL_Texture* lifeTXT = SDL_CreateTextureFromSurface(app.renderer, lifeSurface);
    SDL_Surface* scoreSurface = TTF_RenderText_Solid(font, scoreText.str().c_str(), { 255, 255, 255, 0 });
    SDL_Texture* scoreTXT = SDL_CreateTextureFromSurface(app.renderer, scoreSurface);
    SDL_Surface* hsSurface = TTF_RenderText_Solid(font, hiScoreText.str().c_str(), { 255, 255, 255, 0 });
    SDL_Texture* hsTXT = SDL_CreateTextureFromSurface(app.renderer, hsSurface);
    draw(lifeTXT, 0, 0);
    draw(scoreTXT, 0, 20);
    draw(hsTXT, 0, 40);
    SDL_FreeSurface(lifeSurface);
    SDL_FreeSurface(scoreSurface);
    SDL_FreeSurface(hsSurface);
    SDL_DestroyTexture(lifeTXT);
    SDL_DestroyTexture(scoreTXT);
    SDL_DestroyTexture(hsTXT);
}

void Game::prepareScene() {
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
    SDL_RenderClear(app.renderer);
}

void Game::updateScene() {
    if (app.running) {
        if (enemySpawnTimer > 0) {
            enemySpawnTimer--;
        }
        enemyFire++;
        if (player.getReload() > 0) {
            player.setReload(player.getReload() - 1);
        }
        gameTicks++;
        healthText.str(std::string());
        scoreText.str(std::string());
        hiScoreText.str(std::string());
    }
    if (player.getHP() <= 0 && Entities.effects.empty() && app.running) {
        Entities.bullets.clear();
        Entities.debrises.clear();
        Entities.fighters.clear();
        Entities.powerUp.clear();
        app.running = false;
        if (score > highScore) {
            highScore = score;
            file.close();
            file.open("scores.txt", ios::out);
            file << highScore;
        }
        endScreen();
        score = 0;
    }
    SDL_RenderPresent(app.renderer);
    //SDL_Delay(40);
}

SDL_Texture* Game::loadTexture(std::string path) {
    SDL_Log("IMG load start: %s", path.c_str());

    // Choose loader based on extension to avoid PNG/JPG cross-probing noise.
    auto toLower = [](std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
    std::string p = toLower(path);

    SDL_Surface* s = nullptr;
    if (p.size() >= 4 && p.rfind(".png") == p.size() - 4) {
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) { SDL_Log("RWFromFile failed for %s: %s", path.c_str(), SDL_GetError()); SDL_Quit(); }
        s = IMG_LoadTyped_RW(rw, 1, "PNG");
    }
    else if ((p.size() >= 4 && p.rfind(".jpg") == p.size() - 4) ||
        (p.size() >= 5 && p.rfind(".jpeg") == p.size() - 5)) {
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) { SDL_Log("RWFromFile failed for %s: %s", path.c_str(), SDL_GetError()); SDL_Quit(); }
        s = IMG_LoadTyped_RW(rw, 1, "JPG");
    }
    else {
        // Fallback: let SDL_image sniff the type.
        s = IMG_Load(path.c_str());
    }

    if (!s) {
        SDL_Log("IMG load FAILED: %s  -> %s", path.c_str(), IMG_GetError());
        SDL_Quit();
    }

    // Pitch guard / scaling
    const int bpp = s->format->BitsPerPixel ? s->format->BitsPerPixel : 32;
    int maxPitch = 8128;
    int bytesPerPixel = (bpp + 7) / 8;
    if (s->pitch > maxPitch || (s->w * bytesPerPixel) > maxPitch) {
        int targetW = std::min(s->w, 2030);
        int targetH = (int)((double)s->h * (double)targetW / (double)s->w + 0.5);
        SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, targetW, targetH, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Rect dstR{ 0,0,targetW,targetH };
        SDL_BlitScaled(s, nullptr, dst, &dstR);
        SDL_FreeSurface(s);
        s = dst;
        SDL_Log("Scaled '%s' to %dx%d to avoid CopyRects limit.", path.c_str(), s->w, s->h);
    }

    SDL_Texture* t = SDL_CreateTextureFromSurface(app.renderer, s);
    if (!t) {
        SDL_Log("CreateTexture FAILED for %s: %s", path.c_str(), SDL_GetError());
        SDL_FreeSurface(s);
        SDL_Quit();
    }
    SDL_FreeSurface(s);

    int w = 0, h = 0; Uint32 fmt = 0; int access = 0;
    SDL_QueryTexture(t, &fmt, &access, &w, &h);
    SDL_Log("IMG load OK: %s  (%dx%d fmt=0x%x)", path.c_str(), w, h, fmt);
    return t;
}

void Game::draw(SDL_Texture* texture, int x, int y, int w, int h) {
    SDL_Rect dst{ x, y, 0, 0 };
    if (w < 0 || h < 0) SDL_QueryTexture(texture, nullptr, nullptr, &dst.w, &dst.h);
    if (w >= 0) dst.w = w;
    if (h >= 0) dst.h = h;
    SDL_RenderCopy(app.renderer, texture, nullptr, &dst);
}

bool Game::detectCollision(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
    return (max(x1, x2) < min(x1 + w1, x2 + w2)) && (max(y1, y2) < min(y1 + h1, y2 + h2));
}
