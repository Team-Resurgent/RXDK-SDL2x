#pragma once
#include <SDL.h>
#include "Entity.hpp"

class Player:public Entity{
	private:
		int speed, reload;
		bool up, down, left, right, fire;
		bool bulletType;
		bool enterGame;
		bool die;
		int ammo;
	public:
		Player();
		void move(); //Overriding superclass move function
		void keyDown(SDL_KeyboardEvent*);
		void keyUp(SDL_KeyboardEvent*);
		void updateAmmo(int);
		void setReload(int);
		void setBulletType(int);
		void setEnterStatus(bool);
		void setDieStatus(bool);
		int getAmmo();
		int getReload();
		int getBulletType();
		bool fireStatus();
		bool enterStatus();
		bool died();
		void resetInput();
};