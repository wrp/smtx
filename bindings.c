
#include "smtx.h"
#pragma GCC diagnostic ignored "-Woverride-init"
struct handler k1[128] = {
	[0x00] = { .act = { .a = passthru }, "\x01\x00" },
	[0x01] = { .act = { .a = passthru }, "\x01\x01" },
	[0x02] = { .act = { .a = passthru }, "\x01\x02" },
	[0x03] = { .act = { .a = passthru }, "\x01\x03" },
	[0x04] = { .act = { .a = passthru }, "\x01\x04" },
	[0x05] = { .act = { .a = passthru }, "\x01\x05" },
	[0x06] = { .act = { .a = passthru }, "\x01\x06" },
	[0x07] = { .act = { .a = passthru }, "\x01\x07" },
	[0x08] = { .act = { .a = passthru }, "\x01\x08" },
	[0x09] = { .act = { .a = passthru }, "\x01\x09" },
	[0x0a] = { .act = { .a = passthru }, "\x01\x0a" },
	[0x0b] = { .act = { .a = passthru }, "\x01\x0b" },
	[0x0c] = { .act = { .a = passthru }, "\x01\x0c" },
	[0x0d] = { .act = { .a = send }, "\r" },
	[0x0e] = { .act = { .a = passthru }, "\x01\x0e" },
	[0x0f] = { .act = { .a = passthru }, "\x01\x0f" },

	[0x10] = { .act = { .a = passthru }, "\x01\x10" },
	[0x11] = { .act = { .a = passthru }, "\x01\x11" },
	[0x12] = { .act = { .a = passthru }, "\x01\x12" },
	[0x13] = { .act = { .a = passthru }, "\x01\x13" },
	[0x14] = { .act = { .a = passthru }, "\x01\x14" },
	[0x15] = { .act = { .a = passthru }, "\x01\x15" },
	[0x16] = { .act = { .a = passthru }, "\x01\x16" },
	[0x17] = { .act = { .a = passthru }, "\x01\x17" },
	[0x18] = { .act = { .a = passthru }, "\x01\x18" },
	[0x19] = { .act = { .a = passthru }, "\x01\x19" },
	[0x1a] = { .act = { .a = passthru }, "\x01\x1a" },
	[0x1b] = { .act = { .a = passthru }, "\x01\x1b" },
	[0x1c] = { .act = { .a = passthru }, "\x01\x1c" },
	[0x1d] = { .act = { .a = passthru }, "\x01\x1d" },
	[0x1e] = { .act = { .a = passthru }, "\x01\x1e" },
	[0x1f] = { .act = { .a = passthru }, "\x01\x1f" },

	[0x20] = { .act = { .a = passthru }, "\x01\x20" },
	[0x21] = { .act = { .a = passthru }, "\x01\x21" },
	[0x22] = { .act = { .a = passthru }, "\x01\x22" },
	[0x23] = { .act = { .a = passthru }, "\x01\x23" },
	[0x24] = { .act = { .a = passthru }, "\x01\x24" },
	[0x25] = { .act = { .a = passthru }, "\x01\x25" },
	[0x26] = { .act = { .a = passthru }, "\x01\x26" },
	[0x27] = { .act = { .a = passthru }, "\x01\x27" },
	[0x28] = { .act = { .a = passthru }, "\x01\x28" },
	[0x29] = { .act = { .a = passthru }, "\x01\x29" },
	[0x2a] = { .act = { .a = passthru }, "\x01\x2a" },
	[0x2b] = { .act = { .a = passthru }, "\x01\x2b" },
	[0x2c] = { .act = { .a = passthru }, "\x01\x2c" },
	[0x2d] = { .act = { .a = passthru }, "\x01\x2d" },
	[0x2e] = { .act = { .a = passthru }, "\x01\x2e" },
	[0x2f] = { .act = { .a = passthru }, "\x01\x2f" },

	[0x30] = { .act = { .a = passthru }, "\x01\x30" },
	[0x31] = { .act = { .a = passthru }, "\x01\x31" },
	[0x32] = { .act = { .a = passthru }, "\x01\x32" },
	[0x33] = { .act = { .a = passthru }, "\x01\x33" },
	[0x34] = { .act = { .a = passthru }, "\x01\x34" },
	[0x35] = { .act = { .a = passthru }, "\x01\x35" },
	[0x36] = { .act = { .a = passthru }, "\x01\x36" },
	[0x37] = { .act = { .a = passthru }, "\x01\x37" },
	[0x38] = { .act = { .a = passthru }, "\x01\x38" },
	[0x39] = { .act = { .a = passthru }, "\x01\x39" },
	[0x3a] = { .act = { .a = passthru }, "\x01\x3a" },
	[0x3b] = { .act = { .a = passthru }, "\x01\x3b" },
	[0x3c] = { .act = { .a = passthru }, "\x01\x3c" },
	[0x3d] = { .act = { .a = passthru }, "\x01\x3d" },
	[0x3e] = { .act = { .a = passthru }, "\x01\x3e" },
	[0x3f] = { .act = { .a = passthru }, "\x01\x3f" },

	[0x40] = { .act = { .a = passthru }, "\x01\x40" },
	[0x41] = { .act = { .a = passthru }, "\x01\x41" },
	[0x42] = { .act = { .a = passthru }, "\x01\x42" },
	[0x43] = { .act = { .a = passthru }, "\x01\x43" },
	[0x44] = { .act = { .a = passthru }, "\x01\x44" },
	[0x45] = { .act = { .a = passthru }, "\x01\x45" },
	[0x46] = { .act = { .a = passthru }, "\x01\x46" },
	[0x47] = { .act = { .a = passthru }, "\x01\x47" },
	[0x48] = { .act = { .a = passthru }, "\x01\x48" },
	[0x49] = { .act = { .a = passthru }, "\x01\x49" },
	[0x4a] = { .act = { .a = passthru }, "\x01\x4a" },
	[0x4b] = { .act = { .a = passthru }, "\x01\x4b" },
	[0x4c] = { .act = { .a = passthru }, "\x01\x4c" },
	[0x4d] = { .act = { .a = passthru }, "\x01\x4d" },
	[0x4e] = { .act = { .a = passthru }, "\x01\x4e" },
	[0x4f] = { .act = { .a = passthru }, "\x01\x4f" },

	[0x50] = { .act = { .a = passthru }, "\x01\x50" },
	[0x51] = { .act = { .a = passthru }, "\x01\x51" },
	[0x52] = { .act = { .a = passthru }, "\x01\x52" },
	[0x53] = { .act = { .a = passthru }, "\x01\x53" },
	[0x54] = { .act = { .a = passthru }, "\x01\x54" },
	[0x55] = { .act = { .a = passthru }, "\x01\x55" },
	[0x56] = { .act = { .a = passthru }, "\x01\x56" },
	[0x57] = { .act = { .a = passthru }, "\x01\x57" },
	[0x58] = { .act = { .a = passthru }, "\x01\x58" },
	[0x59] = { .act = { .a = passthru }, "\x01\x59" },
	[0x5a] = { .act = { .a = passthru }, "\x01\x5a" },
	[0x5b] = { .act = { .a = passthru }, "\x01\x5b" },
	[0x5c] = { .act = { .a = passthru }, "\x01\x5c" },
	[0x5d] = { .act = { .a = passthru }, "\x01\x5d" },
	[0x5e] = { .act = { .a = passthru }, "\x01\x5e" },
	[0x5f] = { .act = { .a = passthru }, "\x01\x5f" },

	[0x60] = { .act = { .a = passthru }, "\x01\x60" },
	[0x61] = { .act = { .a = passthru }, "\x01\x61" },
	[0x62] = { .act = { .a = passthru }, "\x01\x62" },
	[0x63] = { .act = { .a = passthru }, "\x01\x63" },
	[0x64] = { .act = { .a = passthru }, "\x01\x64" },
	[0x65] = { .act = { .a = passthru }, "\x01\x65" },
	[0x66] = { .act = { .a = passthru }, "\x01\x66" },
	[0x67] = { .act = { .a = passthru }, "\x01\x67" },
	[0x68] = { .act = { .a = passthru }, "\x01\x68" },
	[0x69] = { .act = { .a = passthru }, "\x01\x69" },
	[0x6a] = { .act = { .a = passthru }, "\x01\x6a" },
	[0x6b] = { .act = { .a = passthru }, "\x01\x6b" },
	[0x6c] = { .act = { .a = passthru }, "\x01\x6c" },
	[0x6d] = { .act = { .a = passthru }, "\x01\x6d" },
	[0x6e] = { .act = { .a = passthru }, "\x01\x6e" },
	[0x6f] = { .act = { .a = passthru }, "\x01\x6f" },

	[0x70] = { .act = { .a = passthru }, "\x01\x70" },
	[0x71] = { .act = { .a = passthru }, "\x01\x71" },
	[0x72] = { .act = { .a = passthru }, "\x01\x72" },
	[0x73] = { .act = { .a = passthru }, "\x01\x73" },
	[0x74] = { .act = { .a = passthru }, "\x01\x74" },
	[0x75] = { .act = { .a = passthru }, "\x01\x75" },
	[0x76] = { .act = { .a = passthru }, "\x01\x76" },
	[0x77] = { .act = { .a = passthru }, "\x01\x77" },
	[0x78] = { .act = { .a = passthru }, "\x01\x78" },
	[0x79] = { .act = { .a = passthru }, "\x01\x79" },
	[0x7a] = { .act = { .a = passthru }, "\x01\x7a" },
	[0x7b] = { .act = { .a = passthru }, "\x01\x7b" },
	[0x7c] = { .act = { .a = passthru }, "\x01\x7c" },
	[0x7d] = { .act = { .a = passthru }, "\x01\x7d" },
	[0x7e] = { .act = { .a = passthru }, "\x01\x7e" },
	[0x7f] = { .act = { .a = passthru }, "\x01\x7f" },
};

struct handler ctl[128] = {
	[0x00] = { .act = { .a = bad_key }, "\x01\x00" },
	[0x01] = { .act = { .a = bad_key }, "\x01\x01" },
	[0x02] = { .act = { .a = bad_key }, "\x01\x02" },
	[0x03] = { .act = { .a = bad_key }, "\x01\x03" },
	[0x04] = { .act = { .a = bad_key }, "\x01\x04" },
	[0x05] = { .act = { .a = bad_key }, "\x01\x05" },
	[0x06] = { .act = { .a = bad_key }, "\x01\x06" },
	[0x07] = { .act = { .a = bad_key }, "\x01\x07" },
	[0x08] = { .act = { .a = bad_key }, "\x01\x08" },
	[0x09] = { .act = { .a = bad_key }, "\x01\x09" },
	[0x0a] = { .act = { .a = bad_key }, "\x01\x0a" },
	[0x0b] = { .act = { .a = bad_key }, "\x01\x0b" },
	[0x0c] = { .act = { .a = bad_key }, "\x01\x0c" },
	[0x0d] = { .act = { .a = bad_key }, "\x01\x0d" },
	[0x0e] = { .act = { .a = bad_key }, "\x01\x0e" },
	[0x0f] = { .act = { .a = bad_key }, "\x01\x0f" },

	[0x10] = { .act = { .a = bad_key }, "\x01\x10" },
	[0x11] = { .act = { .a = bad_key }, "\x01\x11" },
	[0x12] = { .act = { .a = bad_key }, "\x01\x12" },
	[0x13] = { .act = { .a = bad_key }, "\x01\x13" },
	[0x14] = { .act = { .a = bad_key }, "\x01\x14" },
	[0x15] = { .act = { .a = bad_key }, "\x01\x15" },
	[0x16] = { .act = { .a = bad_key }, "\x01\x16" },
	[0x17] = { .act = { .a = bad_key }, "\x01\x17" },
	[0x18] = { .act = { .a = bad_key }, "\x01\x18" },
	[0x19] = { .act = { .a = bad_key }, "\x01\x19" },
	[0x1a] = { .act = { .a = bad_key }, "\x01\x1a" },
	[0x1b] = { .act = { .a = bad_key }, "\x01\x1b" },
	[0x1c] = { .act = { .a = bad_key }, "\x01\x1c" },
	[0x1d] = { .act = { .a = bad_key }, "\x01\x1d" },
	[0x1e] = { .act = { .a = bad_key }, "\x01\x1e" },
	[0x1f] = { .act = { .a = bad_key }, "\x01\x1f" },

	[0x20] = { .act = { .a = bad_key }, "\x01\x20" },
	[0x21] = { .act = { .a = bad_key }, "\x01\x21" },
	[0x22] = { .act = { .a = bad_key }, "\x01\x22" },
	[0x23] = { .act = { .a = bad_key }, "\x01\x23" },
	[0x24] = { .act = { .a = bad_key }, "\x01\x24" },
	[0x25] = { .act = { .a = bad_key }, "\x01\x25" },
	[0x26] = { .act = { .a = bad_key }, "\x01\x26" },
	[0x27] = { .act = { .a = bad_key }, "\x01\x27" },
	[0x28] = { .act = { .a = bad_key }, "\x01\x28" },
	[0x29] = { .act = { .a = bad_key }, "\x01\x29" },
	[0x2a] = { .act = { .a = bad_key }, "\x01\x2a" },
	[0x2b] = { .act = { .a = bad_key }, "\x01\x2b" },
	[0x2c] = { .act = { .a = bad_key }, "\x01\x2c" },
	[0x2d] = { .act = { .a = bad_key }, "\x01\x2d" },
	[0x2e] = { .act = { .a = bad_key }, "\x01\x2e" },
	[0x2f] = { .act = { .a = bad_key }, "\x01\x2f" },

	[0x30] = { .act = { .a = bad_key }, "\x01\x30" },
	[0x31] = { .act = { .a = bad_key }, "\x01\x31" },
	[0x32] = { .act = { .a = bad_key }, "\x01\x32" },
	[0x33] = { .act = { .a = bad_key }, "\x01\x33" },
	[0x34] = { .act = { .a = bad_key }, "\x01\x34" },
	[0x35] = { .act = { .a = bad_key }, "\x01\x35" },
	[0x36] = { .act = { .a = bad_key }, "\x01\x36" },
	[0x37] = { .act = { .a = bad_key }, "\x01\x37" },
	[0x38] = { .act = { .a = bad_key }, "\x01\x38" },
	[0x39] = { .act = { .a = bad_key }, "\x01\x39" },
	[0x3a] = { .act = { .a = bad_key }, "\x01\x3a" },
	[0x3b] = { .act = { .a = bad_key }, "\x01\x3b" },
	[0x3c] = { .act = { .a = bad_key }, "\x01\x3c" },
	[0x3d] = { .act = { .a = bad_key }, "\x01\x3d" },
	[0x3e] = { .act = { .a = bad_key }, "\x01\x3e" },
	[0x3f] = { .act = { .a = bad_key }, "\x01\x3f" },

	[0x40] = { .act = { .a = bad_key }, "\x01\x40" },
	[0x41] = { .act = { .a = bad_key }, "\x01\x41" },
	[0x42] = { .act = { .a = bad_key }, "\x01\x42" },
	[0x43] = { .act = { .a = bad_key }, "\x01\x43" },
	[0x44] = { .act = { .a = bad_key }, "\x01\x44" },
	[0x45] = { .act = { .a = bad_key }, "\x01\x45" },
	[0x46] = { .act = { .a = bad_key }, "\x01\x46" },
	[0x47] = { .act = { .a = bad_key }, "\x01\x47" },
	[0x48] = { .act = { .a = bad_key }, "\x01\x48" },
	[0x49] = { .act = { .a = bad_key }, "\x01\x49" },
	[0x4a] = { .act = { .a = bad_key }, "\x01\x4a" },
	[0x4b] = { .act = { .a = bad_key }, "\x01\x4b" },
	[0x4c] = { .act = { .a = bad_key }, "\x01\x4c" },
	[0x4d] = { .act = { .a = bad_key }, "\x01\x4d" },
	[0x4e] = { .act = { .a = bad_key }, "\x01\x4e" },
	[0x4f] = { .act = { .a = bad_key }, "\x01\x4f" },

	[0x50] = { .act = { .a = bad_key }, "\x01\x50" },
	[0x51] = { .act = { .a = bad_key }, "\x01\x51" },
	[0x52] = { .act = { .a = bad_key }, "\x01\x52" },
	[0x53] = { .act = { .a = bad_key }, "\x01\x53" },
	[0x54] = { .act = { .a = bad_key }, "\x01\x54" },
	[0x55] = { .act = { .a = bad_key }, "\x01\x55" },
	[0x56] = { .act = { .a = bad_key }, "\x01\x56" },
	[0x57] = { .act = { .a = bad_key }, "\x01\x57" },
	[0x58] = { .act = { .a = bad_key }, "\x01\x58" },
	[0x59] = { .act = { .a = bad_key }, "\x01\x59" },
	[0x5a] = { .act = { .a = bad_key }, "\x01\x5a" },
	[0x5b] = { .act = { .a = bad_key }, "\x01\x5b" },
	[0x5c] = { .act = { .a = bad_key }, "\x01\x5c" },
	[0x5d] = { .act = { .a = bad_key }, "\x01\x5d" },
	[0x5e] = { .act = { .a = bad_key }, "\x01\x5e" },
	[0x5f] = { .act = { .a = bad_key }, "\x01\x5f" },

	[0x60] = { .act = { .a = bad_key }, "\x01\x60" },
	[0x61] = { .act = { .a = bad_key }, "\x01\x61" },
	[0x62] = { .act = { .a = bad_key }, "\x01\x62" },
	[0x63] = { .act = { .a = bad_key }, "\x01\x63" },
	[0x64] = { .act = { .a = bad_key }, "\x01\x64" },
	[0x65] = { .act = { .a = bad_key }, "\x01\x65" },
	[0x66] = { .act = { .a = bad_key }, "\x01\x66" },
	[0x67] = { .act = { .a = bad_key }, "\x01\x67" },
	[0x68] = { .act = { .a = bad_key }, "\x01\x68" },
	[0x69] = { .act = { .a = bad_key }, "\x01\x69" },
	[0x6a] = { .act = { .a = bad_key }, "\x01\x6a" },
	[0x6b] = { .act = { .a = bad_key }, "\x01\x6b" },
	[0x6c] = { .act = { .a = bad_key }, "\x01\x6c" },
	[0x6d] = { .act = { .a = bad_key }, "\x01\x6d" },
	[0x6e] = { .act = { .a = bad_key }, "\x01\x6e" },
	[0x6f] = { .act = { .a = bad_key }, "\x01\x6f" },

	[0x70] = { .act = { .a = bad_key }, "\x01\x70" },
	[0x71] = { .act = { .a = bad_key }, "\x01\x71" },
	[0x72] = { .act = { .a = bad_key }, "\x01\x72" },
	[0x73] = { .act = { .a = bad_key }, "\x01\x73" },
	[0x74] = { .act = { .a = bad_key }, "\x01\x74" },
	[0x75] = { .act = { .a = bad_key }, "\x01\x75" },
	[0x76] = { .act = { .a = bad_key }, "\x01\x76" },
	[0x77] = { .act = { .a = bad_key }, "\x01\x77" },
	[0x78] = { .act = { .a = bad_key }, "\x01\x78" },
	[0x79] = { .act = { .a = bad_key }, "\x01\x79" },
	[0x7a] = { .act = { .a = bad_key }, "\x01\x7a" },
	[0x7b] = { .act = { .a = bad_key }, "\x01\x7b" },
	[0x7c] = { .act = { .a = bad_key }, "\x01\x7c" },
	[0x7d] = { .act = { .a = bad_key }, "\x01\x7d" },
	[0x7e] = { .act = { .a = bad_key }, "\x01\x7e" },
	[0x7f] = { .act = { .a = bad_key }, "\x01\x7f" },

	[L'\r'] = { { .a = transition}, "enter" },
	[L'\n'] = { { .a = transition}, "enter" },
	[L'a' ] = { { .a = attach}, "" }, /* NULL */
	[L'b' ] = { { .a = scrolln}, "-" },
	[L'f' ] = { { .a = scrolln}, "+" },
	[L'>' ] = { { .a = scrollh}, ">" },
	[L'<' ] = { { .a = scrollh}, "<" },
	[L'=' ] = { { .a = balance}, NULL },
	[L'c' ] = { { .a = create}, NULL },
	[L'C' ] = { { .a = create}, "C" },
	[L'j' ] = { { .a = mov}, "j" },
	[L'k' ] = { { .a = mov}, "k" },
	[L'l' ] = { { .a = mov}, "l" },
	[L'h' ] = { { .a = mov}, "h" },
	[L'J' ] = { { .a = resize}, "J" },
	[L'H' ] = { { .a = resize}, "H" },
#ifndef NDEBUG
	[L'Q' ] = { { .a = show_status}, NULL },
#endif
	[L's' ] = { { .a = swap}, NULL },
	[L't' ] = { { .a = new_tabstop}, NULL },
	[L'T' ] = { { .a = transpose}, NULL },
	[L'v' ] = { { .a = set_layout}, NULL },
	[L'W' ] = { { .a = set_width}, NULL },
	[L'Z' ] = { { .a = set_history}, NULL },
	[L'x' ] = { { .a = prune}, NULL },
	[L'0' ] = { { .a = digit}, "0" },
	[L'1' ] = { { .a = digit}, "1" },
	[L'2' ] = { { .a = digit}, "2" },
	[L'3' ] = { { .a = digit}, "3" },
	[L'4' ] = { { .a = digit}, "4" },
	[L'5' ] = { { .a = digit}, "5" },
	[L'6' ] = { { .a = digit}, "6" },
	[L'7' ] = { { .a = digit}, "7" },
	[L'8' ] = { { .a = digit}, "8" },
	[L'9' ] = { { .a = digit}, "9" },
};
