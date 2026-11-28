#pragma once
// Host stub: the critical sections in history_comfort.c guard data shared with the httpd task,
// and the host tests are single-threaded.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m)  ((void)(m))
