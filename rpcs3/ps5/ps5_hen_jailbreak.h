#pragma once

namespace ps5
{
	// Asks the console's HEN helper to lift this title's app sandbox, and waits for it to
	// take effect. Must run before anything reads /data. Returns true once the console's
	// real /data is visible, false if no helper answered (the title then runs sandboxed,
	// as it did before).
	bool request_jailbreak();
}
