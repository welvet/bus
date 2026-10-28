#include "../firmware/bus/control.h"
#include <assert.h>
#include <iostream>
int main() {
  Control state;
  assert(state.speed == 0 && state.angle == -1);
  state.motor(80, 100);
  assert(!state.expire(1599)); assert(state.expire(1600)); assert(state.speed == 0);
  for (uint32_t now = 2000; now < 9000; now += 250) { state.motor(-100, now); assert(!state.expire(now + 249)); }
  state.motor(100, UINT32_MAX - 100); assert(!state.expire(100)); assert(state.expire(1500));
  int value;
  assert(integer(" -100\n", -100, 100, value) && value == -100);
  for (const char *bad : {"", "true", "1.5", "101", "-101", "01", "+1", "12x", "9999999999999999"})
    assert(!integer(bad, -100, 100, value));
  assert(integer("180", 0, 180, value)); assert(!integer("181", 0, 180, value));
  Request request;
  const char *get = "GET /api/info HTTP/1.1\r\nHost: bus\r\n\r\n";
  assert(parseRequest(get, strlen(get), request) == 1); assert(!strcmp(request.path, "/api/info"));
  const char *post = "POST /api/motor HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 4\r\n\r\n-100";
  request = Request(); assert(parseRequest(post, strlen(post), request) == 1);
  assert(request.json); assert(integer(request.body, -100, 100, value) && value == -100);
  assert(parseRequest(post, strlen(post) - 1, request) == 0);
  for (const char *bad : {
    "POST /api/motor HTTP/1.1\r\nContent-Length: 99\r\n\r\n",
    "POST /api/motor HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n0",
    "POST /api/motor HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
    "POST /api/motor HTTP/1.1\r\n\r\n",
    "GET / HTTP/1.1\r\n\r\nx"}) {
    request = Request(); assert(parseRequest(bad, strlen(bad), request) == -1);
  }
  const char *cross = "POST /api/servo HTTP/1.1\r\ncontent-type: application/json\r\nsec-fetch-site: cross-site\r\ncontent-length: 2\r\n\r\n90";
  request = Request(); assert(parseRequest(cross, strlen(cross), request) == 1 && request.crossSite && request.json);
  std::cout << "Watchdog, rollover, validation and HTTP parsing passed\n";
}
