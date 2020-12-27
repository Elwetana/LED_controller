#ifndef __STENCIL_HANDLER_H__
#define __STENCIL_HANDLER_H__

enum StencilFlags
{
    SF_Background,
    SF_Player,
    SF_PlayerProjectile,
    SF_Enemy,
    SF_EnemyProjectile,
    SF_N_FLAGS
};


void Stencil_init(enum GameModes current_mode);
void Stencil_stencil_test(int object_index, int stencil_flag);

#endif /* __STENCIL_HANDLER_H__ */
