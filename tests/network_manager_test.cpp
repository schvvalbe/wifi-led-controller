#include "NetworkManager.h"
#include <ESP8266WiFi.h>
#include <assert.h>
#include <stdio.h>

int main() {
  Configuration config;
  NetworkManager n;
  n.begin(config);
  assert(n.apActive() && WiFi.ap); // No known network: immediate provisioning.
  assert(n.connect("New network", "test-pass"));
  assert(!n.connect("Other", "another-pass"));
  fakeMillis += 700;
  assert(!n.tick(config));
  assert(config.ssid.isEmpty()); // Do not save an untested network.
  assert(WiFi.attempts == 1);
  WiFi.statusValue = WL_CONNECTED;
  assert(n.tick(config)); // The timestamp advanced inside begin: must not time out immediately.
  assert(config.ssid == "New network");
  assert(config.password == "test-pass");
  assert(!n.tick(config)); // Persist only once.
  fakeMillis += 59999; n.tick(config); assert(n.apActive());
  fakeMillis += 1; n.tick(config); assert(!n.apActive());

  WiFi.statusValue = WL_DISCONNECTED;
  n.tick(config); // Retry saved network, without rebooting.
  fakeMillis += 25000; n.tick(config);
  assert(n.apActive() && config.ssid == "New network");
  assert(n.startScan());
  assert(!n.connect("Blocked during scan", "password"));
  WiFi.scanValue = 0; n.tick(config);
  assert(n.connect("Incorrect network", "bad-password"));
  fakeMillis += 700; n.tick(config);
  fakeMillis += 25000; n.tick(config);
  assert(config.ssid == "New network" && config.password == "test-pass");
  assert(n.apActive());
  fakeMillis += 5000; n.tick(config);
  assert(WiFi.attemptedSsid == "New network"); // Roll back to known credentials.

  // Connection timeout across millis wrap.
  WiFi = FakeWiFi{}; fakeMillis = UINT32_MAX - 1000;
  const uint32_t started = fakeMillis;
  NetworkManager wrap;
  wrap.begin(config);
  fakeMillis = started + 24999; wrap.tick(config); assert(!wrap.apActive());
  fakeMillis += 1; wrap.tick(config); assert(wrap.apActive());
  puts("PASS: first boot AP, delayed connection, save-on-success, AP grace, reconnect, scan exclusion, invalid-password rollback, millis wrap");
}
