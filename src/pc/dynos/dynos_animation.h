#ifndef DYNOS_ANIMATION_H
#define DYNOS_ANIMATION_H
#include "types.h"
/* Sample the replacement on the original actor clock, without changing AI,
 * animation completion tests, sounds or the object's native animation pointer. */
static inline s16 dynos_animation_frame(const struct Animation *source,
                                       const struct Animation *target, s16 frame) {
    if(frame<0)return 0;
    if(frame>=source->loopEnd)frame=source->loopEnd-1;
    int start=frame<source->loopStart?0:source->loopStart;
    int targetStart=frame<source->loopStart?0:target->loopStart;
    int end=frame<source->loopStart?source->loopStart:source->loopEnd;
    int targetEnd=frame<source->loopStart?target->loopStart:target->loopEnd;
    int span=end-start-1,targetSpan=targetEnd-targetStart-1;
    return (s16)(targetStart+(span>0&&targetSpan>0?(frame-start)*targetSpan/span:0));
}
#endif
