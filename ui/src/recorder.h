#pragma once
// banana-ui's part of the flight recorder (Settings > System > Developer): its own events (the menu's screens, a
// setting changed, a game started and ended, a notice) sent to bananad, which writes the recording. Nothing is sent
// unless it records (/run/banana/recording, looked at once a second), and a send never waits (a datagram dropped
// when bananad is busy). No passwords, account names or typed text go in.
namespace banana
{
	void recordEvent(const char* source, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
}
