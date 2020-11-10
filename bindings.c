
#include "smtx.h"
#pragma GCC diagnostic ignored "-Woverride-init"

struct handler ctl[128] = {
	[0x00 ... 0x7f] = { .act = { .v = vbeep }, NULL },

	[L'\r'] = { { .a = transition}, "enter" },
	[L'\n'] = { { .a = transition}, "enter" },
	[L'a' ] = { { .v = attach}, NULL },
	[L'b' ] = { { .a = scrolln}, "-" },
	[L'f' ] = { { .a = scrolln}, "+" },
	[L'g' ] = { { .v = focus}, NULL },
	[L'>' ] = { { .a = scrollh}, ">" },
	[L'<' ] = { { .a = scrollh}, "<" },
	[L'=' ] = { { .a = balance}, "=" },
	[L'c' ] = { { .a = create}, "-" },
	[L'C' ] = { { .a = create}, "|" },
	[L'h' ] = { { .a = mov}, "h" },
	[L'j' ] = { { .a = mov}, "j" },
	[L'k' ] = { { .a = mov}, "k" },
	[L'l' ] = { { .a = mov}, "l" },
	[L'N' ] = { { .v = next}, NULL },
#ifndef NDEBUG
	[L'Q' ] = { { .a = show_status}, "x" },
#endif
	[L'S' ] = { { .v = swap}, NULL },
	[L't' ] = { { .v = new_tabstop}, NULL },
	[L'T' ] = { { .v = transpose}, NULL },
	[L'v' ] = { { .v = set_layout}, NULL },
	[L'W' ] = { { .a = set_width}, "" },
	[L'Z' ] = { { .v = set_history}, NULL },
	[L'x' ] = { { .v = prune}, NULL },
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
	[L'-' ] = { { .a = resize}, "-" },
	[L'|' ] = { { .a = resize}, "|" },
};

struct handler code_keys[KEY_MAX - KEY_MIN + 1] = {
	[KEY_RESIZE - KEY_MIN]     = { { .v = reshape_root }, NULL },
	[KEY_F(1) - KEY_MIN]       = { { .a = send }, "\x01\033" },
	[KEY_F(2) - KEY_MIN]       = { { .a = send }, "\x03\033OQ" },
	[KEY_F(3) - KEY_MIN]       = { { .a = send }, "\x03\033OR" },
	[KEY_F(4) - KEY_MIN]       = { { .a = send }, "\x03\033OS" },
	[KEY_F(5) - KEY_MIN]       = { { .a = send }, "\x05\033[15~" },
	[KEY_F(6) - KEY_MIN]       = { { .a = send }, "\x05\033[17~" },
	[KEY_F(7) - KEY_MIN]       = { { .a = send }, "\x05\033[18~" },
	[KEY_F(8) - KEY_MIN]       = { { .a = send }, "\x05\033[19~" },
	[KEY_F(9) - KEY_MIN]       = { { .a = send }, "\x05\033[20~" },
	[KEY_F(10) - KEY_MIN]      = { { .a = send }, "\x05\033[21~" },
	[KEY_F(11) - KEY_MIN]      = { { .a = send }, "\x05\033[23~" },
	[KEY_F(12) - KEY_MIN]      = { { .a = send }, "\x05\033[24~" },
	[KEY_HOME - KEY_MIN]       = { { .a = send }, "\x04\033[1~" },
	[KEY_END - KEY_MIN]        = { { .a = send }, "\x04\033[4~" },
	[KEY_PPAGE - KEY_MIN]      = { { .a = send }, "\x04\033[5~" },
	[KEY_NPAGE - KEY_MIN]      = { { .a = send }, "\x04\033[6~" },
	[KEY_BACKSPACE - KEY_MIN]  = { { .a = send }, "\x01\177" },
	[KEY_DC - KEY_MIN]         = { { .a = send }, "\x04\033[3~" },
	[KEY_IC - KEY_MIN]         = { { .a = send }, "\x04\033[2~" },
	[KEY_BTAB - KEY_MIN]       = { { .a = send }, "\x04\033[Z" },
	[KEY_ENTER - KEY_MIN]      = { { .v = send_cr }, NULL },
	[KEY_UP - KEY_MIN]         = { { .a = sendarrow }, "A" },
	[KEY_DOWN - KEY_MIN]       = { { .a = sendarrow }, "B" },
	[KEY_RIGHT - KEY_MIN]      = { { .a = sendarrow }, "C" },
	[KEY_LEFT - KEY_MIN]       = { { .a = sendarrow }, "D" },
};

struct handler k1[128] = {
	[0x00] = { .act = { .a = send }, "\x01\x00" },
	[0x01] = { .act = { .a = send }, "\x01\x01" },
	[0x02] = { .act = { .a = send }, "\x01\x02" },
	[0x03] = { .act = { .a = send }, "\x01\x03" },
	[0x04] = { .act = { .a = send }, "\x01\x04" },
	[0x05] = { .act = { .a = send }, "\x01\x05" },
	[0x06] = { .act = { .a = send }, "\x01\x06" },
	[0x07] = { .act = { .a = send }, "\x01\x07" },
	[0x08] = { .act = { .a = send }, "\x01\x08" },
	[0x09] = { .act = { .a = send }, "\x01\x09" },
	[0x0a] = { .act = { .a = send }, "\x01\x0a" },
	[0x0b] = { .act = { .a = send }, "\x01\x0b" },
	[0x0c] = { .act = { .a = send }, "\x01\x0c" },
	[0x0d] = { .act = { .v = send_cr }, NULL },
	[0x0e] = { .act = { .a = send }, "\x01\x0e" },
	[0x0f] = { .act = { .a = send }, "\x01\x0f" },

	[0x10] = { .act = { .a = send }, "\x01\x10" },
	[0x11] = { .act = { .a = send }, "\x01\x11" },
	[0x12] = { .act = { .a = send }, "\x01\x12" },
	[0x13] = { .act = { .a = send }, "\x01\x13" },
	[0x14] = { .act = { .a = send }, "\x01\x14" },
	[0x15] = { .act = { .a = send }, "\x01\x15" },
	[0x16] = { .act = { .a = send }, "\x01\x16" },
	[0x17] = { .act = { .a = send }, "\x01\x17" },
	[0x18] = { .act = { .a = send }, "\x01\x18" },
	[0x19] = { .act = { .a = send }, "\x01\x19" },
	[0x1a] = { .act = { .a = send }, "\x01\x1a" },
	[0x1b] = { .act = { .a = send }, "\x01\x1b" },
	[0x1c] = { .act = { .a = send }, "\x01\x1c" },
	[0x1d] = { .act = { .a = send }, "\x01\x1d" },
	[0x1e] = { .act = { .a = send }, "\x01\x1e" },
	[0x1f] = { .act = { .a = send }, "\x01\x1f" },

	[0x20] = { .act = { .a = send }, "\x01\x20" },
	[0x21] = { .act = { .a = send }, "\x01\x21" },
	[0x22] = { .act = { .a = send }, "\x01\x22" },
	[0x23] = { .act = { .a = send }, "\x01\x23" },
	[0x24] = { .act = { .a = send }, "\x01\x24" },
	[0x25] = { .act = { .a = send }, "\x01\x25" },
	[0x26] = { .act = { .a = send }, "\x01\x26" },
	[0x27] = { .act = { .a = send }, "\x01\x27" },
	[0x28] = { .act = { .a = send }, "\x01\x28" },
	[0x29] = { .act = { .a = send }, "\x01\x29" },
	[0x2a] = { .act = { .a = send }, "\x01\x2a" },
	[0x2b] = { .act = { .a = send }, "\x01\x2b" },
	[0x2c] = { .act = { .a = send }, "\x01\x2c" },
	[0x2d] = { .act = { .a = send }, "\x01\x2d" },
	[0x2e] = { .act = { .a = send }, "\x01\x2e" },
	[0x2f] = { .act = { .a = send }, "\x01\x2f" },

	[0x30] = { .act = { .a = send }, "\x01\x30" },
	[0x31] = { .act = { .a = send }, "\x01\x31" },
	[0x32] = { .act = { .a = send }, "\x01\x32" },
	[0x33] = { .act = { .a = send }, "\x01\x33" },
	[0x34] = { .act = { .a = send }, "\x01\x34" },
	[0x35] = { .act = { .a = send }, "\x01\x35" },
	[0x36] = { .act = { .a = send }, "\x01\x36" },
	[0x37] = { .act = { .a = send }, "\x01\x37" },
	[0x38] = { .act = { .a = send }, "\x01\x38" },
	[0x39] = { .act = { .a = send }, "\x01\x39" },
	[0x3a] = { .act = { .a = send }, "\x01\x3a" },
	[0x3b] = { .act = { .a = send }, "\x01\x3b" },
	[0x3c] = { .act = { .a = send }, "\x01\x3c" },
	[0x3d] = { .act = { .a = send }, "\x01\x3d" },
	[0x3e] = { .act = { .a = send }, "\x01\x3e" },
	[0x3f] = { .act = { .a = send }, "\x01\x3f" },

	[0x40] = { .act = { .a = send }, "\x01\x40" },
	[0x41] = { .act = { .a = send }, "\x01\x41" },
	[0x42] = { .act = { .a = send }, "\x01\x42" },
	[0x43] = { .act = { .a = send }, "\x01\x43" },
	[0x44] = { .act = { .a = send }, "\x01\x44" },
	[0x45] = { .act = { .a = send }, "\x01\x45" },
	[0x46] = { .act = { .a = send }, "\x01\x46" },
	[0x47] = { .act = { .a = send }, "\x01\x47" },
	[0x48] = { .act = { .a = send }, "\x01\x48" },
	[0x49] = { .act = { .a = send }, "\x01\x49" },
	[0x4a] = { .act = { .a = send }, "\x01\x4a" },
	[0x4b] = { .act = { .a = send }, "\x01\x4b" },
	[0x4c] = { .act = { .a = send }, "\x01\x4c" },
	[0x4d] = { .act = { .a = send }, "\x01\x4d" },
	[0x4e] = { .act = { .a = send }, "\x01\x4e" },
	[0x4f] = { .act = { .a = send }, "\x01\x4f" },

	[0x50] = { .act = { .a = send }, "\x01\x50" },
	[0x51] = { .act = { .a = send }, "\x01\x51" },
	[0x52] = { .act = { .a = send }, "\x01\x52" },
	[0x53] = { .act = { .a = send }, "\x01\x53" },
	[0x54] = { .act = { .a = send }, "\x01\x54" },
	[0x55] = { .act = { .a = send }, "\x01\x55" },
	[0x56] = { .act = { .a = send }, "\x01\x56" },
	[0x57] = { .act = { .a = send }, "\x01\x57" },
	[0x58] = { .act = { .a = send }, "\x01\x58" },
	[0x59] = { .act = { .a = send }, "\x01\x59" },
	[0x5a] = { .act = { .a = send }, "\x01\x5a" },
	[0x5b] = { .act = { .a = send }, "\x01\x5b" },
	[0x5c] = { .act = { .a = send }, "\x01\x5c" },
	[0x5d] = { .act = { .a = send }, "\x01\x5d" },
	[0x5e] = { .act = { .a = send }, "\x01\x5e" },
	[0x5f] = { .act = { .a = send }, "\x01\x5f" },

	[0x60] = { .act = { .a = send }, "\x01\x60" },
	[0x61] = { .act = { .a = send }, "\x01\x61" },
	[0x62] = { .act = { .a = send }, "\x01\x62" },
	[0x63] = { .act = { .a = send }, "\x01\x63" },
	[0x64] = { .act = { .a = send }, "\x01\x64" },
	[0x65] = { .act = { .a = send }, "\x01\x65" },
	[0x66] = { .act = { .a = send }, "\x01\x66" },
	[0x67] = { .act = { .a = send }, "\x01\x67" },
	[0x68] = { .act = { .a = send }, "\x01\x68" },
	[0x69] = { .act = { .a = send }, "\x01\x69" },
	[0x6a] = { .act = { .a = send }, "\x01\x6a" },
	[0x6b] = { .act = { .a = send }, "\x01\x6b" },
	[0x6c] = { .act = { .a = send }, "\x01\x6c" },
	[0x6d] = { .act = { .a = send }, "\x01\x6d" },
	[0x6e] = { .act = { .a = send }, "\x01\x6e" },
	[0x6f] = { .act = { .a = send }, "\x01\x6f" },

	[0x70] = { .act = { .a = send }, "\x01\x70" },
	[0x71] = { .act = { .a = send }, "\x01\x71" },
	[0x72] = { .act = { .a = send }, "\x01\x72" },
	[0x73] = { .act = { .a = send }, "\x01\x73" },
	[0x74] = { .act = { .a = send }, "\x01\x74" },
	[0x75] = { .act = { .a = send }, "\x01\x75" },
	[0x76] = { .act = { .a = send }, "\x01\x76" },
	[0x77] = { .act = { .a = send }, "\x01\x77" },
	[0x78] = { .act = { .a = send }, "\x01\x78" },
	[0x79] = { .act = { .a = send }, "\x01\x79" },
	[0x7a] = { .act = { .a = send }, "\x01\x7a" },
	[0x7b] = { .act = { .a = send }, "\x01\x7b" },
	[0x7c] = { .act = { .a = send }, "\x01\x7c" },
	[0x7d] = { .act = { .a = send }, "\x01\x7d" },
	[0x7e] = { .act = { .a = send }, "\x01\x7e" },
	[0x7f] = { .act = { .a = send }, "\x01\x7f" },
};
