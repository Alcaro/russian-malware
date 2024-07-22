Arlib is my utility library / std:: replacement.
It exists for several reasons:
- The C++ standard headers contain plenty of strange features and options whose existence
    significantly increases compile time and error message verbosity. For example, I only have one
    string class, no allocators or wchar_t; this allows me to define most member functions outside
    the header.
- The std:: types lack, or lacked when I started Arlib, lots of useful features, for example an easy
    way to check if a string starts with another, or an easy way to split a string by linebreaks.
    (There are no standardized sockets or JSON parsers either, but third party libraries can fill
    that omission.)
- The std:: types often contain strange guarantees whose existence increase the constant factor of
    every operation (for example, std::unordered_map must be implemented as an array of linked
    lists, open addressing is forbidden), even if I have no use for such guarantees.
- I prioritize binary size and easy distribution on Windows (one single file, no DLLs), and
    including libstdc++ would often triple the program size.
- And, most importantly, every feature I implement is a feature I fully understand, so I can debug
    it, debug other instances of the same protocol or format, know which edge cases are likely to
    cause bugs (for example to write a test suite, or research potential security vulnerabilities),
    and appreciate the true complexity of something that intuitively should be simple.
There are, of course, several drawbacks to rejecting the standard library, the most obvious one
  being the time it took me to reimplement it. While I'm happy with the knowledge and results I've
  gained, it's not a path for everyone.

Arlib is designed for my personal use only. While some things I've built on top are intended to be
  usable by others, my goal is making high quality software; actually having it used is more of a
  side effect. I rarely advertise/announce them, make releases, or similar.

As such, outsider contributions would be unexpected. Bug reports are welcome, but I can't promise
  anything about feature requests and pull requests; simplicity is also a feature.

I have rewritten parts of Arlib many times, and there are still a few pieces that need rewrites as
  soon as I can think of a better design. Each rewrite is a thing I've learned not to do, and I
  intend to keep learning.

I care only about the final state of my tools, not the way there; I will fix imperfections whenever
  I find them. As such, change frequency is high, and a commit for each change would be quite noisy,
  so I don't bother - I batch up minor changes until anything significant happens (though I often
  don't realize when I finish those big changes, so they too tend to get batched up).
