// Upright truncated 40-degree cone: full steering at zero or full speed.
export const cone = { width:220, height:320, bottom:260, top:40, center:110, half:24, radians:Math.PI/9 };
export function coneInput(px, py) {
  const vertical=Math.max(cone.top,Math.min(cone.bottom,py));
  const travel=cone.bottom-vertical;
  const radius=cone.half/Math.tan(cone.radians)+travel;
  const steering=Math.max(-1,Math.min(1,Math.atan2(cone.center-px,radius)/cone.radians));
  return {steering,throttle:travel/(cone.bottom-cone.top)};
}
export function conePosition(steering,throttle) {
  const travel=throttle*(cone.bottom-cone.top);
  const radius=cone.half/Math.tan(cone.radians)+travel;
  return {x:cone.center-Math.tan(steering*cone.radians)*radius,y:cone.bottom-travel};
}
