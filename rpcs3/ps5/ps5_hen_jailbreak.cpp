#include "stdafx.h"
#include "ps5_hen_jailbreak.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5_sce.h"

// WHY A HOMEBREW TITLE HAS TO ASK TO BE JAILBROKEN
//
// A title named in the console's allowlist (/data/whitelist.txt, read by PS5SXHelper
// here) is only *permitted to launch*. The daemon that admits it can also lift its app
// sandbox, but it does that per process, on request -- not as a side effect of the
// allowlist. Until it happens, "/data" is not the console's /data: it resolves inside the
// title's own sandbox, /mnt/sandbox/<TITLE_ID>_000/data, which does not exist. Every read
// and write under /data/rpcs3/ then fails with ENOENT.
//
// That is the whole of the black screen. The title could not see the PS3UPDAT.PUP, the
// games/ folder or boot.txt sitting in the real /data/rpcs3/, and it could not open
// cache/RPCS3.log to say that it could not. Nothing downstream -- firmware install, game
// boot, RADV -- was ever reached.
//
// The request protocol is the daemons' own, and all of them answer to the etaHEN name
// (OnionHEN watches both etahen and onionhen; PS5SXHelper takes it too, verified on this
// console, where a hand-written request produced, within a second:
//
//   [JB] request pid=112 tid=PPSA99300 pre-jb uid=1 authid=0x4400001084c2052d
//   [JB] post-jb pid=112 uid=0 (was 1) authid=0x4801000000000013 ok=1
//   [JB] OK: pid=112 tid=PPSA99300 fully jailbroken
//   [JB] cleared request file /mnt/sandbox/PPSA99300_000/download0/etahen_jailbreak
//
// ):
//
//   the title writes {"PID":<pid>} to /download0/etahen_jailbreak -- /download0 is
//   relative to the title, so that is the download0 inside its own sandbox, the directory
//   the daemon already watches for this title;
//   the daemon reads the pid, sets that process's uid and authid to root, and removes the
//   request.
//
// PCSX2's PS5 port does exactly this at boot (PS5SX2, orbis-shims/ProsperoHenJailbreak.cpp),
// which is why that tile runs and this one did not.
//
// TIMING is PS5SX2's: the daemon scans, so poll at ~60 Hz with a bounded budget, and allow
// a separate grace period after the request is consumed, because the daemon returns to its
// loop between taking the file and applying the credentials.
//
// READINESS is the real /data becoming visible, not the request file going away -- /data is
// the thing every later step actually needs. uid is not consulted: it is not exported to a
// title, and PS5SX2 notes that on some helpers it keeps reporting the sandbox value even
// once the process is root.

namespace
{
	constexpr const char* request_path = "/download0/etahen_jailbreak";
	constexpr const char* staged_path = "/download0/etahen_jailbreak.tmp";

	constexpr unsigned poll_us = 16667;     // ~60 Hz
	constexpr unsigned consume_polls = 360; // ~6 s for a daemon to pick the request up
	constexpr unsigned visible_polls = 600; // ~10 s for the credentials to land

	bool path_exists(const char* path)
	{
		struct stat st{};
		return ::stat(path, &st) == 0;
	}

	bool data_is_visible()
	{
		struct stat st{};
		return ::stat("/data", &st) == 0 && S_ISDIR(st.st_mode);
	}
}

bool ps5::request_jailbreak()
{
	const int pid = static_cast<int>(::getpid());

	if (pid <= 1)
	{
		std::fprintf(stderr, "[hen] not requesting: unusable pid %d\n", pid);
		return false;
	}

	// A request left over from an earlier run names a stale pid; clear it so the daemon
	// cannot read the wrong process. Write through a staged name and rename into place so
	// the daemon can never see a half-written request.
	::unlink(request_path);
	::unlink(staged_path);

	char body[32];
	const int len = std::snprintf(body, sizeof(body), "{\"PID\":%d}\n", pid);

	const int fd = ::open(staged_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	if (fd < 0)
	{
		// No /download0 to publish into: this console's helper does not drive titles this
		// way, so carry on sandboxed rather than stall the boot.
		std::fprintf(stderr, "[hen] no request: %s is not writable (errno %d)\n", staged_path, errno);
		return false;
	}

	const ssize_t written = ::write(fd, body, static_cast<size_t>(len));
	::close(fd);

	if (written != len || ::rename(staged_path, request_path) != 0)
	{
		std::fprintf(stderr, "[hen] no request: publish failed (errno %d)\n", errno);
		::unlink(staged_path);
		return false;
	}

	std::fprintf(stderr, "[hen] requested jailbreak for pid %d\n", pid);

	// Phase 1: wait for the daemon to consume the request.
	unsigned polls = 0;

	for (; polls < consume_polls; ++polls)
	{
		sceKernelUsleep(poll_us);

		if (!path_exists(request_path))
		{
			break;
		}
	}

	if (polls == consume_polls)
	{
		std::fprintf(stderr, "[hen] no daemon answered within %u ms; continuing sandboxed\n",
			consume_polls * poll_us / 1000);
		return false;
	}

	std::fprintf(stderr, "[hen] request consumed after %u ms; waiting for /data\n", (polls + 1) * poll_us / 1000);

	// Phase 2: the request being gone is only the consume signal -- the daemon still has to
	// apply the credentials before /data is the console's.
	for (unsigned grace = 0; grace < visible_polls; ++grace)
	{
		if (data_is_visible())
		{
			std::fprintf(stderr, "[hen] jailbroken: /data is the console's /data\n");
			return true;
		}

		sceKernelUsleep(poll_us);
	}

	std::fprintf(stderr, "[hen] request consumed but /data is still the sandbox's\n");
	return false;
}
