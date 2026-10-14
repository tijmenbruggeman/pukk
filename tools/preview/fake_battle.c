/* Fake battle engine for the host preview: the scenes in preview.c send the
 * events themselves; this only logs what the UI asks for. */
#include <stdio.h>
#include "battle.h"

void battle_host(const char *pack_name)
{
    printf("preview: battle_host(%s)\n", pack_name);
}

void battle_join(void)
{
    printf("preview: battle_join()\n");
}

void battle_start(void)
{
    printf("preview: battle_start()\n");
}

void battle_answer(uint8_t choice)
{
    printf("preview: battle_answer(%u)\n", choice);
}

void battle_leave(void)
{
    printf("preview: battle_leave()\n");
}
