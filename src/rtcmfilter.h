/*
 * rtcmfilter.h
 *
 *  Created on: 21 Aug 2019
 *      Author: simon
 */

#ifndef SRC_RTCMFILTER_H_
#define SRC_RTCMFILTER_H_

#ifndef RTKLIB_H
#include "rtklib.h"
#endif

#ifndef TRUE
#define TRUE -1
#define FALSE 0
#endif

typedef struct buffer {
	unsigned char * content;	// Space for a list of RTCM messages and/or fragments.
	size_t length;				// length of the malloc'ed content buffer.
} Buffer;

extern int displayingBuffers();
extern Buffer * createBuffer(size_t length);
extern void freeBuffer(Buffer * buffer);
extern unsigned int getRtcmLength(unsigned char * messageBuffer, unsigned int bufferLength);
extern unsigned int getCRC(Buffer * buffer);
extern unsigned int getMessageType(rtcm_t * rtcm);
extern Buffer * createBuffer(size_t length);
extern void freeBuffer(Buffer * buffer);
extern void displayBuffer(Buffer * buffer);
extern void displayRtcmMessage(rtcm_t * rtcm);
extern Buffer * addMessageFragmentToBuffer(Buffer * buffer, unsigned char * fragment, size_t fragmentLength);
extern Buffer * getRtcmDataBlocks(Buffer inputBuffer, rtcm_t * rtcm);

#endif /* SRC_RTCMFILTER_H_ */
