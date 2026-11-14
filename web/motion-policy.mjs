// All browser/CV response shaping lives here, never in a client.
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const round=v=>Math.sign(v)*Math.round(Math.abs(v));
export function desiredMotion(steering,throttle) {
  const axis=2*Math.round((clamp(steering,-1,1)+1)*65535/2)/65535-1;
  const stick=Math.abs(axis)<=.12?0:round(Math.sign(axis)*(Math.abs(axis)-.12)/.88*90);
  const position=stick/90;
  const angle=-round(position*Math.abs(position)*45)||0; // Installed servo orientation.
  const trigger=Math.round(Math.abs(clamp(throttle,-1,1))*1023)/1023;
  const speed=trigger<=.02?0:Math.sign(throttle)*(30+round(((trigger-.02)/.98)**2*70));
  return {angle,speed};
}
export class MotionPolicy {
  constructor({riseMs=1000}={}) {
    if(!Number.isFinite(riseMs)||riseMs<=0)throw Error('riseMs must be positive');
    this.riseMs=riseMs;this.reset(0);
  }
  reset(now) {this.desired=0;this.allowed=0;this.real=0;this.angle=0;this.at=now;}
  set(steering,throttle,now) {
    // Account for elapsed time under the OLD target before changing it.
    this.step(now);
    const target=desiredMotion(steering,throttle);
    if(Math.sign(target.speed)!==Math.sign(this.desired) || Math.abs(target.speed)<Math.abs(this.real)) {
      // Reversal starts from zero; reductions immediately lower the ceiling.
      this.allowed=Math.sign(target.speed)!==Math.sign(this.desired)?0:Math.abs(target.speed);
      this.real=Math.sign(target.speed)*Math.min(Math.abs(target.speed),this.allowed)||0;
    }
    this.desired=target.speed;this.angle=target.angle;this.at=now;
  }
  step(now) {
    // No catch-up burst after an event-loop stall; device commands expire in 200 ms.
    const dt=Math.max(0,now-this.at);
    if(dt>200)this.allowed=0;
    else this.allowed=Math.min(Math.abs(this.desired),this.allowed+100*dt/this.riseMs);
    this.real=Math.sign(this.desired)*Math.min(Math.abs(this.desired),this.allowed)||0;
    this.at=now;
    return {angle:this.angle,speed:Math.trunc(this.real)||0};
  }
  status(){return {desired_speed:this.desired,allowed_speed:Number(this.allowed.toFixed(2)),real_speed:Number(this.real.toFixed(2)),angle:this.angle,rise_ms:this.riseMs,min_speed:30,max_angle:45};}
}
