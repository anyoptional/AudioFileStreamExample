/*
     File: afsclient.cpp
 Abstract: n/a
  Version: 1.0.1
 
 Disclaimer: IMPORTANT:  This Apple software is supplied to you by Apple
 Inc. ("Apple") in consideration of your agreement to the following
 terms, and your use, installation, modification or redistribution of
 this Apple software constitutes acceptance of these terms.  If you do
 not agree with these terms, please do not use, install, modify or
 redistribute this Apple software.
 
 In consideration of your agreement to abide by the following terms, and
 subject to these terms, Apple grants you a personal, non-exclusive
 license, under Apple's copyrights in this original Apple software (the
 "Apple Software"), to use, reproduce, modify and redistribute the Apple
 Software, with or without modifications, in source and/or binary forms;
 provided that if you redistribute the Apple Software in its entirety and
 without modifications, you must retain this notice and the following
 text and disclaimers in all such redistributions of the Apple Software.
 Neither the name, trademarks, service marks or logos of Apple Inc. may
 be used to endorse or promote products derived from the Apple Software
 without specific prior written permission from Apple.  Except as
 expressly stated in this notice, no other rights or licenses, express or
 implied, are granted by Apple herein, including but not limited to any
 patent rights that may be infringed by your derivative works or by other
 works in which the Apple Software may be incorporated.
 
 The Apple Software is provided by Apple on an "AS IS" basis.  APPLE
 MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
 THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
 FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND
 OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
 
 IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
 OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION,
 MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED
 AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
 STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 
 Copyright (C) 2013 Apple Inc. All Rights Reserved.
 
*/

#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <AudioToolbox/AudioToolbox.h>

#define PRINTERROR(LABEL)	printf("%s err %4.4s %ld\n", LABEL, (char *)&err, err)

const int port = 51515;			// the port we will use

const unsigned int kNumAQBufs = 3;			// number of audio queue buffers we allocate
const size_t kAQBufSize = 128 * 1024;		// number of bytes in each audio queue buffer
const size_t kAQMaxPacketDescs = 512;		// number of packet descriptions in our array

struct MyData
{
	AudioFileStreamID audioFileStream;	// the audio file stream parser

	AudioQueueRef audioQueue;								// the audio queue
	AudioQueueBufferRef audioQueueBuffer[kNumAQBufs];		// audio queue buffers
	
	AudioStreamPacketDescription packetDescs[kAQMaxPacketDescs];	// packet descriptions for enqueuing audio
	
	unsigned int fillBufferIndex;	// the index of the audioQueueBuffer that is being filled
	size_t bytesFilled;				// how many bytes have been filled
	size_t packetsFilled;			// how many packets have been filled

	bool inuse[kNumAQBufs];			// flags to indicate that a buffer is still in use
	bool started;					// flag to indicate that the queue has been started
	bool failed;					// flag to indicate an error occurred

	pthread_mutex_t mutex;			// a mutex to protect the inuse flags
	pthread_cond_t cond;			// a condition varable for handling the inuse flags
	pthread_cond_t done;			// a condition varable for handling the inuse flags
};
typedef struct MyData MyData;

int  MyConnectSocket();

void MyAudioQueueOutputCallback(void* inClientData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
void MyAudioQueueIsRunningCallback(void *inUserData, AudioQueueRef inAQ, AudioQueuePropertyID inID);

void MyPropertyListenerProc(	void *							inClientData,
								AudioFileStreamID				inAudioFileStream,
								AudioFileStreamPropertyID		inPropertyID,
								UInt32 *						ioFlags);

void MyPacketsProc(				void *							inClientData,
								UInt32							inNumberBytes,
								UInt32							inNumberPackets,
								const void *					inInputData,
								AudioStreamPacketDescription	*inPacketDescriptions);

OSStatus MyEnqueueBuffer(MyData* myData);
void WaitForFreeBuffer(MyData* myData);

int main (int argc, char * const argv[]) 
{
	// allocate a struct for storing our state
	MyData* myData = (MyData*)calloc(1, sizeof(MyData));
	
	// initialize a mutex and condition so that we can block on buffers in use.
	pthread_mutex_init(&myData->mutex, NULL);
	pthread_cond_init(&myData->cond, NULL);
	pthread_cond_init(&myData->done, NULL);
	
	// get connected
	int connection_socket = MyConnectSocket();
	if (connection_socket < 0) return 1;
	printf("connected\n");

	// allocate a buffer for reading data from a socket
	const size_t kRecvBufSize = 40000;
	char* buf = (char*)malloc(kRecvBufSize * sizeof(char));

	// create an audio file stream parser
	OSStatus err = AudioFileStreamOpen(myData, MyPropertyListenerProc, MyPacketsProc, 
							kAudioFileAAC_ADTSType, &myData->audioFileStream);
	if (err) { PRINTERROR("AudioFileStreamOpen"); free(buf); return 1; }
	
	while (!myData->failed) {
		// read data from the socket
		printf("->recv\n");
		ssize_t bytesRecvd = recv(connection_socket, buf, kRecvBufSize, 0);
		printf("bytesRecvd %ld\n", bytesRecvd);
		if (bytesRecvd <= 0) break; // eof or failure
		
		// parse the data. this will call MyPropertyListenerProc and MyPacketsProc
		err = AudioFileStreamParseBytes(myData->audioFileStream, bytesRecvd, buf, 0);
		if (err) { PRINTERROR("AudioFileStreamParseBytes"); break; }
	}

	// enqueue last buffer
	MyEnqueueBuffer(myData);

	printf("flushing\n");
	err = AudioQueueFlush(myData->audioQueue);
	if (err) { PRINTERROR("AudioQueueFlush"); free(buf); return 1; }

	printf("stopping\n");
	err = AudioQueueStop(myData->audioQueue, false);
	if (err) { PRINTERROR("AudioQueueStop"); free(buf); return 1; }
	
	printf("waiting until finished playing..\n");
	pthread_mutex_lock(&myData->mutex); 
	pthread_cond_wait(&myData->done, &myData->mutex);
	pthread_mutex_unlock(&myData->mutex);
	
	
	printf("done\n");
	
	// cleanup
	free(buf);
	err = AudioFileStreamClose(myData->audioFileStream);
	err = AudioQueueDispose(myData->audioQueue, false);
	close(connection_socket);
	free(myData);
	
    return 0;
}

void MyPropertyListenerProc(	void *							inClientData,
								AudioFileStreamID				inAudioFileStream,
								AudioFileStreamPropertyID		inPropertyID,
								UInt32 *						ioFlags)
{	
	// this is called by audio file stream when it finds property values
	MyData* myData = (MyData*)inClientData;
	OSStatus err = noErr;

	printf("found property '%c%c%c%c'\n", (char)(inPropertyID>>24)&255, (char)(inPropertyID>>16)&255, (char)(inPropertyID>>8)&255, (char)inPropertyID&255);

	switch (inPropertyID) {
		case kAudioFileStreamProperty_ReadyToProducePackets :
		{
			// the file stream parser is now ready to produce audio packets.
			// get the stream format.
			AudioStreamBasicDescription asbd;
			UInt32 asbdSize = sizeof(asbd);
			err = AudioFileStreamGetProperty(inAudioFileStream, kAudioFileStreamProperty_DataFormat, &asbdSize, &asbd);
			if (err) { PRINTERROR("get kAudioFileStreamProperty_DataFormat"); myData->failed = true; break; }
			
			// create the audio queue
			err = AudioQueueNewOutput(&asbd, MyAudioQueueOutputCallback, myData, NULL, NULL, 0, &myData->audioQueue);
			if (err) { PRINTERROR("AudioQueueNewOutput"); myData->failed = true; break; }
			
			// allocate audio queue buffers
			for (unsigned int i = 0; i < kNumAQBufs; ++i) {
				err = AudioQueueAllocateBuffer(myData->audioQueue, kAQBufSize, &myData->audioQueueBuffer[i]);
				if (err) { PRINTERROR("AudioQueueAllocateBuffer"); myData->failed = true; break; }
			}

			// get the cookie size
			UInt32 cookieSize;
			Boolean writable;
			err = AudioFileStreamGetPropertyInfo(inAudioFileStream, kAudioFileStreamProperty_MagicCookieData, &cookieSize, &writable);
			if (err) { PRINTERROR("info kAudioFileStreamProperty_MagicCookieData"); break; }
			printf("cookieSize %d\n", (unsigned int)cookieSize);

			// get the cookie data
			void* cookieData = calloc(1, cookieSize);
			err = AudioFileStreamGetProperty(inAudioFileStream, kAudioFileStreamProperty_MagicCookieData, &cookieSize, cookieData);
			if (err) { PRINTERROR("get kAudioFileStreamProperty_MagicCookieData"); free(cookieData); break; }

			// set the cookie on the queue.
			err = AudioQueueSetProperty(myData->audioQueue, kAudioQueueProperty_MagicCookie, cookieData, cookieSize);
			free(cookieData);
			if (err) { PRINTERROR("set kAudioQueueProperty_MagicCookie"); break; }

			// listen for kAudioQueueProperty_IsRunning
			err = AudioQueueAddPropertyListener(myData->audioQueue, kAudioQueueProperty_IsRunning, MyAudioQueueIsRunningCallback, myData);
			if (err) { PRINTERROR("AudioQueueAddPropertyListener"); myData->failed = true; break; }
			
			break;
		}
	}
}

void MyPacketsProc(				void *							inClientData,
								UInt32							inNumberBytes,
								UInt32							inNumberPackets,
								const void *					inInputData,
								AudioStreamPacketDescription	*inPacketDescriptions)
{
	// this is called by audio file stream when it finds packets of audio
	MyData* myData = (MyData*)inClientData;
	printf("got data.  bytes: %d  packets: %d\n", (unsigned int)inNumberBytes, (unsigned int)inNumberPackets);

	// the following code assumes we're streaming VBR data. for CBR data, you'd need another code branch here.

	for (int i = 0; i < inNumberPackets; ++i) {
		SInt64 packetOffset = inPacketDescriptions[i].mStartOffset;
		SInt64 packetSize   = inPacketDescriptions[i].mDataByteSize;
		
		// if the space remaining in the buffer is not enough for this packet, then enqueue the buffer.
		size_t bufSpaceRemaining = kAQBufSize - myData->bytesFilled;
		if (bufSpaceRemaining < packetSize) {
			MyEnqueueBuffer(myData);
			WaitForFreeBuffer(myData);
		}
		
		// copy data to the audio queue buffer
		AudioQueueBufferRef fillBuf = myData->audioQueueBuffer[myData->fillBufferIndex];
		memcpy((char*)fillBuf->mAudioData + myData->bytesFilled, (const char*)inInputData + packetOffset, packetSize);
		// fill out packet description
		myData->packetDescs[myData->packetsFilled] = inPacketDescriptions[i];
		myData->packetDescs[myData->packetsFilled].mStartOffset = myData->bytesFilled;
		// keep track of bytes filled and packets filled
		myData->bytesFilled += packetSize;
		myData->packetsFilled += 1;
		
		// if that was the last free packet description, then enqueue the buffer.
		size_t packetsDescsRemaining = kAQMaxPacketDescs - myData->packetsFilled;
		if (packetsDescsRemaining == 0) {
			MyEnqueueBuffer(myData);
			WaitForFreeBuffer(myData);
		}
	}	
}

OSStatus StartQueueIfNeeded(MyData* myData)
{
	OSStatus err = noErr;
	if (!myData->started) {		// start the queue if it has not been started already
		err = AudioQueueStart(myData->audioQueue, NULL);
		if (err) { PRINTERROR("AudioQueueStart"); myData->failed = true; return err; }		
		myData->started = true;
		printf("started\n");
	}
	return err;
}

OSStatus MyEnqueueBuffer(MyData* myData)
{
	OSStatus err = noErr;
	myData->inuse[myData->fillBufferIndex] = true;		// set in use flag
	
	// enqueue buffer
	AudioQueueBufferRef fillBuf = myData->audioQueueBuffer[myData->fillBufferIndex];
	fillBuf->mAudioDataByteSize = myData->bytesFilled;		
	err = AudioQueueEnqueueBuffer(myData->audioQueue, fillBuf, myData->packetsFilled, myData->packetDescs);
	if (err) { PRINTERROR("AudioQueueEnqueueBuffer"); myData->failed = true; return err; }		
	
	StartQueueIfNeeded(myData);
	
	return err;
}


void WaitForFreeBuffer(MyData* myData)
{
	// go to next buffer
	if (++myData->fillBufferIndex >= kNumAQBufs) myData->fillBufferIndex = 0;
	myData->bytesFilled = 0;		// reset bytes filled
	myData->packetsFilled = 0;		// reset packets filled

	// wait until next buffer is not in use
	printf("->lock\n");
	pthread_mutex_lock(&myData->mutex); 
	while (myData->inuse[myData->fillBufferIndex]) {
		printf("... WAITING ...\n");
		pthread_cond_wait(&myData->cond, &myData->mutex);
	}
	pthread_mutex_unlock(&myData->mutex);
	printf("<-unlock\n");
}

int MyFindQueueBuffer(MyData* myData, AudioQueueBufferRef inBuffer)
{
	for (unsigned int i = 0; i < kNumAQBufs; ++i) {
		if (inBuffer == myData->audioQueueBuffer[i]) 
			return i;
	}
	return -1;
}


void MyAudioQueueOutputCallback(	void*					inClientData, 
									AudioQueueRef			inAQ, 
									AudioQueueBufferRef		inBuffer)
{
	// this is called by the audio queue when it has finished decoding our data. 
	// The buffer is now free to be reused.
	MyData* myData = (MyData*)inClientData;
		
	unsigned int bufIndex = MyFindQueueBuffer(myData, inBuffer);
	
	// signal waiting thread that the buffer is free.
	pthread_mutex_lock(&myData->mutex);
	myData->inuse[bufIndex] = false;
	pthread_cond_signal(&myData->cond);
	pthread_mutex_unlock(&myData->mutex);
}

void MyAudioQueueIsRunningCallback(		void*					inClientData, 
										AudioQueueRef			inAQ, 
										AudioQueuePropertyID	inID)
{
	MyData* myData = (MyData*)inClientData;
	
	UInt32 running;
	UInt32 size;
	OSStatus err = AudioQueueGetProperty(inAQ, kAudioQueueProperty_IsRunning, &running, &size);
	if (err) { PRINTERROR("get kAudioQueueProperty_IsRunning"); return; }
	if (!running) {
		pthread_mutex_lock(&myData->mutex);
		pthread_cond_signal(&myData->done);
		pthread_mutex_unlock(&myData->mutex);
	}
}

int MyConnectSocket()
{
	int connection_socket;
	struct hostent *host = gethostbyname("127.0.0.1");
	if (!host) { printf("can't get host\n"); return -1; }
		
	connection_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (connection_socket < 0) { printf("can't create socket\n"); return -1; }
	
	struct sockaddr_in server_sockaddr;
	server_sockaddr.sin_family = host->h_addrtype;
	memcpy(&server_sockaddr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);
	server_sockaddr.sin_port = htons(port);
	
	int err = connect(connection_socket, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr));
	if (err < 0) { printf("can't connect\n"); return -1; }
	
	return connection_socket;
}
