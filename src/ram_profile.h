#ifndef LAUNCHER_RAM_PROFILE_H
#define LAUNCHER_RAM_PROFILE_H

#ifdef ENABLE_RAM_LOGGING
void ramProfileLog(const char *tag);
#define RAM_LOG(tag) ramProfileLog(tag)
#else
#define RAM_LOG(tag) \
    do {             \
    } while (0)
#endif

#endif
