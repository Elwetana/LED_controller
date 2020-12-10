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

void Stencil_init();
void Stencil_check_movement();


#endif /* __STENCIL_HANDLER_H__ */
