# PLAYTEST-5e results — 2026-10-16

User: the boxes flickered for a split second, nothing steady.
- Log: thread-safe mode reported off, no command lists, yet the hooked Draw saw only 19-37 game draws per 10 s (the game draws ~85,000) while target binds arrived. The game's draws go through method versions that were never in the context's table at Present, so hooking "both versions" (2 tables) wasn't enough.
- (Between sessions the game had to be reinstalled - stuck on EA authentication; same exe 1.0.3.47248, mod redeployed.)
