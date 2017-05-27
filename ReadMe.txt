### AudioFileStreamExample ###

===========================================================================
DESCRIPTION:

The AudioFileStreamExample project provides two targets, a client that receives the streamed data and parses it for the appropriate audio file properties and data, and a server that opens an AudioFile for read and sends the raw bytes over a specified port. The example shows how to utilize the AudioFileStream callback mechanisms to received and process the data provided by the server, and plays the audio data using the AudioQueue API.

===========================================================================
BUILD REQUIREMENTS:

Mac OS X v10.8 or later

===========================================================================
RUNTIME REQUIREMENTS:

Mac OS X v10.8 or later

===========================================================================
PACKAGING LIST:

afsclient.cpp
- The client side code

afsserver.cpp
- The server side code

===========================================================================
CHANGES FROM PREVIOUS VERSIONS:

Version 1.0.1
- First version.
- Updated Xcode Project.

===========================================================================
Copyright (C) 2009-2013 Apple Inc. All rights reserved.
