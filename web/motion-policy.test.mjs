import test from 'node:test';
import assert from 'node:assert/strict';
import {MotionPolicy,desiredMotion} from './motion-policy.mjs';
function advance(p,to){for(let t=p.at+50;t<to;t+=50)p.step(t);return p.step(to);}
test('backend maps raw intent: deadzones, 30% minimum, quadratic response and steering limit',()=>{
 assert.deepEqual(desiredMotion(0,0),{angle:0,speed:0});
 assert.equal(desiredMotion(.1,.019).speed,0);assert.equal(desiredMotion(.1,.019).angle,0);
 assert.equal(desiredMotion(0,.03).speed,30);assert.equal(desiredMotion(0,.5).speed,47);
 assert.equal(desiredMotion(.56,0).angle,-11);
 assert.deepEqual(desiredMotion(1,1),{angle:-45,speed:100});assert.deepEqual(desiredMotion(-1,-1),{angle:45,speed:-100});
});
test('ramp begins at zero, ignores minimum floor, 50 at half second and 100 at one second',()=>{
 const p=new MotionPolicy();p.set(0,1,0);assert.equal(p.step(0).speed,0);
 assert.equal(advance(p,100).speed,10);assert.equal(advance(p,500).speed,50);assert.equal(advance(p,1000).speed,100);
});
test('updated desired speed caps real output and future increases restart at the lower output',()=>{
 const p=new MotionPolicy();p.set(0,1,0);advance(p,600);assert.equal(p.real,60);
 p.set(0,.03,600);assert.equal(p.desired,30);assert.equal(p.real,30);assert.equal(p.allowed,30);
 p.set(0,1,600);assert.equal(p.step(600).speed,30);assert.equal(advance(p,700).speed,40);
 p.set(0,0,700);assert.equal(p.real,0);assert.equal(p.allowed,0);
 p.set(0,1,700);assert.equal(advance(p,800).speed,10);
});
test('low held target cannot bank unused acceleration credit; reversal restarts at zero',()=>{
 const p=new MotionPolicy();p.set(0,.03,0);advance(p,2000);assert.equal(p.real,30);assert.equal(p.allowed,30);
 p.set(0,1,2000);assert.equal(advance(p,2100).speed,40);
 p.set(0,-1,2100);assert.equal(p.real,0);assert.equal(advance(p,2200).speed,-10);
});
test('steering never ramps; long scheduler stalls discard acceleration credit',()=>{
 const p=new MotionPolicy();p.set(1,1,0);assert.equal(p.step(0).angle,-45);
 advance(p,500);assert.equal(p.step(1000).speed,0);
 assert.equal(p.step(1050).speed,5);
});

test('desired target can change during a ramp without restarting it or jumping to the minimum',()=>{
 const p=new MotionPolicy();p.set(0,1,0);advance(p,200);assert.equal(p.real,20);
 p.set(0,.5,200);assert.equal(p.desired,47);assert.equal(p.real,20);
 assert.equal(advance(p,400).speed,40);assert.equal(advance(p,700).speed,47);
 p.set(0,1,700);assert.equal(advance(p,800).speed,57);
});
