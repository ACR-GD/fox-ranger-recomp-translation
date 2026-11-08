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

/* Headless demo recorder: renders an animated interactive walkthrough of the
 * 640x480 Mission Control launcher into PPM frames at `frames_dir`. */
int launcher_record_demo(const char *frames_dir, const char *game_dir);

#ifdef __cplusplus
}
#endif

#endif /* FOX_LAUNCHER_H */
