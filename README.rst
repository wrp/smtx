Introduction
============

smtx is the Simple Modal Terminal Multiplexer

Quickstart
==========

smtx is a window manager.  When first started, smtx creates a single
window with a pty running the program specified in SHELL.  Entering
the `MOD` keysequence (default is `CTRL+g`) will put smtx in `command`
mode, in which key sequences are interpreted to manipulate the
windows.  To transition back to `keypress` mode, you may press
`RETURN`, `MOD`, or `ESC`.  Pressing `ESC` or `RETURN` transitions
mode without sending a key to the underlying pty, while pressing
`MOD` transitions and sends the keystroke.  To quit, use `qq` from
command mode.

Windows
=======

New windows are created in `command` mode with `c` and closed with `xx`.
To switch among the windows use `j`, `k`, and `1`, `2`, etc.

Usage
=====

Usage is simple::

    smtx [-T NAME] [-t NAME] [-c KEY]

The `-T` flag tells smtx to assume a different kind of host terminal.

The `-t` flag tells smtx what terminal type to advertise itself as.
Note that this doesn't change how smtx interprets control sequences; it
simply controls what the `TERM` environment variable is set to.

The `-c` flag lets you specify a keyboard character to use as the "command
prefix" for smtx when modified with *control* (see below).  By default,
this is `g`.

Once inside smtx, things pretty much work like any other terminal.  However,
smtx lets you split up the terminal into multiple virtual terminals.

At any given moment, exactly one virtual terminal is *focused*.  It is
to this terminal that keyboad input is sent.  The focused terminal is
indicated by the location of the cursor.

The following commands are recognized in smtx, when preceded by the command
prefix (by default *ctrl-g*):

Up/Down/Left/Right Arrow
    Focus the virtual terminal above/below/to the left of/to the right of
    the currently focused terminal.

o
    Focus the previously-focused virtual terminal.

h / v
    Split the focused virtual terminal in half horizontally/vertically,
    creating a new virtual terminal to the right/below.  The new virtual
    terminal is focused.

w
    Delete the focused virtual terminal.  Some other nearby virtual
    terminal will become focused if there are any left.  smtx will exit
    once all virtual terminals are closed.  Virtual terminals will also
    close if the program started inside them exits.

l
    Redraw the screen.

PgUp/PgDown/End
    Scroll the screen back/forward half a screenful, or recenter the
    screen on the actual terminal.

That's it.  There aren't dozens of commands, there are no modes, there's
nothing else to learn.

(Note that these keybindings can be changed at compile time.)

Compatibility
=============

The `smtx` Terminal Types
------------------------
smtx comes with a terminfo description file called smtx.ti.  This file
describes all of the features supported by smtx.

If you want to install this terminal type, use the `tic` compiler that
comes with ncurses::

    tic -s -x smtx.ti

or simply::

    make install-terminfo

This will install the following terminal types:

smtx
    This terminal type supports all of the features of smtx, but with
    the default 8 "ANSI" colors only.

smtx-256color
    Note that smtx is not magic and cannot actually display more colors
    than the host terminal supports.

smtx-noutf
    This terminal type supports everything the smtx terminal type does,
    but does not advertise UTF8 capability.

That command will compile and install the terminfo entry.  After doing so,
calling smtx with `-t smtx`::

    smtx -t smtx

will instruct programs to use that terminfo entry.
You can, of course, replace `smtx` with any of the other above terminal
types.

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
