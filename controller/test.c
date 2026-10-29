#define main controller_main
#include "main.c"
#undef main
#include <assert.h>
int main(void) {
    assert(steeringAngle(0.1,0.12)==0);
    assert(steeringAngle(1,0.12)==90 && steeringAngle(-1,0.12)==-90);
    assert(steeringAngle(0.56,0.12)==45 && steeringAngle(-0.56,0.12)==-45);
    assert(steeringAngle(2,0.12)==90);
    assert(triggerSpeed(0,0)==0);
    assert(triggerSpeed(0.3,0.8)==80 && triggerSpeed(0.8,0.3)==-80);
    assert(triggerSpeed(1,1)==0 && triggerSpeed(0.5,0.5)==0);
    assert(triggerSpeed(0,1)==100 && triggerSpeed(1,0)==-100);
    assert(triggerSpeed(0.01,0.02)==0);
    assert(triggerSpeed(-1,2)==100);
    uint8_t bytes[3]; commandBytes(bytes,-90,-100);
    assert(bytes[0]==1 && bytes[1]==166 && bytes[2]==156);
    commandBytes(bytes,90,100); assert(bytes[1]==90 && bytes[2]==100);
    double value;
    assert(parseNumber("10s",true,&value) && value==10);
    assert(parseNumber("20ms",true,&value) && value==0.02);
    assert(!parseNumber("NaN",false,&value) && !parseNumber("3x",true,&value));
    puts("C steering, trigger winner, packet encoding and argument validation passed");
}
