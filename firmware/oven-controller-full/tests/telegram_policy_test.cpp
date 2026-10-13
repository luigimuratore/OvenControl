#include "../src/telegram_policy.h"
#include <cassert>
#include <initializer_list>

int main() {
  using namespace telegram;
  assert(validToken("12345:abcdefghijklmnopqrstuvwxyz_123"));
  assert(!validToken("")); assert(!validToken("123:short"));
  assert(!validToken("123:abcdefghijklmnopqrstuvwxyz/path"));
  assert(!authorized(42, 43, 43, "private"));
  assert(!authorized(42, 42, 43, "private"));
  assert(!authorized(42, 42, 42, "group"));
  assert(!authorized(-42, -42, 42, "private"));
  assert(authorized(42, 42, 42, "private"));
  assert(fresh(100, 220)); assert(!fresh(100, 221)); assert(!fresh(221, 220));
  assert(command("/status") == Command::Status);
  for (auto text : {"/start", "/help"}) assert(command(text) == Command::Help);
  assert(command("/modalita") == Command::ModeMenu);
  for (auto text : {"/essenziale", kEssentialButton}) assert(command(text) == Command::Essential);
  for (auto text : {"/sintetica", kBriefButton}) assert(command(text) == Command::Brief);
  for (auto text : {"/dettagliata", kDetailedButton}) assert(command(text) == Command::Detailed);
  assert(command("/essenziale extra") == Command::None);
  assert(!allowEvent(Mode::Essential, false)); assert(allowEvent(Mode::Essential, true));
  for (auto mode : {Mode::Brief, Mode::Detailed}) {
    assert(allowEvent(mode, false)); assert(allowEvent(mode, true));
  }
  assert(modeFrom(99) == Mode::Brief && modeFrom(-1) == Mode::Brief);
  assert(modeFrom(0) == Mode::Essential && modeFrom(2) == Mode::Detailed);
  assert(strlen(kGuideText) + 80 < kMessageBytes);
  for (auto text : {"/stop", "/start prova", "/reset", "/status extra", "hello"})
    assert(command(text) == Command::None);
  assert(!expired(10001, 1, true, false)); assert(expired(10002, 1, true, false));
  assert(expired(3600002, 1, false, false)); assert(!expired(3600002, 1, false, true));
  assert(expired(10001, UINT32_MAX - 1, true, false));
  assert(retryMs(1) == 5000); assert(retryMs(4) == 60000);
  assert(retryMs(1, 90) == 90000); assert(retryMs(10, UINT32_MAX) == 86400000);
}
