std::map<std::string, LoadedSound> OptimizedSoundLoader::load_sounds_parallel
- change to unorderedmap for faster lookup, sorting is not important here?

using clock = std::chrono::steady_clock;
- consider moving this to top of cpp file or even headerfile to avoid rewriting

what are templates/template class?

lots of safety features and logging, is this okay for final product? Concern is that it maybe slows down the program or stores unnecessary logging data that can take up tons of storage? Especially on a device like a raspberry pi zero 2 w, which has my 16 gb sd card.

M4PendingOffsetSound
- might be worth initializing vars in this struct, possible UB

