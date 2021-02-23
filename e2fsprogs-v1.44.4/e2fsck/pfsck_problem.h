
/* Disable pfsck_fix from being called once
 *
 */
void pfsck_disable_single_fix(e2fsck_t ctx);

/* Synchronization routine before a fix is carried out
 *
 */
void pfsck_pre_fix(e2fsck_t ctx);

/* Sychronization routine after a fix is carried out
 *
 */
void pfsck_post_fix(e2fsck_t ctx);

/* Synchronization routine called throughout e2fsck.
 * Checks if there are any fixes to carry out
 */
int pfsck_sync_fix(e2fsck_t ctx);
