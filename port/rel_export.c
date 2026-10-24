/* Compiled into every REL module: the only exported symbols. Everything
 * else in a REL has hidden visibility, as RELs only exposed _prolog/_epilog. */
#if defined(_WIN32)
#define PORT_REL_EXPORT __declspec(dllexport)
#else
#define PORT_REL_EXPORT __attribute__((visibility("default")))
#endif

void _prolog(void);
void _epilog(void);

PORT_REL_EXPORT void port_rel_prolog(void) {
    _prolog();
}

PORT_REL_EXPORT void port_rel_epilog(void) {
    _epilog();
}
