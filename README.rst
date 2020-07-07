Introduction
============

smtx -- Simple Modal Terminal Multiplexer

smtx is designed for simplicity in the implementation and ease of use
in a limited environment (eg, on a small physical device).

Quickstart
==========

smtx is a window manager.  When first started, smtx creates a single
window with a pty running the program specified in SHELL.  The window
that is created will fill the physical screen, and the underlying pty
that it is viewing will have a width at least as much as that specified
at startup (default is 80).  The width of the underlying pty can be
changed at runtime.  The `CMD` keysequence (default is
`CTRL+g`) will put smtx in `command` mode, in which key sequences are
interpreted to manipulate the windows.  Transition back to `keypress`
mode by pressing `RETURN` or `CMD`.  Pressing `RETURN` transitions
mode without sending a key to the underlying pty, while pressing `CMD`
transitions and sends the keystroke.  To scroll a window horizontally,
use the `scrollh` command (by default bound to `<` and `>`).

Windows
=======

New windows are created in `command` mode with `create`, which is by
default bound to the keystrokes `c` and `C`.  New windows will split
the currently focused window.
To switch among the windows use `j`, `k`, `l`, and `h`.

Usage
=====

Usage is simple::

    smtx [-c ctrl-key] [-s history-size] [-t terminal-type] [-v] [-w width]

The `-t` flag tells smtx what terminal type to advertise itself as.
(This just controls what the `TERM` environment variable is set to.)

The `-c` flag lets you specify a keyboard character to use as the "command
prefix" for smtx when modified with *control* (see below).  By default,
this is `g`.

The `-s` flag controls the amount of scrollback saved for each terminal.

The `-w` flag sets the minimum width for newly created ptys  (default is 80).

Ths `-v` flag causes smtx to print its version and exit.

Once inside smtx, things pretty much work like any other terminal.  However,
smtx lets you split up the terminal into multiple virtual terminals.

At any given moment, exactly one virtual terminal is *focused*.  It is
to this terminal that keyboad input is sent.  The focused terminal is
indicated by the location of the cursor.

The following commands are recognized in smtx when in command mode:

h/j/k/l/Up/Down/Left/Right Arrow
    Focus the virtual terminal above/below/to the left of/to the right of
    the currently focused terminal.

c / C
    Split the focused virtual terminal in half horizontally/vertically,
    creating a new virtual terminal to the right/below.  The old virtual
    terminal retains the focus.

b/f
    Scroll the screen back/forward half a screenful, or recenter the
    screen on the actual terminal.

W
    Set the width of the focused pty.  eg, to set the width of the currently
    focused pty to 120, enter command mode and type `120W`

[0-9] Set a command count.

(Note that these keybindings can be changed at compile time, and that the
above list is incomplete and subject to change.)

Compatibility
=============

The `smtx` Terminal Types
------------------------
smtx comes with a terminfo description file called smtx.ti.  This file
describes all of the features supported by smtx.

If you want to install this terminal type, use the `tic` compiler that
comes with ncurses::

    tic -s -x smtx.ti


Using these terminfo entries allows programs to use the full power of smtx's
terminal emulation, but it is entirely optional. A primary design goal
of smtx was for it to be completely usable on systems that didn't have the
smtx terminfo entry installed. By default, smtx advertises itself as the
widely-available `screen-bce` terminal type.

Copyright and License
=====================

Copyright 2016-2019 Rob King <jking@deadpixi.com>

Copyright 2020 William Pursell <william.r.pursell@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
