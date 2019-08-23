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
#define MAX_BUFFERS_TO_DISPLAY 50
#define LENGTH_OF_HEADER 3
#define LENGTH_OF_CRC 3
#define STATE_EATING_MESSAGES 0
#define STATE_PROCESSING_START_OF_RTCM_MESSAGE 1
#define STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE 2
#define STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER 3

extern int verboseMode;
extern int addNewline;

static int numberOfRtcmMessagesDisplayed = 0;
static int numberOfBuffersDisplayed = 0;
static unsigned int state = STATE_EATING_MESSAGES;

// Get the length of the RTCM message.  The three bytes of the header form a big-endian
// 24-bit value. The bottom ten bits is the message length.
unsigned int getRtcmLength(unsigned char * messageBuffer, unsigned int bufferLength) {

    if (bufferLength < 3) {
        // The message starts very near the end of the buffer, so we can't calculate
        // the length until we get the next one.

    	if (displaying()) {
    		fprintf(stderr, "getRtcmLength(): buffer too short to get message length - %d\n",
    				bufferLength);
    	}
        return 0;
    }

    // Get bits 14-23 of the buffer (lower 10 bits of second and third byte).
    unsigned int rtcmLength = getbitu(messageBuffer, 14, 10);

    return rtcmLength;
}

// Get the CRC - the 3 bytes following the embedded message
unsigned int getCRC(Buffer * buffer) {
	// The buffer contains a 3-byte header containing the message length, the message
	// and a three-byte CRC.
	if (buffer == NULL) {
		fprintf(stderr, "getCRC(): buffer is nll\n");
		return 0;
	}
	if (buffer->length < LENGTH_OF_HEADER + 2) {
		fprintf(stderr, "getCRC(): buffer too short to get CRC - %ld\n", buffer->length);
		return 0;
	}

	unsigned int messageLength = getRtcmLength(buffer->content, buffer->length);

	if (buffer == NULL || buffer->length < messageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC) {
		fprintf(stderr, "getCRC(): buffer too short to get CRC - %ld\n", buffer->length);
		return 0;
	}

	size_t startOfCRCInBits = (messageLength + LENGTH_OF_HEADER * 8);
	unsigned int crc = getbitu(buffer->content, startOfCRCInBits, 24);
	return crc;
}

// Get the message type - the first 12 bits of the embedded message.
unsigned int getMessageType(Buffer * buffer) {
	if (buffer == NULL || buffer->length < LENGTH_OF_HEADER + 2) {
		fprintf(stderr, "getMessageType(): buffer too short to get message type - %ld\n", buffer->length);
		return 0;
	}
	return getbitu(buffer->content, 24, 12);
}

int displaying() {
	return verboseMode > 0 && (displayingBuffers() || displayingRtcmMessages);
}

int displayingBuffers() {
	if (verboseMode > 0) {
		// If we have seen any RTCM messages, stop when we've displayed the maximum,
		// otherwise stp when we've displayed that number of buffers.
		if (numberOfRtcmMessagesDisplayed > 0) {
			return numberOfRtcmMessagesDisplayed <= MAX_RTCM_MESSAGES_TO_DISPLAY;
		} else {
			return numberOfBuffersDisplayed <= MAX_BUFFERS_TO_DISPLAY;
		}
	}
}

int displayingRtcmMessages() {
	return (verboseMode > 0 && (numberOfRtcmMessagesDisplayed <= MAX_RTCM_MESSAGES_TO_DISPLAY));
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
void displayBuffer(Buffer * buffer) {

	if (displayingBuffers()) {

		numberOfBuffersDisplayed++;

		if (buffer == NULL) {
			fprintf(stderr, "displayBuffer(): buffer is NULL\n");
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
			fprintf(stderr, "displayBuffer(): buffer is empty\n");
		}
	}
}

// Display the RTCM message.
void displayRtcmMessage(Buffer * buffer) {

	if (displayingRtcmMessages()) {

		if (buffer == NULL) {
			return;
		}
		if (buffer->length == 0) {
			return;
		}

		if (buffer->content == NULL) {
			return;
		}

		numberOfRtcmMessagesDisplayed++;

		unsigned int messageLength = getRtcmLength(buffer->content, buffer->length);

		if (messageLength == 0) {
			fprintf(stderr, "displayRtcmMessage(): buffer too short to get message length - %ld\n", buffer->length);
			return;
		}

		size_t totalMessageLength = messageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;

		if (buffer->length < totalMessageLength) {
			fprintf(stderr, "short buffer: length %ld totalMessageLength %ld\n",
				buffer->length, totalMessageLength);
			for (unsigned int i = 0; i < buffer->length; i++) {
				putc(buffer->content[i], stderr);
			}
			fprintf(stderr, "\n----------------------------------------------------------\n");
			return;
		}

		unsigned int crc = getCRC(buffer);

		unsigned int messageType = getMessageType(buffer);

		fprintf(stderr, "\nRTCM message - length %d type %d CRC %x (0x%02x%02x%02x)",
				messageLength, messageType, crc,
				buffer->content[messageLength+3],
				buffer->content[messageLength+4],
				buffer->content[messageLength+5]);
		for (unsigned int i = 0; i < totalMessageLength; i++) {
			if ((i % 32) == 0) {
				putc('\n', stderr);
			}
			fprintf(stderr, "%02x ", buffer->content[i]);
		}
		fprintf(stderr, "\n----------------------------------------------------------\n");
	}
}

// addMessageFragmentToBuffer adds a message fragment to a buffer, which may be empty or may
// already have some contents.
Buffer * addMessageFragmentToBuffer(Buffer * buffer, unsigned char * fragment, size_t fragmentLength) {
	unsigned char * startOfMessageInBuffer= NULL;
	if (buffer == NULL) {
		buffer = createBuffer(fragmentLength);
		memcpy(buffer->content, fragment, fragmentLength);
	} else if  (buffer->content == NULL) {
		free(buffer);
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
	static Buffer * rtcmMessageBuffer = NULL;	// Used to build a complete buffer for display.
	static size_t rtcmBufferLength = 0;			// length of the allocated RTCM buffer.
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

	while (i < inputBuffer.length) {

		// Scan the buffer for the next RTCM message.

		// This could be a case statement, except that we use break to escape from the while loop.
		if (state == STATE_EATING_MESSAGES) {
			// Process messages which are not RTCM by ignoring them.  If we see the start of an
			// RTCM message, stop eating.
			if (displaying() && i == 0 && inputBuffer.content[i] != 0xd3) {
				fprintf(stderr, "\neating messages from position 0\n");
			}
			if (inputBuffer.content[i] == 0xd3) {
				// Stop eating and process the RTCM message.
				if (displaying()) {
					fprintf(stderr, "\nFound RTCM message, stop eating - position %d\n", i);
				}
				state = STATE_PROCESSING_START_OF_RTCM_MESSAGE;
				continue;
			} else {
				// Eat.
				if (displaying()) {
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
				if (displaying()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				state = STATE_EATING_MESSAGES;
				continue;
			}

			if (i > 0) {
				fprintf(stderr,
						"warning: processing continuation of message, but this should only happen at the start of a buffer - i=%d\n", i);
				i++;
				if (displaying()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				state = STATE_EATING_MESSAGES;
				continue;
			}

			// This buffer starts with the continuation of an RTCM message.
			if (displayingRtcmMessages()) {
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
				if (displayingRtcmMessages()) {
					fprintf(stderr, "finding length of RTCM message from the continuation\n");
				}
				headerBuffer.content[headerBuffer.length-1] = inputBuffer.content[0];
				headerBuffer.content[headerBuffer.length] = inputBuffer.content[1];
				headerBuffer.length += 2;
				size_t rtcmMessageLength = getRtcmLength(headerBuffer.content, headerBuffer.length);
				if (displayingRtcmMessages()) {
					fprintf(stderr, "\nGot the message length - %ld - switching to state processing continuation of RTCM message\n",
							rtcmMessageLength);
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, (size_t)2);
				// The total message is Leader, message, CRC.
				totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;
				if (displayingRtcmMessages()) {
					rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, inputBuffer.content, (size_t)2);
				}

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

			if (displayingRtcmMessages()) {
				fprintf(stderr, "\ntotal message length %ld, sent so far %ld\n",
						totalRtcmMessageLength, rtcmMessageBytesSent);
			}

			size_t messageRemaining = totalRtcmMessageLength - rtcmMessageBytesSent;
			if (messageRemaining > inputBuffer.length) {
				// The whole of the buffer is part of a long RTCM message and there is
				// more of the message to come in another buffer.  Send the whole buffer.
				if (displayingRtcmMessages()) {
					fprintf(stderr, "the continuation buffer does not complete the RTCM message\n");
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, inputBuffer.length);
				rtcmMessageBytesSent += inputBuffer.length;
				if (displayingRtcmMessages()) {
					rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, inputBuffer.content, inputBuffer.length);
				}
				// Finished processing the input buffer.  Still in continuation state.
				break;
			} else {
				// the buffer starts with the end of an RTCM message, possibly followed by
				// more messages of some type.  Copy the rest of this RTCM message, display
				// it (if in verbose mode) and continue scanning.

				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, messageRemaining);
				if (displayingRtcmMessages()) {
					rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, inputBuffer.content, messageRemaining);
				}

				if (addNewline) {
					// Add a newline (only needed to help read the output during testing).
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, "\n", 1);
				}

				// The message is assembled and ready for display (if verbose mode).
				if (displayingRtcmMessages()) {
					fprintf(stderr, "displaying message\n");
					displayRtcmMessage(rtcmMessageBuffer);
				}
				// now process the other messages in the buffer.
				i += messageRemaining;
				if (displaying()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				rtcmMessageBytesSent = 0;	// Reset ready for the next trip.
				state = STATE_EATING_MESSAGES;
				continue;
			}

		} else if (state == STATE_PROCESSING_START_OF_RTCM_MESSAGE) {

			// This point in the input buffer is the start of an RTCM message.  Either the buffer contains
			// the whole message or the first fragment of it and it will be continued in the next buffer.
			// The message is binary, variable length and in three parts:
			//     header containing 0xd3 plus two bytes containing the 10-bit message length
			//     the message
			//     three-byte CRC,
			// So the total message is (length+6) bytes long.

			if (displayingRtcmMessages()) {
				fprintf(stderr, "processing RTCM message - position %d\n", i);
			}

			if (inputBuffer.content[i] != 0xd3) {
				fprintf(stderr, "\nError: state is processing start of RTCM message but byte is %d - position %d\n",
						inputBuffer.content[i], i);
			}

			// Get the RTCM message.
			unsigned char * remainingBuffer = inputBuffer.content + i;
			size_t lengthOfRemainingBuffer = inputBuffer.length - i;
			size_t rtcmMessageLength = getRtcmLength(remainingBuffer, lengthOfRemainingBuffer);
			if (displayingRtcmMessages()) {
				fprintf(stderr, "\nFound RTCM message - position %d given message length %ld\n",
						i, rtcmMessageLength);
			}

			if (rtcmMessageLength > 0) {
				// Simple case.  We have enough header to get the message length.
				totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;
				if ((totalRtcmMessageLength) <= lengthOfRemainingBuffer) {
					// The whole message is contained in this input buffer.  Copy it to the
					// output buffer.
					if (displayingRtcmMessages()) {
						fprintf(stderr, "processing complete RTCM message - position %d message length %ld\n",
								i, totalRtcmMessageLength);
					}
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, remainingBuffer, totalRtcmMessageLength);



					if (addNewline) {
						// Add a newline (only needed to help read the output during testing).
						outputBuffer = addMessageFragmentToBuffer(outputBuffer, "\n", 1);
					}

					if (displayingRtcmMessages()) {
						freeBuffer(rtcmMessageBuffer);	// To avoid any memory leak.
						rtcmMessageBuffer = NULL;
						rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer,
								remainingBuffer, totalRtcmMessageLength);
						displayRtcmMessage(rtcmMessageBuffer);
					}

					// Move the position to the next message.
					i += totalRtcmMessageLength;
					totalRtcmMessageLength = 0;	// Reset ready for the next trip.
					rtcmMessageBytesSent = 0;	// Reset ready for the next trip.
					state = STATE_EATING_MESSAGES;
					if (displayingRtcmMessages()) {
						fprintf(stderr, "\nRTCM message processed.  Eating messages from position %d\n", i);
					}
					continue;

				} else {
					// The end of the buffer contains the first part of an RTCM message, with more to follow
					// in the next buffer.  Copy what we have.
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, remainingBuffer, lengthOfRemainingBuffer);
					rtcmMessageBytesSent = lengthOfRemainingBuffer;
					if (displayingRtcmMessages()) {
						fprintf(stderr, "processing start of long RTCM message - position %d message length %ld got the first %ld bytes\n",
								i, totalRtcmMessageLength, lengthOfRemainingBuffer);
						freeBuffer(rtcmMessageBuffer);	// Avoid any memory leak.
						rtcmMessageBuffer = NULL;
						rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, remainingBuffer, lengthOfRemainingBuffer);
					}
					state = STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE;
					// Finished processing this buffer.
					break;
				}

			} else {
				// Edge case - the buffer ends with a fragment of an RTCM message but the fragment is short
				// and doesn't contain a complete header, so we can't get the message length yet.  Take
				// what we have, put it in the output buffer for sending and record it in the header buffer
				// to be used in the next call.
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, remainingBuffer, lengthOfRemainingBuffer);
				rtcmMessageBytesSent = lengthOfRemainingBuffer;
				if (displayingRtcmMessages()) {
					fprintf(stderr, "processing start of long message with incomplete length value - position %d buffer length %ld\n",
							i, inputBuffer.length);
					freeBuffer(rtcmMessageBuffer);	// Avoid any memory leak.
					rtcmMessageBuffer = NULL;
					rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, remainingBuffer, lengthOfRemainingBuffer);
				}

				state = STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER;
				// Finished processing this buffer.
				break;
			}
		} else {
			// Shouldn't happen.
			fprintf(stderr, "warning:  unknown state value %d at i=%d\n", state, i);
			i++;
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
