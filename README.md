# BytePatch
The world's shittiest runtime memory patching library.
### Features:
- Overwrite arbitrary bytes in the target process
- Minhook-like syntax
- Probably crash if you aren't careful
### Usage:
Clone the project, include `BytePatch.h`, and link against BytePatch.lib. Then, call `BP_CreatePatch` with your address and the new bytes to write. If you're familiar with the syntaxes of Minhook and Pattern16, this will be familiar:
```cpp
#include "include/BytePatch.hpp"
#pragma comment(lib, "BytePatch.lib")
if (BP_CreatePatch(0x1402000c0, "?? ?? 06 ?? 06") != BP_OK) {
  std::cerr << "Failed to create patch!";
  return;
}
if (BP_EnablePatch(0x1402000c0) != BP_OK) {
  std::cerr << "Failed to enable patch!";
}
```
Recommendation: Use Pattern16 with a wrapper in case your target isn't static.

BytePatch signature rules are the same as Pattern16's:
 - All byte values are represented in base16/hexadecimal notation
 - Space characters ` ` are ignored completely even inside bit masks, so use them for formatting
 - Symbols other than `0123456789ABCDEFabcdef[]` are wildcards and can stand in for any byte or bit
 - A sequence of symbols within sqare brackets `[]` represents a bit field. Don't forget there are 8 bits in a byte!
 - Bits inside a bitfield can be masked with wildcard symbols
 - A bitfield does not have to be limited to a single byte, `[01xx1100 xxx11xx0]` is a legal 2-byte masked bitfield

### Credit:
- [Dasaav](https://github.com/Dasaav-dsv) for [Pattern16](https://github.com/Dasaav-dsv/Pattern16/), which was quite useful in implementation. You might even notice some things I 'borrowed' ;)
- [TsudaKageyu](https://github.com/TsudaKageyu) for [minhook](https://github.com/TsudaKageyu/minhook) - the syntax is deliberately similar to minhook's to make it easy to adopt.
