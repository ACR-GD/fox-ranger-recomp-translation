/* cheat.h - Konami cheat code for Fox Ranger: infinite shield units + menu chime */
#ifndef FOX_CHEAT_H
#define FOX_CHEAT_H

#include <stdint.h>
#include <SDL.h>
#include "cpu.h"

void cheat_init(void);
void cheat_on_key(SDL_Scancode s);
void cheat_poll(Cpu *c);
int  cheat_call_hook(Cpu *c, int is_far, uint16_t seg_img, uint16_t off);
int  cheat_is_infinite_shield(void);
void cheat_set_infinite_shield(int enabled);
void cheat_set_in_menu(int in_menu);
void cheat_play_sound(void);

#endif
