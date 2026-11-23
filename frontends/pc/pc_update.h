/* New releases: the server says which is the newest (/api/version) and, where this build can replace itself (an AppImage,
 * the Windows exe, the macOS binary), the release's file is downloaded with curl and put in its place. */
#ifndef PC_UPDATE_H
#define PC_UPDATE_H

#include <stddef.h>

/* 1 when this program can install a release over itself. */
int update_can_install(void);
/* Downloads the release named by the /api/version answer and puts it in place of this program. Blocks for the whole
 * download, so it runs on a thread. 0 when done (then update_restart), else the reason in err. */
int update_install(const char *version_json, char *err, size_t cap);
/* Starts the installed release with the same arguments and ends this one. */
void update_restart(char **argv);
/* Removes what an update on Windows left behind (the old exe can't be deleted while it runs). */
void update_cleanup(void);

#endif
