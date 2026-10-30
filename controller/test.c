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
    assert(triggerSpeed(0.3,0.8)==69 && triggerSpeed(0.8,0.3)==-69);
    assert(triggerSpeed(1,1)==0 && triggerSpeed(0.5,0.5)==0);
    assert(triggerSpeed(0,1)==100 && triggerSpeed(1,0)==-100);
    assert(triggerSpeed(0.01,0.02)==0);
    assert(triggerSpeed(-1,2)==100);
    assert(triggerSpeed(0,0.02001)==15 && triggerSpeed(0.02001,0)==-15);
    assert(triggerSpeed(0,0.51)==36 && triggerSpeed(0.51,0)==-36);
    int previousSpeed = 0;
    for (int step = 0; step <= 1000; step++) {
        double pressure = step / 1000.0;
        int speed = triggerSpeed(0,pressure);
        assert(speed >= previousSpeed && speed <= 100);
        assert(triggerSpeed(pressure,0) == -speed);
        assert(pressure <= 0.02 ? speed == 0 : speed >= 15);
        previousSpeed = speed;
    }
    assert(trimmedAngle(0,0,false)==0);
    assert(trimmedAngle(90,0,false)==35);
    assert(trimmedAngle(-90,0,false)==-71);
    assert(trimmedAngle(0,-30,false)==-30);
    assert(trimmedAngle(30,-30,true)==-30);
    assert(trimmedAngle(90,-30,false)==5);
    assert(trimmedAngle(-90,-30,false)==-90);
    assert(trimmedAngle(45,-30,false)==-21);
    assert(trimmedAngle(-45,-30,false)==-45);
    assert(trimmedAngle(0,10,false)==10);
    assert(trimmedAngle(-90,10,false)==-61);
    assert(trimmedAngle(90,10,false)==45);
    assert(trimmedAngle(45,10,false)==19);
    assert(trimmedAngle(-45,10,false)==-8);
    for (int trim = -90; trim <= 90; trim++) {
        int minimum = trim - 71 < -90 ? -90 : trim - 71;
        int maximum = trim + 35 > 90 ? 90 : trim + 35;
        int previous = minimum;
        for (int stick = -90; stick <= 90; stick++) {
            int angle = trimmedAngle(stick,trim,false);
            assert(angle >= minimum && angle <= maximum && angle >= previous);
            previous = angle;
        }
        assert(trimmedAngle(-90,trim,false)==minimum);
        assert(trimmedAngle(90,trim,false)==maximum);
    }
    uint8_t bytes[3]; commandBytes(bytes,-90,-100);
    assert(bytes[0]==1 && bytes[1]==166 && bytes[2]==100);
    commandBytes(bytes,90,100); assert(bytes[1]==90 && bytes[2]==156);
    commandBytes(bytes,0,0); assert(bytes[2]==0);
    commandBytes(bytes,0,triggerSpeed(0.3,0.8)); assert((int8_t)bytes[2]==-69);
    commandBytes(bytes,0,triggerSpeed(0.8,0.3)); assert((int8_t)bytes[2]==69);
    state.calibrateDown = 0; steeringCalibrationRequested = 0;
    calibrationButton(1); assert(steeringCalibrationRequested == 1);
    steeringCalibrationRequested = 0; calibrationButton(1); assert(steeringCalibrationRequested == 0);
    calibrationButton(0); calibrationButton(1); assert(steeringCalibrationRequested == 1);
    steeringCalibrationRequested = 0; calibrationButton(0);
    state.resetDown = 0; steeringResetRequested = 0;
    resetButton(1); assert(steeringResetRequested == 1);
    steeringResetRequested = 0; resetButton(1); assert(steeringResetRequested == 0);
    resetButton(0); resetButton(1); assert(steeringResetRequested == 1);
    steeringResetRequested = 0; resetButton(0);
    assert(trimmedAngle(30,0,true)==0);
    double value;
    assert(parseNumber("10s",true,&value) && value==10);
    assert(parseNumber("20ms",true,&value) && value==0.02);
    assert(!parseNumber("NaN",false,&value) && !parseNumber("3x",true,&value));
    puts("C steering, trigger winner, packet encoding and argument validation passed");
}
