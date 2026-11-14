import test from 'node:test';
import assert from 'node:assert/strict';
import {coneInput,conePosition} from './public/cone.js';
test('cone separates full steering from speed, including zero speed',()=>{
 for(const throttle of [0,.01,.5,1]) for(const steering of [-1,-.5,0,.5,1]) {
  const p=conePosition(steering,throttle),input=coneInput(p.x,p.y);
  assert.ok(Math.abs(input.steering-steering)<1e-10);
  assert.ok(Math.abs(input.throttle-throttle)<1e-10);
 }
});

test('upright cone: upward increases speed, lateral steering available at both ends',()=>{
 assert.equal(coneInput(110,260).throttle,0);
 assert.equal(coneInput(110,40).throttle,1);
 assert.ok(coneInput(6,40).steering>0);
 assert.ok(coneInput(214,40).steering<0);
 assert.ok(coneInput(86,260).steering>.99);
 assert.ok(coneInput(134,260).steering<-.99);
});
