/* Functions which are exposed to the test suite */

extern unsigned describe_layout(char *, ptrdiff_t, unsigned);
extern unsigned describe_row(char *, size_t, int);
extern int smtx_main(int, char **);

#define CTL(x) ((x) & 0x1f)
