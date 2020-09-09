/*
 * Copyright 2017 - 2019 Rob King <jking@deadpixi.com>
 * Copyright 2020 - William Pursell <william.r.pursell@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The test suite conspicuously avoids including smtx.h so that the
 * interface is clear.  The functions declared here are the only
 * access the tests have to the program.
 */

extern size_t describe_layout(char *, ptrdiff_t, unsigned);
extern size_t describe_row(char *, size_t, int, unsigned);
extern size_t describe_state(char *desc, size_t siz);
extern int smtx_main(int, char **);

#define CTL(x) ((x) & 0x1f)
