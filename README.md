# rtcmfilter
Filters a stream of GPS correction messages and allows only RTCM messages through

This project arose from some work I did with the U-Blox F9P-ZED device.
It produces a stream of GPS correction messages in all sorts of formats.
The message stream can be fed into an NTRIP server to be sent on to an NTRIP caster
and used by moving GPS devices to get a more accurate position.
However, the device sends out all sorts of messages.
Some casters only want RTCM messages
and ignore the rest.
A large volume of junk messages uses up Internet bandwidth for no good reason
and has been known to cause the caster to fail.

The filter reads the stream of messages from the device and sends any
RTCM messages to its standard output channel.
It's assumed that standard output is a pipe with an NTRIP server at the other end.

The source code of the filter is based on the BKG NTRIP server.
Input handling is the same as that server.
The original is distributed under the GNU licence,
so this software is distributed under that too.

Build the filter under UNIX like so:

    make all

In this example, the GPS device is connected to one of the USB ports
of a Raspberry Pi and sending messages using the USB as a serial connection.
The filter and the ntrip server are running on the Raspberry Pi.
The filter reads the messages on the USB channel /dev/ttyACM0.
It copies any RTCM messages to stdout and discards any others.
It's conected via a pipe to the ntrip server,
which is scanning for messages on stdin.
It sends any incoming messages to a remote NTRIP caster.
The connection details for the caster are supplied through command line arguments.
Your connection details will be different from mine: 

    rtcmfilter -M 1 -i /dev/ttyACM0 -b 9600 | 
        ntripserver -M 3 -s - -a {castername} -p {casterport} -n {username} -c {password}

The magic that makes the server read messages on stdin is  the arguments "-M 3 -s -".
This uses a small change to the original server that I've made, so you have to use my version.
It's distibuted in another project in this repository.

My U-Blox device sends messages as a stream of text, some plain text, some binary, for example:

    $GBGSV,3,3,10,27,28,278,,28,81,309,,*4D
    $GNGLL,5117.65790,N,00019.41571,W,104548.00,A,A*68

RTCM messages are binary and begin with a 0xd3 byte.
They are variable length.
The second and third bytes contain the message length.
There does not have to be a newline character at the end.

To make the output a little more readable during testing,
the filter adds a newline after each RTCM message.
It's assumed that the target caster will ignore these.

The server opens an NTRIP connection to the caster and sends the incoming messages to it using that protocol. 