#include "Entity.hpp"
#include "CrashProbe.h"
PROBE_THIS_TU

Entity::Entity(){
	x = y = dx = dy = HP = identity = 0;
	texture = NULL;
}

void Entity::move(){
	this->x += dx;
	this->y += dy;
}

void Entity::setHP(int HP){
	this->HP = HP;
}

void Entity::setIdentity(int identity){
	this->identity = identity;
}

void Entity::setX(int x){
	this->x = x;
}

void Entity::setY(int y){
	this->y = y;
}

void Entity::setDX(int dx){
	this->dx = dx;
}

void Entity::setDY(int dy){
	this->dy = dy;
}

void Entity::updateHP(int HP){
	this->HP += HP;
}

void Entity::setTexture(SDL_Texture *texture){
	this->texture = texture;
}

int Entity::getX(){
	return x;
}

int Entity::getY(){
	return y;
}

int Entity::getHP(){
	return HP;
}

int Entity::getDX(){
	return dx;
}

int Entity::getDY(){
	return dy;
}

int Entity::getIdentity(){
	return identity;
}

SDL_Texture* Entity::getTexture(){
	return texture;
}

Entity::~Entity(){

}