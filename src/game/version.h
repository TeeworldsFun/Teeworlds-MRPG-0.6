/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_VERSION_H
#define GAME_VERSION_H
#ifndef GAME_RELEASE_VERSION
#define GAME_RELEASE_VERSION "19.2"
#endif

// teeworlds
#define GAME_VERSION "0.6.4, " GAME_RELEASE_VERSION
#define GAME_NETVERSION "0.6 626fce9a778df4d4"
#define GAME_NETVERSION7 "0.7 802f1be60a05665f"

// ddnet
#define DDNET_VERSION_NUMBER 19020
extern const char* GIT_SHORTREV_HASH;

// mrpg protocol (CLIENT/SERVER SIDE) VERSION
#define MRPG_PROJECT_VERSION "2.33.1"
#define MRPG_PROTOCOL_VERSION 2000

#endif