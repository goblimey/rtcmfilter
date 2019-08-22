/*
 * messagehandler.c
 *
 *  Created on: 21 Aug 2019
 *      Author: simon Ritchie
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WINDOWSVERSION
  #include <winsock2.h>
  #include <io.h>
  #include <sys/stat.h>
  #include <windows.h>
  typedef SOCKET sockettype;
  typedef u_long in_addr_t;
  typedef size_t socklen_t;
  typedef u_short uint16_t;
#else
  typedef int sockettype;
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <sys/termios.h>
  #define closesocket(sock) close(sock)
  #define INVALID_HANDLE_VALUE -1
  #define INVALID_SOCKET -1
#endif

#include "rtcmfilter.h"

#define MAX_RTCM_MESSAGES_TO_DISPLAY 50
#define MAX_OTHER_MESSAGES_TO_DISPLAY 50
#define LENGTH_OF_HEADER 3
#define LENGTH_OF_CRC 3
#define STATE_EATING_MESSAGES 0
#define STATE_PROCESSING_START_OF_RTCM_MESSAGE 1
#define STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE 2
#define STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER 3

extern int verboseMode;
extern int addNewline;

static int numberOfRtcmMessagesDisplayed = 0;
static int numberOfOtherMessagesDisplayed = 0;
static unsigned int state = STATE_EATING_MESSAGES;

// Get the length of the RTCM message.  The three bytes of the header form a big-endian
// 24-bit value. The bottom ten bits is the message length.
unsigned int getRtcmLength(unsigned char * messageBuffer, unsigned int bufferLength) {

    if (bufferLength < 3) {
        // The message starts very near the end of the buffer, so we can't calculate
        // the length until we get the next one.

    	if (verboseMode > 0 && displayingRtcmMessages()) {
    		fprintf(stderr, "getRtcmLength(): buffer too short to get message length - %d\n",
    				bufferLength);
    	}
        return 0;
    }

    unsigned int rtcmLength = (messageBuffer[1]<<8|(messageBuffer[2]&0x3ff));

    return rtcmLength;
}

// Get the value of the Field With No Name (FWNN).  The three bytes of the header form
// a big-endian 24-bit value.  The first byte is 0xd3.  The bottom ten bits are the
// message length .  At present I don't know the purpose of the other six bits.  I've seen
// a vague reference to it being a port number.  In this tool we only display the value,
// we don't use it for anything.
//
// In rtklib rtcm.c, this field is shown as always zero.
unsigned int getFWNN(unsigned char * messageBuffer, unsigned int bufferLength) {

    if (bufferLength < 2) {
        // The message starts very near the end of the buffer, so we can't calculate
        // the length until we get the next one.
        return -1;
    }

    unsigned int fwnn = ((messageBuffer[1] & 0x3) >> 2);

    return fwnn;
}

int displayingBuffers() {
	return (numberOfRtcmMessagesDisplayed <= MAX_RTCM_MESSAGES_TO_DISPLAY ||
			numberOfOtherMessagesDisplayed <= MAX_OTHER_MESSAGES_TO_DISPLAY);
}

int displayingRtcmMessages() {
	return (numberOfRtcmMessagesDisplayed <= MAX_RTCM_MESSAGES_TO_DISPLAY);
}

int displayingOtherMessages() {
	return (numberOfOtherMessagesDisplayed <= MAX_OTHER_MESSAGES_TO_DISPLAY);
}

Buffer * createBuffer(size_t length) {
	Buffer * buffer = malloc(sizeof(Buffer));
	buffer->length = length;
	buffer->content = malloc(length);
	return buffer;
}

void freeBuffer(Buffer * buffer) {
	if (buffer == NULL) {
		return;
	}

	if (buffer->content != NULL) {
		free(buffer->content);
	}

	free(buffer);
}

// diaplayBuffer() displays the contents of a buffer and the given buffer processing state.
void displayBuffer(Buffer * buffer, int state) {

	if (verboseMode > 0 && displayingBuffers()) {

		if (buffer == NULL) {
			fprintf(stderr, "buffer is NULL\n");
		}

		switch (state) {
		case STATE_EATING_MESSAGES:
			fprintf(stderr, "\nBuffer length %ld, state eating messages:",
					buffer->length);
			break;
		case STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE:
			fprintf(stderr, "\nBuffer length %ld, state processing continuation of RTCM message:",
					buffer->length);
			break;
		case STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER:
			fprintf(stderr, "\nBuffer length %ld, state processing continuation of RTCM message with incomplete header:",
					buffer->length);
			break;
		case STATE_PROCESSING_START_OF_RTCM_MESSAGE:
			fprintf(stderr, "\nBuffer length %ld, state processing start of RTCM message:",
					buffer->length);
			break;
		default:
			fprintf(stderr, "\nBuffer length %ld, unknown state:",
					buffer->length);
			break;
		}

		if (buffer->length > 0) {
			for (int j = 0; j < buffer->length; j++) {
				if ((j % 32) == 0) {
					putc('\n', stderr);
				}
				fprintf(stderr, "%02x ", buffer->content[j]);
			}
			putc('\n', stderr);
			putc('\n', stderr);
			for (int j = 0; j < buffer->length; j++) {
				if ((j % 32) == 0) {
					putc('\n', stderr);
				}
				putc(buffer->content[j], stderr);
			}
			putc('\n', stderr);
		} else {
			fprintf(stderr, "empty buffer\n");
		}
	}
}

// Display the RTCM message.
void displayMessage(unsigned char * buffer, unsigned int bufferLength) {
    if (buffer == NULL) {
        return;
    }
    if (bufferLength == 0) {
        return;
    }

    unsigned int messageLength = getRtcmLength(buffer, bufferLength);

    if (messageLength == 0) {
    	fprintf(stderr, "displayMessage(): buffer too short to get message length - %d\n", bufferLength);
    	return;
    }

    unsigned int totalMessageLength = messageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;

    if (bufferLength < totalMessageLength) {
        fprintf(stderr, "short buffer: length %d totalMessageLength %d\n",
            bufferLength, totalMessageLength);
        for (unsigned int i = 0; i < bufferLength; i++) {
            putc(buffer[i], stderr);
        }
        fprintf(stderr, "\n----------------------------------------------------------\n");
        return;
    }

    // The buffer contains 03d, 2 bytes including the message length, the message and
    // 3 bytes CRC.
    unsigned int crc = buffer[messageLength+3]<<16|
        buffer[messageLength+4]<<8|buffer[messageLength+5];

    unsigned int fwnn = getFWNN(buffer, bufferLength);

    // Get the message type from the message body.
    unsigned int type = getbitu(buffer + 3, 24, 12);

    fprintf(stderr, "\nRTCM message fwnn %d length %d type %d CRC 0x%02x%02x%02x",
    		fwnn, messageLength, type,
    		buffer[messageLength+3], buffer[messageLength+4], buffer[messageLength+5]);
    for (unsigned int i = 0; i < totalMessageLength; i++) {
        if ((i % 32) == 0) {
            putc('\n', stderr);
        }
        fprintf(stderr, "%02x ", buffer[i]);
    }
    fprintf(stderr, "\n----------------------------------------------------------\n");
}

// addMessageFragmentToBuffer adds a message fragment to a buffer, which may be empty or may
// already have some contents.
Buffer * addMessageFragmentToBuffer(Buffer * buffer, unsigned char * fragment, size_t fragmentLength) {
	unsigned char * startOfMessageInBuffer= NULL;
	if (buffer == NULL || buffer->content == NULL) {
		buffer = createBuffer(fragmentLength);
		memcpy(buffer->content, fragment, fragmentLength);
	} else {
		buffer->content = realloc(buffer->content, buffer->length + fragmentLength);
		memcpy(buffer->content + buffer->length, fragment, fragmentLength);
		buffer->length += fragmentLength;
	}

	return buffer;
}

// Given a Buffer containing messages and message fragments, extract any RTCM messages or
// fragments of RTCM messages and return a Buffer containing just them.
Buffer * getRtcmMessages(Buffer inputBuffer) {
	static size_t rtcmBufferLength = 0;			// length of the allocated RTCM buffer.
	static size_t rtcmMessageLength = 0;		// The message length from the header.
	static size_t totalRtcmMessageLength = 0;	// length of leader, message and CRC.
	static size_t rtcmMessageBytesSent = 0;		// RTCM message bytes sent in previous call(s).

	Buffer * outputBuffer = NULL;
	unsigned int i = 0;
	// Space to handle an edge case where we need to hold onto a message fragment across
	// several buffers.
	static unsigned char headerContent[3];
	static Buffer headerBuffer;
	headerBuffer.content = headerContent;
	headerBuffer.length = 3;

	if (inputBuffer.length == 0) {
		return NULL;
	}
	if (inputBuffer.content == NULL) {
		return NULL;
	}

	/*
	 * getRtcmMessages() returns a buffer containing RTCM messages.  The input is a Buffer containing
	 * messages and fragments of messages of many types.  The input buffer is fixed length so it's
	 * typically a fragment which is the end of a message started in an earlier buffer, a string of
	 * complete messages and a fragment which is the start of a message to be continued in the next
	 * buffer(s).  Variations on that include a buffer which starts with the start of a message, a buffer
	 * which is just one fragment, and so on.  Messages may be separated by null bytes and/or line breaks.
	 * A line break may be a newline or a Carriage Return Newline sequence (if the sending system is MS
	 * Windows).  A message may or may not be RTCM. If the buffer contains two adjacent RTCM messages,
	 * there does not have to be any separator between them.
	 *
	 * The method takes the input buffer, scans it for RTCM messages, copies those to the output buffer
	 * and discards anything else.  Since messages can span many buffers, some state must be preserved
	 * between input buffers.
	 *
	 * The design assumes that the output buffer is longer than the input buffer, so it should never
	 * overflow.
	 *
	 * An RTCM message is a stream of bytes with a 24-bit big-endian header, a variable-length message
	 * and a 24-bit big-endian Cyclic Redundancy Check (CRC) value.  The header has 0xd3 at the top,
	 * and the bottom ten bits are the message length.  The purpose of the other six bits (the "Field
	 * With No Name") is currently unknown to me.  For example:
	 *
	 *     D3 00 13 3E D7 D3 02 02 98 0E DE EF 34 B4 BD 62 AC 09 41 98 6F 33 36 0B 98
     *     -header-  1           5             10             15          19 ---CRC--
     *
     * The message contents is binary and in this case it happens to contain another D3 byte.
     *
     * That example comes from  https://portal.u-blox.com/s/question/0D52p00008HKDWQCA5/decoding-rtcm3-message
     * and that posting also refers to a longer message.
     *
     * The format of the RTCM messages is defined in a standard.  It's not open-source and I haven't
     * bought a copy.  The messages contain information such as the positions of satellites.  There are
     * several numbered message types, so the first part of the message is probably the type number,
     * but I have no idea of the format.  There may be some clues in the open-source library RTKLIB,
     * but I haven't looked at that yet.
	 *
	 * The tool reads a stream of messages in non-blocking mode using a bounded input buffer, so
	 * typically it contains a fragment of a message, which continues the last message from the last buffer,
	 * followed by a list of complete messages, followed by another fragment which is continued in the next
	 * buffer.
	 *
	 * To keep track of state between buffers there is a state machine with three states representing:
	 * not processing an RTCM message; processing the start (possibly all) of an RTCM message; processing
	 * the continuation of an RTCM message.  There are also static variables to track how much of a message
	 * that spans many buffers has already been processed.
	 *
	 * Some messages (including RTCM) are binary so the buffer may contain several null bytes, which
     * means (a) you can't treat the buffer as a simple string and (b) messages may contain what look
     * like newlines or 0xd3, but which are just part of the data.  Also, in a noisy environment we may
     * have to assume that characters could be dropped.  To guard against all this, the function should
     * check the CRC and ignore any messages that are badly formatted, but at present it doesn't.  The
     * destination caster should have some error checking of its own, so this should not be catastrophic.
	 */

	if (verboseMode > 0 && displayingBuffers()) {

		displayBuffer(&inputBuffer, state);
	}

	// Scan the buffer for RTCM messages.
	while (i < inputBuffer.length) {

		// This could be a case statement, except that we use break to escape from the while loop.
		if (state == STATE_EATING_MESSAGES) {
			// Process messages which are not RTCM by ignoring them.  If we see the start of an
			// RTCM message, stop eating.
			if (verboseMode > 0 && displayingOtherMessages() && i == 0 &&
					inputBuffer.content[i] != 0xd3) {
				fprintf(stderr, "\neating messages from position 0\n");
			}
			if (inputBuffer.content[i] == 0xd3) {
				// Stop eating and process the RTCM message.
				if (verboseMode > 0 && (displayingRtcmMessages() || displayingOtherMessages())) {
					fprintf(stderr, "\nFound RTCM message, stop eating - position %d\n", i);
				}
				state = STATE_PROCESSING_START_OF_RTCM_MESSAGE;
				continue;
			} else {
				// Eat.
				if (verboseMode > 0 && displayingOtherMessages()) {
					putc(inputBuffer.content[i], stderr);
				}
				i++;
				continue;
			}

		} else if (state == STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE ||
				state == STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER) {

			// We are processing a buffer that starts with the continuation of a long RTCM
			// message.  Either the whole of the buffer contains one fragment  of the message or
			// it starts with part of the message, followed by other messages, RTCM or not.

			if (headerBuffer.content == NULL) {
				// continuation with no start.  Should never happen.
				fprintf(stderr, "warning: processing continuation of message, but without a start - should never happen -  buffer position %d\n", i);
				i++;
				if (verboseMode > 0 && displayingOtherMessages()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				numberOfOtherMessagesDisplayed++;
				state = STATE_EATING_MESSAGES;
				continue;
			}

			if (i > 0) {
				fprintf(stderr,
						"warning: processing continuation of message, but this should only happen at the start of a buffer - i=%d\n", i);
				i++;
				if (verboseMode > 0 && displayingOtherMessages()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				numberOfOtherMessagesDisplayed++;
				state = STATE_EATING_MESSAGES;
				continue;
			}

			// This buffer starts with the continuation of an RTCM message.
			if (verboseMode > 0 && displayingRtcmMessages()) {
				if (state == STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE) {
					fprintf(stderr, "\nprocessing continuation of RTCM message\n");
				} else {
					fprintf(stderr, "\nprocessing continuation of RTCM message with incomplete header\n");
				}
			}

			if (state == STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER) {
				// Edge case.  The message started very close to the end of the last buffer.  Some
				// of the message length values are in the current buffer, so we can only get it
				// now.  Copy two characters from the input to the header buffer, which is enough
				// to complete it, then carry on as normal.
				if (verboseMode > 0 && displayingRtcmMessages()) {
					fprintf(stderr, "finding length of RTCM message from the continuation\n");
				}
				headerBuffer.content[headerBuffer.length-1] = inputBuffer.content[0];
				headerBuffer.content[headerBuffer.length] = inputBuffer.content[1];
				headerBuffer.length += 2;
				rtcmMessageLength = getRtcmLength(headerBuffer.content, headerBuffer.length);
				if (verboseMode > 0 && displayingRtcmMessages()) {
					fprintf(stderr, "\nGot the message length - %ld - switching to state processing continuation of RTCM message\n",
							rtcmMessageLength);
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, (size_t)2);
				// The total message is Leader, message, CRC.
				totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;

				// Adjust the input buffer
				inputBuffer.content += 2;
				inputBuffer.length -= 2;
				rtcmMessageBytesSent += 2;

				state = STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE;
				i += 2;   // Carry on processing this buffer.
			}

			// Either the buffer contains the rest of the message, maybe followed by
			// other messages, or it contains just part of one long message.
			// Note: normally, at this point i is guaranteed to be zero (ie we are at
			// the start of the input buffer).  If we are processing the edge case
			// where the previous buffer did not contain all of the header, we will
			// have consumed the first couple of characters in the buffer, then adjusted
			// the references.

			if (verboseMode > 0 && displayingRtcmMessages()) {
				fprintf(stderr, "\ntotal message length %ld\n", totalRtcmMessageLength);
			}

			size_t messageRemaining = totalRtcmMessageLength - rtcmMessageBytesSent;
			if (messageRemaining > inputBuffer.length) {
				// The whole of the buffer is part of a long RTCM message and there is
				// more of the message to come in another buffer.  Send the whole buffer.
				if (verboseMode > 0 && displayingRtcmMessages()) {
					fprintf(stderr, "the continuation buffer does not complete the RTCM message\n");
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, inputBuffer.length);
				rtcmMessageBytesSent += inputBuffer.length;
				// Finished processing the input buffer.  Still in continuation state.
				break;
			} else {
				// the buffer starts with the end of an RTCM message, possibly followed by
				// more messages of some type.  Copy the rest of this RTCM message and then
				// continue scanning.
				if (verboseMode > 0 && displayingRtcmMessages()) {
					fprintf(stderr, "\nThe first %ld characters of this continuation buffer completes the RTCM message\n",
							messageRemaining);
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, messageRemaining);
				rtcmMessageBytesSent = 0;	// Reset ready for the next trip.

				if (addNewline) {
					// Add a newline (only needed to help read the output during testing).
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, "\n", 1);
				}

				// The message is assembled and ready for display (if we are doing that).
				if (verboseMode > 0 && displayingRtcmMessages()) {
					fprintf(stderr, "\nOuput buffer now contains\n");
					displayBuffer(outputBuffer, state);
				}
				// now process the other messages in the buffer.
				i += messageRemaining;
				if (verboseMode > 0 && displayingOtherMessages()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				numberOfOtherMessagesDisplayed++;
				state = STATE_EATING_MESSAGES;
				continue;
			}

		} else if (state == STATE_PROCESSING_START_OF_RTCM_MESSAGE) {

			// Starting at this index, the input buffer contains an RTCM message or the first
			// fragment of an RTCM message which will be continued in the next buffer.  The
			// message is binary, variable length and in three parts:
			//     header containing 0xd3 plus two bytes containing the 10-bit message length
			//     the message
			//     three-byte CRC,
			// So the total message is (length+6) bytes long.

			if (verboseMode > 0 && displayingRtcmMessages()) {
				fprintf(stderr, "processing RTCM message - position %d\n", i);
			}

			if (inputBuffer.content[i] != 0xd3) {
				fprintf(stderr, "\nError: state is processing start of RTCM message but byte is %d - position %d\n",
						inputBuffer.content[i], i);
			}

			if (numberOfRtcmMessagesDisplayed <= MAX_RTCM_MESSAGES_TO_DISPLAY) {
				numberOfRtcmMessagesDisplayed++;
			}

			// Get the RTCM message.
			unsigned char * messageFragment = inputBuffer.content + i;
			rtcmMessageLength = getRtcmLength(messageFragment, inputBuffer.length - i);
			if (verboseMode > 0 && displayingRtcmMessages()) {
				fprintf(stderr, "\nFound RTCM message - position %d given message length %ld\n",
						i, rtcmMessageLength);
			}

			if (rtcmMessageLength > 0) {
				// Simple case.  We have enough header to get the message length.
				totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;
				rtcmMessageLength = 0;	// Ready for the next scan.
				if ((i + totalRtcmMessageLength) <= inputBuffer.length) {
					// The whole message is contained in this input buffer.  Copy it to the
					// output buffer.
					if (verboseMode > 0 && displayingRtcmMessages()) {
						fprintf(stderr, "processing complete RTCM message - position %d message length %ld\n",
								i, totalRtcmMessageLength);
						displayMessage(messageFragment, totalRtcmMessageLength);
					}
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, messageFragment, totalRtcmMessageLength);
					rtcmMessageBytesSent = 0;	// Reset ready for the next call.

					if (addNewline) {
						// Add a newline (only needed to help read the output during testing).
						outputBuffer = addMessageFragmentToBuffer(outputBuffer, "\n", 1);
					}

					// Display the messages collected so far.
					if (verboseMode > 0 && displayingRtcmMessages()) {
						fprintf(stderr, "\nOuput buffer now contains\n");
						displayBuffer(outputBuffer, state);
					}

					i += totalRtcmMessageLength;    // Move to the next message.
					numberOfOtherMessagesDisplayed++;
					state = STATE_EATING_MESSAGES;
					if (verboseMode > 0 && displayingOtherMessages()) {
						fprintf(stderr, "\nRTCM message processed.  Eating messages from position %d\n", i);
					}
					continue;

				} else {
					// The end of the buffer contains the first part of an RTCM message.
					// The rest is to follow.  Copy what we have.
					size_t lengthOfFirstPart = inputBuffer.length - i;
					if (verboseMode > 0 && displayingRtcmMessages()) {
						fprintf(stderr, "processing start of long RTCM message - position %d message length %ld got the first %ld bytes\n",
								i, totalRtcmMessageLength, lengthOfFirstPart);
					}
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, messageFragment, lengthOfFirstPart);
					rtcmMessageBytesSent = lengthOfFirstPart;
					state = STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE;
					// Finished processing this buffer.
					break;
				}

			} else {
				// Edge case - the buffer ends with a fragment of an RTCM message but the fragment is short
				// and doesn't contain a complete header, so we can't get the message length yet.  Take
				// what we have, put it in the output buffer for sending and record it in the header buffer
				// to be used in the next call.
				if (verboseMode > 0 && displayingRtcmMessages()) {
					fprintf(stderr, "processing start of long message with incomplete length value - position %d buffer length %ld\n",
							i, inputBuffer.length);
				}
				size_t fragmentLength = inputBuffer.length - i;
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, messageFragment, fragmentLength);
				rtcmMessageBytesSent = fragmentLength;
				state = STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER;
				// Finished processing this buffer.
				break;
			}
		} else {
			// Shouldn't happen.
			fprintf(stderr, "warning:  unknown state value %d at i=%d\n", state, i);
			i++;
			numberOfOtherMessagesDisplayed++;
			state = STATE_EATING_MESSAGES;
		}   // end if
	}  // end while

	return outputBuffer;
}

/* extract unsigned/signed bits ------------------------------------------------
* extract unsigned/signed bits from byte data
* args   : unsigned char *buff I byte data
*          int    pos    I      bit position from start of data (bits)
*          int    len    I      bit length (bits) (len<=32)
* return : extracted unsigned/signed bits
*
* Stolen from rtklib rtkcmn.c.
*-----------------------------------------------------------------------------*/
unsigned int getbitu(const unsigned char *buff, int pos, int len)
{
    unsigned int bits=0;
    int i;
    for (i=pos;i<pos+len;i++) bits=(bits<<1)+((buff[i/8]>>(7-i%8))&1u);
    return bits;
}
