/*
 * messagehandler.c
 *
 *  Created on: 21 Aug 2019
 *      Author: simon Ritchie
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#define STATE_PROCESSING_RTCM_MESSAGE 1

static unsigned int state = STATE_EATING_MESSAGES;

extern int verboseMode;

static int numberOfBuffersDisplayed = 0;

// Message counts.
static unsigned long int rtcmMessagesSoFar = 0;
static unsigned long int type1005MessagesSoFar = 0;
static unsigned long int type1074MessagesSoFar = 0;
static unsigned long int type1084MessagesSoFar = 0;
static unsigned long int type1097MessagesSoFar = 0;
static unsigned long int type1094MessagesSoFar = 0;
static unsigned long int type1124MessagesSoFar = 0;
static unsigned long int type1127MessagesSoFar = 0;
static unsigned long int type1230MessagesSoFar = 0;
static unsigned long int unexpectedMessagesSoFar = 0;

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
unsigned int getMessageType(rtcm_t * rtcm) {
	if (rtcm == NULL || rtcm->nbyte == 0) {
		fprintf(stderr, "getMessageType(): no message\n");
		return 0;
	}
	return getbitu(rtcm->buff,24,12);
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
		case STATE_PROCESSING_RTCM_MESSAGE:
			fprintf(stderr, "\nBuffer length %ld, state processing start of RTCM message:",
					buffer->length);
			break;
		default:
			fprintf(stderr, "\nBuffer length %ld, unknown state:",
					buffer->length);
			break;
		}

		if (buffer->length > 0) {
			for (unsigned int j = 0; j < buffer->length; j++) {
				if ((j % 32) == 0) {
					putc('\n', stderr);
				}
				fprintf(stderr, "%02x ", buffer->content[j]);
			}
			putc('\n', stderr);
			putc('\n', stderr);
			for (unsigned int j = 0; j < buffer->length; j++) {
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
void displayRtcmMessage(rtcm_t * rtcm) {

	if (rtcm == NULL) {
		return;
	}

	if (rtcm->nbyte == 0) {
		return;
	}

	if (displayingBuffers()) {

		numberOfBuffersDisplayed++;

		fprintf(stderr, "\nFound RTCM message - length %d/%d type %d",
				rtcm->len-3, rtcm->nbyte, rtcm->outtype);
		for (int i = 0; i < rtcm->nbyte; i++) {
			if ((i % 32) == 0) {
				putc('\n', stderr);
			}
			fprintf(stderr, "%02x ", rtcm->buff[i]);
		}
		fprintf(stderr, "\n----------------------------------------------------------\n");
	}
}


void resetTotals() {
	rtcmMessagesSoFar = 0;
	type1005MessagesSoFar = 0;
	type1074MessagesSoFar = 0;
	type1084MessagesSoFar = 0;
	type1097MessagesSoFar = 0;
	type1094MessagesSoFar = 0;
	type1124MessagesSoFar = 0;
	type1127MessagesSoFar = 0;
	type1230MessagesSoFar = 0;
	unexpectedMessagesSoFar = 0;
}

void displayTotals() {
	// Get the time as yy/mm/dd hh:mm:ss.
	time_t now = time(NULL);
	struct tm * tm = gmtime(&now);
	char timeStr[20];
	sprintf(timeStr, "%04d/%02d/%02d %02d:%02d:%02d",
			tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	fprintf(stderr, "%s %ld messages so far: %ld 1005 %ld 1074 %ld 1084 %ld 1094 %ld 1097 %ld 1124 %ld 1127 %ld 1230 %ld unexpected\n",
		timeStr,
		rtcmMessagesSoFar,
		type1005MessagesSoFar,
		type1074MessagesSoFar,
		type1084MessagesSoFar,
		type1094MessagesSoFar,
		type1097MessagesSoFar,
		type1124MessagesSoFar,
		type1127MessagesSoFar,
		type1230MessagesSoFar,
		unexpectedMessagesSoFar);
}

void displayTotalsEveryHour() {
	static int currentDay = 0;
	static int currentHour = 0;

	time_t now = time(NULL);
	struct tm * tm = gmtime(&now);
	if (currentDay == 0) {
		currentDay = tm->tm_mday;
		currentHour = 23;
	}

	// Report every hour.
	if (currentHour != tm->tm_hour) {
		currentHour = tm->tm_hour;
		displayTotals();
	}

	// Reset totals once after midnight.
	if (currentDay !=tm->tm_mday) {
		currentDay =tm->tm_mday;
		resetTotals();
	}
}

// addMessageFragmentToBuffer adds a message fragment to a buffer, which may be empty or may
// already have some contents.
Buffer * addMessageFragmentToBuffer(Buffer * buffer, unsigned char * fragment, size_t fragmentLength) {
	if (buffer == NULL) {
		buffer = createBuffer(fragmentLength);
		memcpy(buffer->content, fragment, fragmentLength);
		return buffer;
	}

	if  (buffer->content == NULL) {
		buffer->content = malloc(fragmentLength);
		memcpy(buffer->content, fragment, fragmentLength);
		buffer->length = fragmentLength;
		return buffer;
	}

	buffer->content = realloc(buffer->content, buffer->length + fragmentLength);
	memcpy(buffer->content + buffer->length, fragment, fragmentLength);
	buffer->length += fragmentLength;
	return buffer;
}


// Given a Buffer containing messages and message fragments, extract any RTCM data blocks or
// fragments of them and return a Buffer containing just those data.
Buffer * getRtcmDataBlocks(Buffer inputBuffer, rtcm_t * rtcm) {

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

	const unsigned char rtcm_header_byte = 0xd3;
	static Buffer * trailingFragment = NULL;	// Holds any unprocessed fragment of the previous buffer.

	// The combinedBuffer is our workspace.  If there is a trailing fragment from
	// last time, it contains that followed by the input buffer, otherwise it contains
	// just the input buffer.
	Buffer * combinedBuffer = NULL;

	// The outputBuffer.  Complete RTCM messages are copied into here.
	Buffer * outputBuffer = NULL;

	if (inputBuffer.length == 0) {
		return NULL;
	}
	if (inputBuffer.content == NULL) {
		return NULL;
	}

	if (trailingFragment != NULL && trailingFragment->content != NULL) {
		// There is a fragment left to process from the end of the last buffer.
		// It should start with a 0xd3 byte.
		// Combine the trailing part from last time and the new input buffer.
		combinedBuffer = addMessageFragmentToBuffer(
				combinedBuffer, trailingFragment->content, trailingFragment->length);
		combinedBuffer = addMessageFragmentToBuffer(
				combinedBuffer, inputBuffer.content, inputBuffer.length);
		freeBuffer(trailingFragment);
		trailingFragment = NULL;
		if (displayingBuffers()) {
			fprintf(stderr, "processing combined buffer length %ld\n", combinedBuffer->length);
		}

		//We need at least the first three bytes of the message to figure out the length.
		if ((combinedBuffer->length) < LENGTH_OF_HEADER) {
			// Edge case.  The header is still incomplete! Copy what we have and exit.
			if (displayingBuffers()) {
				fprintf(stderr, "not enough in combined buffer to get the header - length %ld\n",
						combinedBuffer->length);
			}
			trailingFragment = addMessageFragmentToBuffer(
					trailingFragment, combinedBuffer->content, combinedBuffer->length);
			return NULL;
		}

		if (displayingBuffers()) {
			fprintf(stderr, "combined input buffer");
			displayBuffer(combinedBuffer);
		}

	} else {
		// The previous buffer was completely processed.  Process just the
		// new input buffer.
		combinedBuffer = addMessageFragmentToBuffer(
				combinedBuffer, inputBuffer.content, inputBuffer.length);
		if (displayingBuffers()) {
			fprintf(stderr, "processing input buffer length %ld\n", combinedBuffer->length);
		}
	}

	int displayEating = TRUE;

	size_t i = 0;
	while (i < combinedBuffer->length) {

		// Scan the buffer for the next RTCM message.

		unsigned char * remainingBuffer = combinedBuffer->content + i;
		size_t lengthOfRemainingBuffer = combinedBuffer->length - i;

		// This could be a case statement, except that we use break to escape from the while loop.
		if (state == STATE_EATING_MESSAGES) {
			// Process messages which are not RTCM by ignoring them.  If we see the start of an
			// RTCM message, stop eating.
			if (displayingBuffers() && combinedBuffer->content[i] != rtcm_header_byte) {

			}
			if (combinedBuffer->content[i] == rtcm_header_byte) {
				// Stop eating.
				if (displayingBuffers()) {
					fprintf(stderr, "\nStart of RTCM message, stop eating - position %ld\n", i);
				}
				state = STATE_PROCESSING_RTCM_MESSAGE;
				continue;
			} else {
				// Eat.
				if (displayingBuffers()) {
					if (displayEating) {
						// Only display this once.
						displayEating = FALSE;
						fprintf(stderr, "\neating messages from position %ld\n", i);
					}
					putc(combinedBuffer->content[i], stderr);
				}
				i++;
				continue;
			}

		} else if (state == STATE_PROCESSING_RTCM_MESSAGE) {

			// This point in the input buffer is the start of an RTCM message.  Either the buffer contains
			// the whole message or the first fragment of it and it will be continued in the next buffer.
			// The message is binary, variable length and in three parts:
			//     header containing 0xd3 plus two bytes containing the 10-bit message length
			//     the message
			//     three-byte CRC,
			// So the total message is (length+6) bytes long and we need the first three bytes to figure
			// out the length.

			if (displayingBuffers()) {
				fprintf(stderr, "processing RTCM message - position %ld\n", i);
			}

			if (combinedBuffer->content[i] != rtcm_header_byte) {
				fprintf(stderr, "\nError: state is processing start of RTCM message but byte is 0x%x - position %ld\n",
						combinedBuffer->content[i], i);
				state = STATE_EATING_MESSAGES;
				i++;
				continue;
			}

			size_t rtcmMessageLength = getRtcmLength(remainingBuffer, lengthOfRemainingBuffer);

			if (rtcmMessageLength == 0) {
				// We don't have enough of the message to figure out the length.  Put the
				// remainder into the trailing buffer for next time and exit.
				if (displayingBuffers()) {
					fprintf(stderr, "\nincomplete RTCM message at position %ld given message length %ld remaining %ld - deferring\n",
							i, rtcmMessageLength, lengthOfRemainingBuffer);
				}
				trailingFragment = addMessageFragmentToBuffer(
						trailingFragment, remainingBuffer, lengthOfRemainingBuffer);
				break;
			}

			// We have the message length.
			if (displayingBuffers()) {
				fprintf(stderr, "\nFound RTCM message - position %ld given message length %ld\n",
						i, rtcmMessageLength);
			}

			size_t totalRtcmMessageLength = rtcmMessageLength + LENGTH_OF_HEADER + LENGTH_OF_CRC;

			if (totalRtcmMessageLength > lengthOfRemainingBuffer) {
				// The rest of the input buffer does not contain the whole message.
				// Copy what we have into the trailing fragment and carry on.
				if (displayingBuffers()) {
					fprintf(stderr, "\nincomplete RTCM message - position %ld message length %ld/%ld remaining %ld\n",
							i, rtcmMessageLength, totalRtcmMessageLength, lengthOfRemainingBuffer);
				}
				trailingFragment = addMessageFragmentToBuffer(
						trailingFragment, remainingBuffer, lengthOfRemainingBuffer);
				break;
			}

			// The whole message is contained in this input buffer.  Check it.
			if (displayingBuffers()) {
				fprintf(stderr, "\nchecking message\n");
			}
			memcpy(rtcm->buff, remainingBuffer, totalRtcmMessageLength);
			rtcm->nbyte = totalRtcmMessageLength;
			rtcm->len = rtcmMessageLength + LENGTH_OF_HEADER;
			int messageStatus = input_rtcm3(rtcm, rtcm_header_byte);
			if (messageStatus < 0) {
				// The message is not legal.  Start eating.
				if (displayingBuffers()) {
					switch (messageStatus) {
					case -2:
						fprintf(stderr, "RTCM message fails CRC check - position %ld given message length %ld\n",
							i, rtcmMessageLength);
						break;
					case -1:
						fprintf(stderr, "error - cannot decode RTCM message - position %ld given message length %ld\n",
							i, rtcmMessageLength);
						break;
					default:
						fprintf(stderr, "unexpected error while reading RTCM message - position %ld given message length %ld\n",
								i, rtcmMessageLength);
						break;
					}
				}
				state = STATE_EATING_MESSAGES;
				i++;
				continue;
			} else {
				if (displayingBuffers()) {
					fprintf(stderr, "RTCM message at position %ld.  Status %d type %d given message length %ld\n",
						i, messageStatus, rtcm->outtype, rtcmMessageLength);
				}
				rtcmMessagesSoFar++;
				switch (rtcm->outtype) {
				case 1005:
					type1005MessagesSoFar++;
					break;
				case 1074:
					type1074MessagesSoFar++;
					break;
				case 1084:
					type1084MessagesSoFar++;
					break;
				case 1094:
					type1094MessagesSoFar++;
					break;
				case 1097:
					type1097MessagesSoFar++;
					break;
				case 1124:
					type1124MessagesSoFar++;
					break;
				case 1127:
					type1127MessagesSoFar++;
					break;
				case 1230:
					type1230MessagesSoFar++;
					break;
				default:
					unexpectedMessagesSoFar++;
					fprintf(stderr, "unexpected message type %d\n", rtcm->outtype);
					break;
				}
			}

			// The message is legal.  Copy it to the output buffer.
			if (displayingBuffers()) {
				fprintf(stderr, "processing complete RTCM message - position %ld message length %ld\n",
						i, totalRtcmMessageLength);
			}
			outputBuffer = addMessageFragmentToBuffer(outputBuffer, remainingBuffer, totalRtcmMessageLength);

			if (displayingBuffers()) {
				displayRtcmMessage(rtcm);
			}

			// Move the position to the next message.
			i += totalRtcmMessageLength;
			state = STATE_EATING_MESSAGES;
			continue;

		} else {
			// Shouldn't happen.
			fprintf(stderr, "warning:  unknown state value %d at i=%ld\n", state, i);
			i++;
			state = STATE_EATING_MESSAGES;
		}   // end if
	}  // end while

	freeBuffer(combinedBuffer);

	if (displayingBuffers()) {
		if (outputBuffer == NULL || outputBuffer->length == 0) {
			fprintf(stderr, "returning empty output buffer\n");
		} else {
			fprintf(stderr, "returning output buffer");
		}
		// Totals are displayed frequently at first.
		displayTotals();
	}

	// Totals are displayed every hour in normal running.
	displayTotalsEveryHour();

	return outputBuffer;
}
