#include "Player.hpp"
#include "defs.hpp"
#include "CrashProbe.h"
PROBE_THIS_TU

Player::Player(){
	up = left = down = right = fire = die = false;
	reload = 0;
	speed = 10;
	bulletType = ammo = enterGame = 0;
}

void Player::move() {
	// Resolve intended velocity from input
	if (up && left) { dx = -speed; dy = -speed; }
	else if (up && right) { dx = speed; dy = -speed; }
	else if (down && left) { dx = -speed; dy = speed; }
	else if (down && right) { dx = speed; dy = speed; }
	else if (up) { dy = -speed; }
	else if (down) { dy = speed; }
	else if (left) { dx = -speed; }
	else if (right) { dx = speed; }

	// Query current texture size so we clamp against the visible sprite
	int w = 0, h = 0;
	if (texture) { // assuming Player has SDL_Texture* texture; if not, swap for getTexture()
		SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
	}
	else {
		// reasonable fallbacks if texture not set yet
		w = 64; h = 64;
	}

	// Clamp within the window bounds (0..WIDTH-w, 0..HEIGHT-h)
	x = (x + dx < 0 ? 0 : (x + dx > WIDTH - w ? WIDTH - w : x + dx));
	y = (y + dy < 0 ? 0 : (y + dy > HEIGHT - h ? HEIGHT - h : y + dy));

	// consume this frame's velocity
	dx = dy = 0;
}


void Player::keyDown(SDL_KeyboardEvent *event){
	if(!(event->repeat)){
		switch(event->keysym.scancode){
		case SDL_SCANCODE_W:
		case SDL_SCANCODE_UP:
			up = true;
			break;
		case SDL_SCANCODE_S:
		case SDL_SCANCODE_DOWN:
			down = true;
			break;
		case SDL_SCANCODE_A:
		case SDL_SCANCODE_LEFT:
			left = true;
			break;
		case SDL_SCANCODE_D:
		case SDL_SCANCODE_RIGHT:
			right = true;
			break;
		case SDL_SCANCODE_LSHIFT:
			fire = true;
			break;
		}
	}
}

void Player::keyUp(SDL_KeyboardEvent *event){
	if(!(event->repeat)){
		switch(event->keysym.scancode){
		case SDL_SCANCODE_W:
		case SDL_SCANCODE_UP:
			up = false;
			break;
		case SDL_SCANCODE_S:
		case SDL_SCANCODE_DOWN:
			down = false;
			break;
		case SDL_SCANCODE_A:
		case SDL_SCANCODE_LEFT:
			left = false;
			break;
		case SDL_SCANCODE_D:
		case SDL_SCANCODE_RIGHT:
			right = false;
			break;
		case SDL_SCANCODE_LSHIFT:
			fire = false;
			break;
		}
	}
}

void Player::updateAmmo(int ammo){
	this->ammo += ammo;
}

void Player::setReload(int reload){
	this->reload = reload;
}

void Player::setBulletType(int type){
	this->bulletType = type;
}

void Player::setEnterStatus(bool status){
	this->enterGame = status;
}

void Player::setDieStatus(bool status){
	this->die = status;
}

int Player::getAmmo(){
	return ammo;
}

int Player::getReload(){
	return reload;
}

int Player::getBulletType(){
	return bulletType;
}

bool Player::fireStatus(){
	return fire;
}

bool Player::enterStatus(){
	return enterGame;
}

bool Player::died(){
	return die;
}

void Player::resetInput(){
	left = up = right = down = fire = false;
}