SD Tap Board Server
===================

The tap board is attached to a relatively low-powered, headless ARM-based
machine.  It interfaces to a more powerful server by means of a TCP/UDP
connection pair.  Control information flows over the TCP channel, while
bulk data transfers flow over the UDP channel.


Building the Code
-----------------

To compile, ensure CC is set to your cross-compiler.  Then simply type
"make".  The build system will kick out a program called "spi" that you can
then copy to the target board.

Running the Program
-------------------
The program accepts no arguments.  Simply run "./spi" on the target board.
It will start up a server and print out a message:

    root@kovan:~# ./spi 
    Listening on port 7283

On your client machine, connect either using the GUI frontend, or use a
console program such as "telnet" or "netcat".  You should get a 'cmd>'
prompt:

    smc@edmond ~> nc kovan.local 7283
    cmd> 

To receive data, listen to UDP port 17283.  The GUI will automatically do
this, but if you're running in a console session, you can use netcat in a
different window:

    smc@edmond ~/C/sd> nc -l -u 17283 | hexdump -C

Brief Command Set Explanation
-----------------------------

To get a list of commands, type "??" or "help".  The major commands are
described here.  A command will have a '*' next to it in the help screen if
it is not yet implemented.

"rc" -- Reset the card and reinitialize it.  This must be the first command
you run, as the board starts in a powered-off state.  If you attempt to
send any other commands, the board could time out.

"ci" / "cs" -- Returns the card's CID or CSD respectively.  Does not decode
the values, but returns the 16-byte instructions on both the data and
command channels.

"so [arg]" -- Sets the sector offset, in terms of 512-byte blocks.
Defaults to sector 0 at reset.

"rs" -- Read current sector.  Reads a 512-byte block fro the current sector
offset.  This gets placed into the "read buffer" and also gets sent out
the data channel.

"cb" -- Copes the read buffer into the write buffer.  If you want to test
single-bit changes, read from a given sector, copy it to the write buffer,
modify it with "bo" and "sb", and write it back out with "ws".

"bo [arg]" -- Sets write buffer pointer offset to the specified argument.
This is within the 512-byte write buffer size.

"sb [arg]" -- Sets the value of the write-buffer at the current buffer
pointer offset to the specified arg, and increments the buffer pointer
offset.  Use this for changing individual bytes in the write buffer.

"bc" -- Returns the write buffer contents over the data channel.

"ws" -- Write the current contents of the write buffer to the current
sector offset.


Data Channel Format
-------------------

The data channel data is subject to change, and likely will change in order
to accommodate more data such as timestamps and sequence numbers.

All packets start with an identifier, based on the following enum in sd.h:

    enum net_data_types {
        NET_DATA_UNKNOWN = 0,
        NET_DATA_NAND = 1,
        NET_DATA_SD = 2,
        NET_DATA_CMD = 3,
        NET_DATA_BUFFER_OFFSET = 4,
        NET_DATA_BUFFER_CONTENTS = 5,
    };

The remaineder of the packet is type-dependent.  For example, NET_DATA_SD
will contain an additional 512 bytes (for a total of 513), and
NET_DATA_NAND will contain an additional two bytes (for a total of three).
