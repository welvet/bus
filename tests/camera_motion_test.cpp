#include <cassert>
#include <iostream>
#include "../firmware/camera/motion.h"
using BusProtocol::Command;
int main() {
  CameraMotion::State state;
  state.reset(123);
  assert(state.tick(1,true).command == Command::Stop);
  state.mode(false, 10);
  assert(state.accept(123,1,0,"drive",0,10,500,10));
  assert(!state.accept(123,1,0,"auto",0,0,0,10));
  assert(state.tick(10,true).command == Command::Auto);
  assert(state.tick(20,true).command == Command::Stop); // No implicit repeated activation.
  state.mode(true, 30);
  uint32_t epoch = state.epoch;
  assert(!state.accept(123,2,epoch,"drive",10,20,500,30));
  assert(state.tick(30,true).speed == 0); // Neutral arming before the first motion.
  assert(state.tick(50,true).speed == -20 && state.tick(50,true).angle == 10);
  assert(state.accept(123,2,epoch,"drive",10,20,500,400));
  assert(state.accept(123,1,epoch,"drive",10,20,500,400));
  assert(state.startedAt == 30); // Old/duplicate packets cannot extend expiry.
  assert(state.tick(529,true).command == Command::Drive);
  assert(state.tick(530,true).command == Command::Stop);
  assert(!state.accept(123,3,epoch,"drive",-45,-100,100,600));
  assert(state.tick(600,true).speed == 0);
  assert(state.tick(620,true).speed == 100);
  assert(!state.accept(123,4,epoch,"drive",20,30,100,630));
  assert(state.tick(630,true).speed == -30); // Immediate replacement, no extra neutral during motion.
  state.mode(false,640);
  assert(state.tick(640,true).command == Command::Stop && !state.active);
  assert(state.accept(123,5,epoch,"drive",0,50,100,650));
  state.mode(true,660);
  assert(state.epoch != epoch);
  assert(state.accept(123,5,epoch,"drive",0,50,100,660)); // Old auto epoch cannot replay.
  assert(!state.accept(123,5,state.epoch,"drive",0,50,100,660));
  assert(state.tick(660,false).command == Command::Stop);
  state.mode(true,700);
  assert(state.tick(1700,true).command == Command::Stop); // Stale Renesas feedback.
  state.reset(456);
  assert(state.accept(123,6,state.epoch,"stop",0,0,0,1800));
  assert(!state.accept(456,0,state.epoch,"stop",0,0,0,1800));
  state.mode(true,UINT32_MAX - 50);
  assert(!state.accept(456,1,state.epoch,"drive",0,20,100,UINT32_MAX - 50));
  state.tick(UINT32_MAX - 50,true);
  assert(state.tick(48,true).command == Command::Drive);
  assert(state.tick(49,true).command == Command::Stop);
  assert(CameraMotion::newer(0,UINT32_MAX) && !CameraMotion::newer(UINT32_MAX,0));
  assert(!CameraMotion::newer(0x80000000u,0));
  state.mode(true,100);
  assert(state.accept(456,2,state.epoch,"drive",46,0,100,100));
  assert(state.accept(456,2,state.epoch,"drive",0,0,2001,100));
  assert(state.accept(456,2,state.epoch,"drive",0,0,0,100));
  assert(state.accept(456,2,state.epoch,"dance",0,0,100,100));
  std::cout << "Camera timed replacement, counters/session/epoch, arming, disconnect, expiry and rollover passed\n";
}
