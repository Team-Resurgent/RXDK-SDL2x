#pragma once

/*	
	-> Gameplay speed = 25 loop ticks/sec
*/

#define WIDTH 1280
#define HEIGHT 720

#define playerTexture "D:\\resources\\sprites\\playerShip.png"
#define playerHP 10
#define playerBulletSpeed 30

#define enemyTexture "D:\\resources\\sprites\\enemyShip.png"
#define enemyHP 5
#define enemySpeed -5
#define enemyBulletSpeed -15

#define bulletHP 1

#define normalBulletTexture "D:\\resources\\sprites\\blueLaser.png"
#define waveBulletTexture "D:\\resources\\sprites\\waveBullet.png"
#define enemyBulletTexture "D:\\resources\\sprites\\redLaser.png"


#define powerUpSPD 6
#define bonusHPTexture "D:\\resources\\sprites\\bonusHP.png"
#define enchanceAttackTexture "D:\\resources\\sprites\\enchanceATK.png"

#define explosionTexture "D:\\resources\\sprites\\explosion.png"

#define titleScreenTexture "D:\\resources\\sprites\\titleScreen.png"
#define endScreenTexture "D:\\resources\\sprites\\endScreen.png"
#define icon "D:\\resources\\sprites\\icon.png"
#define backgroundTexture "D:\\resources\\sprites\\background.jpg"//background.jpg for shorter background

#define soundChannel 5
#define fireSound "D:\\resources\\sound\\laser.wav"
#define explosionSound "D:\\resources\\sound\\explosion.wav"
#define bgSound "D:\\resources\\sound\\background.wav"
#define btSound "D:\\resources\\sound\\button.wav"

enum{
	pPlane,
	ePlane,
	ePlane2,
	eBullet,
	shipDebris,
	bonusHP,
	enchanceATK,
};

enum{
	normalBullet,
	waveBullet
};

enum{
	CH_MUSIC,
	CH_MENU,
	CH_PLAYER,
	CH_ENEMY,
	CH_OTHER
};

enum{
	SOUND_FIRE,
	SOUND_EXPLOSION,
	SOUND_BUTTON
};