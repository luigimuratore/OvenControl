#include <cassert>
#include <string>
#include <iostream>
#include "../src/http_request.h"
#include "../src/heater_guard.h"

static ovenhttp::Request parse(const std::string &wire, size_t fragment = 1) {
  ovenhttp::Request request;
  for (size_t pos=0;pos<wire.size();pos+=fragment)request.feed(wire.data()+pos,std::min(fragment,wire.size()-pos));
  return request;
}
int main() {
  // Expiration must suppress late starts while still accepting STOP/emergency.
  assert(ovenhttp::safetyAction("/api/command","emergency"));
  assert(ovenhttp::safetyAction("/api/command?x=1","stop"));
  assert(ovenhttp::safetyAction("/api/manual","stop"));
  assert(ovenhttp::safetyAction("/api/test/command","exit"));
  assert(!ovenhttp::safetyAction("/api/command","start"));
  assert(!ovenhttp::safetyAction("/api/command","resume"));
  assert(!ovenhttp::safetyAction("/api/manual","target"));
  assert(!ovenhttp::safetyAction("/api/commands","stop"));
  const std::string body=R"({"action":"start","hardwareReady":true})";
  const std::string post="POST /api/command?x=1 HTTP/1.1\r\nHost: forno\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
  for (size_t n=1;n<=post.size();++n) {
    const auto r=parse(post,n);assert(r.complete&&!r.error&&r.method==ovenhttp::Method::Post);
    assert(r.bodyBytes==body.size()&&r.body()==body);assert(std::string(r.uri)=="/api/command?x=1");
  }
  assert(parse("GET /api/status HTTP/1.1\r\n\r\n").complete);
  assert(parse("HEAD / HTTP/1.0\r\n\r\n").method==ovenhttp::Method::Head);
  assert(!parse(post.substr(0,post.size()-1)).complete);
  assert(parse("POST / HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n").error==400);
  assert(parse("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n").error==400);
  assert(parse("POST / HTTP/1.1\r\nContent-Length: 16001\r\n\r\n").error==413);
  assert(parse("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n").error==400);
  assert(parse("POST / HTTP/1.1\r\nContent-Length: 12x\r\n\r\n").error==400);
  assert(parse("POST / HTTP/1.1\r\nContent-Length: 999999999999999999999999999\r\n\r\n").error==413);
  assert(parse("PUT / HTTP/1.1\r\n\r\n").error==405);
  assert(parse("GET / HTTP/9\r\n\r\n").error==400);
  assert(parse("GET / HTTP/1.1\r\nX: "+std::string(4096,'a')).error==431);
  const std::string full(16000,'x');
  assert(parse("POST /api/recipes HTTP/1.1\r\ncontent-length:\t16000 \r\n\r\n"+full,1024).complete);
  const auto pipeline=parse("GET /api/status HTTP/1.1\r\n\r\n"+post,1024);
  assert(pipeline.complete&&pipeline.method==ovenhttp::Method::Get&&pipeline.bodyBytes==0);
  // Incomplete/fractured HTTP input never authorizes a command. A control plan
  // can keep being refreshed while individual request fragments make progress.
  ovenhttp::Request stalled; stalled.feed(post.data(),30); assert(!stalled.complete);
  heater::Guard guard;
  for(uint32_t now=0;now<20000;now+=10) {
    heater::Plan plan;plan.cycleActive=plan.heatingAllowed=plan.healthy=true;
    plan.sampleAt=now-now%500;plan.windowMs=5000;plan.duty=60;guard.publish(plan);guard.tick(now);
    assert(guard.fault==heater::Fault::None&&!stalled.complete);
  }
  std::cout<<"HTTP framing: all fragment boundaries, body limits, malformed/truncated requests, pipelining and control independence passed\n";
}
