/*
     File: afsserver.cpp
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

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <CoreServices/CoreServices.h>

const int port = 51515;

int main (int argc, char * const argv[]) 
{
	if (argc < 2) {
		printf("Usage: afsend <filename>\n");
		return 1;
	}

	// open the file we are going to stream
	FILE* file = fopen(argv[1], "r");
	if (!file) {
		printf("can't open file '%s'\n", argv[1]);
		return 1;
	}

	// create listener socket
	int listener_socket;
	listener_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listener_socket < 0) {
		printf("can't create listener_socket\n");
		return 1;
	}
	
	// bind listener socket
	struct sockaddr_in server_sockaddr;
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_sockaddr.sin_port = htons(port);
	if (bind(listener_socket, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
		printf("can't bind listener_socket\n");
		return 1;
	}
	
	// begin listening for connections
	listen(listener_socket, 4);
	
	// loop for each connection
	while (true) {
		printf("waiting for connection\n");
		
		struct sockaddr_in client_sockaddr;
		socklen_t client_sockaddr_size = sizeof(client_sockaddr);
		int connection_socket = accept(listener_socket, (struct sockaddr*)&client_sockaddr, &client_sockaddr_size);
		if (connection_socket < 0) {
			printf("accept failed\n");
			continue;
		}
		
		printf("connected\n");
		
		off_t totalSent = 0;
		
		// send out the file
		fseek(file, 0, SEEK_SET); // rewind
		while (true) {
			// read from the file
			char buf[32768];
			size_t bytesRead = fread(buf, 1, 32768, file);
			printf("bytesRead %ld\n", bytesRead);
			
			if (bytesRead == 0) {
				printf("done\n");
				break; // eof
			}
			
			// send to the client
			ssize_t bytesSent = send(connection_socket, buf, bytesRead, 0);
			totalSent += bytesSent;
			printf("  bytesSent %ld  totalSent %qd\n", bytesSent, totalSent);
			if (bytesSent < 0) {
				printf("send failed\n");
				break;
			}
		}
		
		close(connection_socket);
	}

	// ..never gets here..
    return 0;
}
