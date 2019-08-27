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

#define MAX_BUFFERS_TO_DISPLAY 50
#define LENGTH_OF_HEADER 3
#define LENGTH_OF_CRC 3
#define STATE_EATING_MESSAGES 0
#define STATE_PROCESSING_START_OF_RTCM_MESSAGE 1
#define STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE 2
#define STATE_PROCESSING_CONTINUATION_OF_RTCM_MESSAGE_WITH_INCOMPLETE_HEADER 3

extern int verboseMode;
extern int addNewline;

static int numberOfBuffersDisplayed = 0;
static unsigned int state = STATE_EATING_MESSAGES;

// Get the length of the RTCM message.  The three bytes of the header form a big-endian
// 24-bit value. The bottom ten bits is the message length.
unsigned int getRtcmLength(unsigned char * messageBuffer, unsigned int bufferLength) {

    if (bufferLength < 3) {
        // The message starts very near the end of the buffer, so we can't calculate
        // the length until we get the next one.

    	if (displayingBuffers()) {
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

int displayingBuffers() {
	int result = FALSE;
	if (verboseMode > 0) {
		result = numberOfBuffersDisplayed < MAX_BUFFERS_TO_DISPLAY;
		return result;
	} else {
		return FALSE;
	}
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

// displayBuffer() displays the contents of a buffer and the given buffer processing state.
void displayBuffer(Buffer * buffer) {

	if (buffer == NULL) {
		fprintf(stderr, "displayBuffer(): buffer is NULL\n");
		return;
	}

	if (buffer->length == 0) {
		fprintf(stderr, "displayBuffer(): buffer is empty\n");
		return;
	}

	if (displayingBuffers()) {

		numberOfBuffersDisplayed++;

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

	if (buffer == NULL) {
		return;
	}
	if (buffer->length == 0) {
		return;
	}

	if (buffer->content == NULL) {
		return;
	}

	if (displayingBuffers()) {

		numberOfBuffersDisplayed++;

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

// Given a Buffer containing messages and message fragments, extract any RTCM data blocks or
// fragments of them and return a Buffer containing just those data.
Buffer * getRtcmDataBlocks(Buffer inputBuffer) {
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
	 * getRtcmDataBlocks() takes a buffer containing satellite navigation messages of all sorts and
	 * returns a buffer containing only RTCM messages.  Data is read from a satnav device in real time
	 * with a timeout specified and using a bounded buffer.  The resulting input buffer typically contains
	 * a fragment of a message that is continued from the previous buffer, a series of complete messages
	 * and a fragment of a message that is continued in the next buffer.  Variations on that include a
	 * buffer which starts exactly at the start of a message, a buffer which is just one large fragment,
	 * and so on.  Messages may be separated by null bytes and/or line breaks.  A line break may be a
	 * newline or a Carriage Return Newline sequence (if the sending system is MS Windows).  Each message
	 * may or may not be RTCM.  If the buffer contains two adjacent RTCM messages, there does not have to be
	 * any separator between them.
	 *
	 * Each RTCM data block is a stream of bytes with a 24-bit big-endian header, a variable-length embedded
	 * message and a 24-bit big-endian Cyclic Redundancy Check (CRC) value.  (To avoid confusion between the
	 * whole stream of data and the embedded message, I call the sequence (header, embedded message, CRC)
	 * the RTCM data block and uses message to refer to the embedded message.)  The header has 0xd3 in the top
	 * byte and the bottom ten bits are the message length.  It appears that the other six bits are always
	 * zero.  For example:
	 *
	 *     D3 00 13 3E D7 D3 02 02 98 0E DE EF 34 B4 BD 62 AC 09 41 98 6F 33 36 0B 98
     *     -header-  1           5             10             15          19 ---CRC--
     *
     * The message contents is binary, actually a string of bits with fields of various sizes.  In this
     * example the message happens to contain another D3 byte.  The example comes from
     * https://portal.u-blox.com/s/question/0D52p00008HKDWQCA5/decoding-rtcm3-message
     * and that posting also refers to a longer message.
     *
     * Having the message length specified in the message and given that an RTCM block can be spread over many
     * input buffers, there's an edge case where the buffer contains just the first one or two bytes of an
     * RTCM block, say 0xD3 0x00.  That's not enough to figure out the message length.  The filter needs to write
     * those data but remember them until the next block arrives so that it can make sense of the start of that.
     *
     * The format of the message embedded in the RTCM data block are defined in a standard.  It's not open-source
     * and I haven't bought a copy.  There are several numbered message types.  Each messages type has a
     * different format, but the format of the first few bits is common to all.  For example The first 12 bits
     * of the embedded message gives the message type.  The open source library RTKLIB has methods to decode the
     * various messages, so the format can be gleaned by reading that source code.
     *
     * This method takes the input buffer, scans it for RTCM data blocks, copies those to the output buffer
	 * and discards anything else.  Since messages can span many buffers, some state must be preserved
	 * between input buffers.
	 *
	 * The output buffer is built up on the fly using malloc() and realloc().  Since it only ever contains some or
	 * all of the messages in the input buffer, and the length of that is bounded, the output buffer is bounded
	 * automatically.
	 *
	 * To keep track of state between input buffers there is a state machine with states representing:
	 * not processing an RTCM message (discarding whatever it sees); processing the start (possibly all) of an RTCM
	 * data block; processing the continuation of an RTCM data block of unknown length, processing the continuation
	 * of an RTCM data block of known length.  State data includes static variables to track how much of a message
	 * that spans many buffers has already been processed.
	 *
	 * In verbose mode, the filter displays the first few input buffers and any RTCM messages in those buffers.
	 *
	 * Some messages (including RTCM) are binary so the buffer may contain several null bytes, which means
     * (a) you can't treat the buffer as a simple C string and (b) messages may contain what look like newlines or
     * RTCM headers, but which are just part of the data.  Also, in a noisy environment we should assume that
     * characters could be dropped.  To guard against all this, the function should check the CRC (using the code
     * from RTKLIB) and ignore any messages that are badly formatted, but at present it doesn't.  The destination
     * caster should have some error checking of its own, so letting some junk through should not be catastrophic.
	 */

	while (i < inputBuffer.length) {

		// Scan the buffer for the next RTCM message.

		// This could be a case statement, except that we use break to escape from the while loop.
		if (state == STATE_EATING_MESSAGES) {
			// Process messages which are not RTCM by ignoring them.  If we see the start of an
			// RTCM message, stop eating.
			if (displayingBuffers() && i == 0 && inputBuffer.content[i] != 0xd3) {
				fprintf(stderr, "\neating messages from position 0\n");
			}
			if (inputBuffer.content[i] == 0xd3) {
				// Stop eating and process the RTCM message.
				if (displayingBuffers()) {
					fprintf(stderr, "\nFound RTCM message, stop eating - position %d\n", i);
				}
				state = STATE_PROCESSING_START_OF_RTCM_MESSAGE;
				continue;
			} else {
				// Eat.
				if (displayingBuffers()) {
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
				if(displayingBuffers()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				state = STATE_EATING_MESSAGES;
				continue;
			}

			if (i > 0) {
				fprintf(stderr,
						"warning: processing continuation of message, but this should only happen at the start of a buffer - i=%d\n", i);
				i++;
				if (displayingBuffers()) {
					fprintf(stderr, "\neating messages from position %d\n", i);
				}
				state = STATE_EATING_MESSAGES;
				continue;
			}

			// This buffer starts with the continuation of an RTCM message.
			if (displayingBuffers()) {
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
				if (displayingBuffers()) {
					fprintf(stderr, "finding length of RTCM message from the continuation\n");
				}
				headerBuffer.content[headerBuffer.length-1] = inputBuffer.content[0];
				headerBuffer.content[headerBuffer.length] = inputBuffer.content[1];
				headerBuffer.length += 2;
				size_t rtcmMessageLength = getRtcmLength(headerBuffer.content, headerBuffer.length);
				if (displayingBuffers()) {
					fprintf(stderr, "\nGot the message length - %ld - switching to state processing continuation of RTCM message\n",
							rtcmMessageLength);
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, (size_t)2);
				// The total message is Leader, message, CRC.
				totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;
				if (displayingBuffers()) {
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

			if (displayingBuffers()) {
				fprintf(stderr, "\ntotal message length %ld, sent so far %ld\n",
						totalRtcmMessageLength, rtcmMessageBytesSent);
			}

			size_t messageRemaining = totalRtcmMessageLength - rtcmMessageBytesSent;
			if (messageRemaining > inputBuffer.length) {
				// The whole of the buffer is part of a long RTCM message and there is
				// more of the message to come in another buffer.  Send the whole buffer.
				if (displayingBuffers()) {
					fprintf(stderr, "the continuation buffer does not complete the RTCM message\n");
				}
				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, inputBuffer.length);
				rtcmMessageBytesSent += inputBuffer.length;
				if (displayingBuffers()) {
					rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, inputBuffer.content, inputBuffer.length);
				}
				// Finished processing the input buffer.  Still in continuation state.
				break;
			} else {
				// the buffer starts with the end of an RTCM message, possibly followed by
				// more messages of some type.  Copy the rest of this RTCM message, display
				// it (if in verbose mode) and continue scanning.

				outputBuffer = addMessageFragmentToBuffer(outputBuffer, inputBuffer.content, messageRemaining);
				if (displayingBuffers()) {
					rtcmMessageBuffer = addMessageFragmentToBuffer(rtcmMessageBuffer, inputBuffer.content, messageRemaining);
				}

				if (addNewline) {
					// Add a newline (only needed to help read the output during testing).
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, "\n", 1);
				}

				// The message is assembled and ready for display (if verbose mode).
				if (displayingBuffers()) {
					fprintf(stderr, "displaying message\n");
					displayRtcmMessage(rtcmMessageBuffer);
				}
				// now process the other messages in the buffer.
				i += messageRemaining;
				if (displayingBuffers()) {
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

			if (displayingBuffers()) {
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
			if (displayingBuffers()) {
				fprintf(stderr, "\nFound RTCM message - position %d given message length %ld\n",
						i, rtcmMessageLength);
			}

			if (rtcmMessageLength > 0) {
				// Simple case.  We have enough header to get the message length.
				totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;
				if ((totalRtcmMessageLength) <= lengthOfRemainingBuffer) {
					// The whole message is contained in this input buffer.  Copy it to the
					// output buffer.
					if (displayingBuffers()) {
						fprintf(stderr, "processing complete RTCM message - position %d message length %ld\n",
								i, totalRtcmMessageLength);
					}
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, remainingBuffer, totalRtcmMessageLength);



					if (addNewline) {
						// Add a newline (only needed to help read the output during testing).
						outputBuffer = addMessageFragmentToBuffer(outputBuffer, "\n", 1);
					}

					if (displayingBuffers()) {
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
					if (displayingBuffers()) {
						fprintf(stderr, "\nRTCM message processed.  Eating messages from position %d\n", i);
					}
					continue;

				} else {
					// The end of the buffer contains the first part of an RTCM message, with more to follow
					// in the next buffer.  Copy what we have.
					outputBuffer = addMessageFragmentToBuffer(outputBuffer, remainingBuffer, lengthOfRemainingBuffer);
					rtcmMessageBytesSent = lengthOfRemainingBuffer;
					if (displayingBuffers()) {
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
				if (displayingBuffers()) {
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
