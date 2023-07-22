Introduction
============

smtx -- Simple Modal Terminal Multiplexer

Designed for simplicity and ease of use in a limited environment (eg,
on a small physical device), smtx provides provides ptys that are wider
than the physical device and the ability to have multiple views in
different locations of the pty.

https://terminalizer.com/view/28083a105080
https://terminalizer.com/view/e38467005081

Quickstart
==========

When first started, `smtx` creates a single window with a pty running
the program specified in SHELL.  The window that is created will fill
the physical screen, and the underlying pty that it is viewing will have
a width of at least 80.  The `CMD` keysequence (default is `CTRL+g`)
will put smtx in `command` mode in which key sequences are interpreted
to manipulate the windows.  Press `RETURN` to exit command mode.
Press `CMD` to exit command mode and send `CMD` to the underlying pty.
In command mode, create new windows using `c` or `C`.  Use `hjkl` to
navigate between windows.  Use `<` and `>` to horizontally scroll the
underlying pty.  Most commands accept a count, so you could create 5 new
windows with `5c` and move down N times with `Nj`. (In the follwoing, `N`
will represent an arbitrary integer).  Choose one of the preset layouts
with `Nv`.  To attach the current window to a different pty, use `Na`.
To swap the pty in the current window with the pty in a different
window, use `NS`.  To move the focus to the window with pty N, use `Ng`.
To transpose the orientation of the current window, use `T`.  To
close the currently focused window (the underlying pty is not
affected), use `x`.  To destroy all of the ptys and exit, use `0x`.

Features
========

smtx is modal, so you can enter multiple commands from command mode without
typing the `CMD` keysequence multiple times.  From command mode, you can create 5 new windows with `ccccc`, or `5c`
There are several preset layouts, so you can get a layout of 5 windows
with `5v`.
You can recursively convert an axial split to a sagittal split with `T` (transpose),
and you can discard the currently focused window and all its children
with `x`.  To attach the current window to a different pty, use `a`.
To swap the pty of the current window with a pty in a different window,
use `s`.  Change the width of the pty in the currectly focused window
with `W`.

You can also generate a window layout with an osc sequence.
To generate a layout with seven windows, you could do::

    printf '\033]60;.5:.5 .25:.66 .5:.66 .5:.83 .5:1 1:.25 1:1\007'

where each coordinate pair represents the lower right hand corner of the window
as a fraction of the full screen (note that order matters).

Windows
=======

New windows are created in `command` mode with `create`, which is by
default bound to the keystrokes `c` and `C`.  To navigate  among the
windows use `j`, `k`, `l`, and `h`.  The navigation is not directly related to
the physical layout on the screen, but navigates the tree structure of the
extant windows.  Each window belongs to one "canvas",
which is simply a node in the tree used to keep track of windows.
The root canvas fills the entire physical screen and contains the window
in the upper left.  Each canvas can have 0, 1, or 2 children.  If the root
window is split on the axial plane (ie, split top to bottom), the root
canvas is said to be of "type 0" and has one child.  If the root window
is split on the sagittal plane (ie, split left to right), the root
canvas is of "type 1".  That is, a layout with 3 windows in which the
left half of the screen is full height and the right half of the screen
is split into 2 half-height windows indicates a root canvas of type 1.
In that layout, the root canvas has one child (the right half of the screen),
and that child (which is a type 0 canvas) has one child.  The window navigation
follows this tree structure naturally.
`h` and `l` move the cursor to the left and right child of the
currently focused canvas.  `k` moves to the parent canvas, and `j` moves to the
primary child; left for type 0, and right for type 1.


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
    Move the focus to the virtual terminal in the window of the canvas
    that is the child/parent of
    the currently focused terminal.

c / C
    Split the focused virtual terminal in half horizontally/vertically,
    creating a new virtual terminal to the right/below.  The old virtual
    terminal retains the focus.

b/f
    Scroll the screen back/forward half a screenful, or recenter the
    screen on the actual terminal.

-/|
    Change the size of the current window to be the specifiec percentage
    of the enclosing canvas. eg, '25|' will make the current window use
    25% of the horizontal space of the canvas.

=   Recursively rebalance windows.  By itself, rebalance in both
    directions.  1= and 2= will rebalance only horizontally or
    vertically, respectively.

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
terminal emulation, but it is entirely optional.  If the terminfo file is
not installed, smtx will use reasonable defaults.

Copyright and License
=====================

Copyright 2016-2019 Rob King <jking@deadpixi.com>

Copyright 2020-2023 William Pursell <william.r.pursell@gmail.com>

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
