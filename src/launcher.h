#ifndef FOX_LAUNCHER_H
#define FOX_LAUNCHER_H

#include "launcher_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LAUNCHER_START_GAME = 1,
    LAUNCHER_QUIT = 0,
    LAUNCHER_ERROR = -1
} LauncherResult;

/* Runs the 1992 Fox Ranger themed launcher window.
 * Updates cfg with user selections.
 * Returns LAUNCHER_START_GAME when user launches the mission, or LAUNCHER_QUIT. */
LauncherResult launcher_run(LauncherConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* FOX_LAUNCHER_H */
