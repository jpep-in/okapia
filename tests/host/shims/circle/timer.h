// The compositor times itself with this and nothing else. A counter that only
// goes up is all the arithmetic needs; the numbers are meaningless here, and
// the tests never read them.
#ifndef _circle_timer_h
#define _circle_timer_h
#include <circle/types.h>
class CTimer
{
public:
    static unsigned GetClockTicks (void) { static unsigned n; return ++n; }
};
#endif
