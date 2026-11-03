/* Per-REL constructor tables, normally emitted by the REL linker. Compiled
 * into each REL's pre-linked object (CMakeLists.txt), so they stay local. */
typedef void (*voidfunctionptr)(void);
voidfunctionptr _ctors[] = {0};
voidfunctionptr _dtors[] = {0};
