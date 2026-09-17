/* host_stubs.c -- link-only fallbacks for non-AArch64 loader smoke builds */
#if !defined(__aarch64__)
#define WEAK __attribute__((weak))

WEAK void MobileMenu__Update_orig(void *self) { (void)self; }
WEAK void FindWeaponLockOnTarget_orig(void *self) { (void)self; }
WEAK void MainMenuScreen__AddAllItems_orig(void *self) { (void)self; }
WEAK void CAutomobile__Render_orig(void *self) { (void)self; }

WEAK void hyd_freeze_stub(void) {}
WEAK void aim_lr_stub(void) {}
WEAK void aim_ud_stub(void) {}
WEAK void free_aim_stub(void) {}
WEAK void CCam__FollowCar_ymov_stub(void) {}
WEAK void CPlane__nozzle_stub(void) {}
WEAK void CPlane__rudder_turret_stub(void) {}
WEAK void CHud__DrawRadar_stub(void) {}
WEAK void CCamera__Process_fov_stub(void) {}
WEAK void CCoronas__Render_ps2corona_stub(void) {}
WEAK void CPostEffects__MobileRender_ps2filter_stub(void) {}
#endif
