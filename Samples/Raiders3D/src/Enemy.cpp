#include "Enemy.hpp"
#include "CrashProbe.h"
PROBE_THIS_TU

Enemy::Enemy(){
	reload = changeMovement = ticks = 0;
}

void Enemy::setReload(int reload){
	this->reload = reload;
}

void Enemy::setChangeMovement(bool changed){
	this->changeMovement = changed;
}

void Enemy::updateTicks(){
	this->ticks++;
}

int Enemy::getReload(){
	return reload;
}

bool Enemy::getChangeMovement(){
	return changeMovement;
}

int Enemy::getTIcks(){
	return ticks;
}