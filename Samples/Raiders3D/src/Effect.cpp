#include "Effect.hpp"
#include "CrashProbe.h"
PROBE_THIS_TU

void Effect::setRGBA(int r, int g, int b, int a){
	this->r = r;
	this->g = g;
	this->b = b;
	this->a = a;
}

int Effect::getR(){
	return r;
}

int Effect::getG(){
	return g;
}

int Effect::getB(){
	return b;
}

int Effect::getA(){
	return a;
}

void Effect::updateA(int a){
	this->a += a;
}