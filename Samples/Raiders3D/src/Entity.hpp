#pragma once
#include <SDL.h>

class Entity{
	protected:
		int x, y, dx, dy;
		int HP;
		int identity;
		SDL_Texture *texture;
	public:
		Entity();
		void move();
		void setHP(int);
		void setX(int);
		void setY(int);
		void setDX(int);
		void setDY(int);
		void updateHP(int);
		void setIdentity(int identity);
		void setTexture(SDL_Texture*);
		int getX();
		int getY();
		int getDX();
		int getDY();
		int getHP();
		int getIdentity();
		SDL_Texture *getTexture();
		virtual ~Entity();
};