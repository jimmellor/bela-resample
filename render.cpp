/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

The platform for ultra-low latency audio and sensor processing

http://bela.io

A project of the Augmented Instruments Laboratory within the
Centre for Digital Music at Queen Mary University of London.
http://www.eecs.qmul.ac.uk/~andrewm

(c) 2016 Augmented Instruments Laboratory: Andrew McPherson,
  Astrid Bin, Liam Donovan, Christian Heinrichs, Robert Jack,
  Giulio Moro, Laurel Pardue, Victor Zappi. All rights reserved.

The Bela software is distributed under the GNU Lesser General Public License
(LGPL 3.0), available here: https://www.gnu.org/licenses/lgpl-3.0.txt
*/


#include <Bela.h>
#include <cmath>
#include <string>

#include <SampleData.h>
#include <sndfile.h>		
#include <iostream>
#include <cstdlib>
#include <resample.h>
#include <Scope.h>

#define NUM_CHANNELS 1    // NUMBER OF CHANNELS IN THE FILE
#define BUFFER_LEN 22050   // Playback/resamp BUFFER LENGTH
#define RESAMP_BUFFER_LEN 1024   // Playback/resamp BUFFER LENGTH
#define SRC_BUFFER_LEN 220500 // Source buffer length


using namespace std;

string gFilename = "ella-mono.wav";
int gNumFramesInFile;
static SRC_STATE *audioSrc = NULL;
double gResampRatio = 3.0;

// Two buffers for each channel:
// one of them loads the next chunk of audio while the other one is used for playback
SampleData gSampleBuf[2][NUM_CHANNELS];

float gSrcBuffer[SRC_BUFFER_LEN];
float gResampBuffer[RESAMP_BUFFER_LEN];

// read pointer relative current buffer (range 0-BUFFER_LEN)
// initialise at BUFFER_LEN to pre-load second buffer (see render())
int gReadPtr = 0;
// read pointer relative to file, increments by BUFFER_LEN (see fillBuffer())
int gBufferReadPtr = 0;
// keeps track of which buffer is currently active (switches between 0 and 1)
int gActiveBuffer = 0;
// this variable will let us know if the buffer doesn't manage to load in time
int gDoneLoadingBuffer = 1;

SNDFILE *sndfile ;
SF_INFO sfinfo ;

AuxiliaryTask gFillBufferTask;

int getSamples(string file, float *buf, int channel, int startFrame, int endFrame)
{
	SNDFILE *sndfile ;
	SF_INFO sfinfo ;
	sfinfo.format = 0;
	if (!(sndfile = sf_open (file.c_str(), SFM_READ, &sfinfo))) {
		cout << "Couldn't open file " << file << ": " << sf_strerror(sndfile) << endl;
		return 1;
	}

	int numChannelsInFile = sfinfo.channels;
	if(numChannelsInFile < channel+1)
	{
		cout << "Error: " << file << " doesn't contain requested channel" << endl;
		return 1;
	}
    
    int frameLen = endFrame-startFrame;
    
    if(frameLen <= 0 || startFrame < 0 || endFrame <= 0 || endFrame > sfinfo.frames)
	{
	    cout << "Error: " << file << " invalid frame range requested" << endl;
		return 1;
	}
    
    sf_seek(sndfile,startFrame,SEEK_SET);
    
    float* tempBuf = new float[frameLen*numChannelsInFile];
    
	int subformat = sfinfo.format & SF_FORMAT_SUBMASK;
	int readcount = sf_read_float(sndfile, tempBuf, frameLen*numChannelsInFile); //FIXME

	// Pad with zeros in case we couldn't read whole file
	for(int k = readcount; k <frameLen*numChannelsInFile; k++)
		tempBuf[k] = 0;

	if (subformat == SF_FORMAT_FLOAT || subformat == SF_FORMAT_DOUBLE) {
		double	scale ;
		int 	m ;

		sf_command (sndfile, SFC_CALC_SIGNAL_MAX, &scale, sizeof (scale)) ;
		if (scale < 1e-10)
			scale = 1.0 ;
		else
			scale = 32700.0 / scale ;
		cout << "File samples scale = " << scale << endl;

		for (m = 0; m < frameLen; m++)
			tempBuf[m] *= scale;
	}
	
	for(int n=0;n<frameLen;n++)
	    buf[n] = tempBuf[n*numChannelsInFile+channel];

	sf_close(sndfile);

	return 0;
}

int getNumChannels(string file) {
    
    SNDFILE *sndfile ;
	SF_INFO sfinfo ;
	sfinfo.format = 0;
	if (!(sndfile = sf_open (file.c_str(), SFM_READ, &sfinfo))) {
		cout << "Couldn't open file " << file << ": " << sf_strerror(sndfile) << endl;
		return -1;
	}

	return sfinfo.channels;
}

int getNumFrames(string file) {
    
    SNDFILE *sndfile ;
	SF_INFO sfinfo ;
	sfinfo.format = 0;
	if (!(sndfile = sf_open (file.c_str(), SFM_READ, &sfinfo))) {
		cout << "Couldn't open file " << file << ": " << sf_strerror(sndfile) << endl;
		return -1;
	}

	return sfinfo.frames;
}

int readAudio(float *buffer, int samples){
	return sf_read_float(sndfile, buffer, samples);
}

long srcCallback(void *cb_data, float **data) {
	
	int readFrames = (round(float(RESAMP_BUFFER_LEN) / gResampRatio)+20);
	//rt_printf("\nReadFrames = %i",readFrames);
	readAudio(gSrcBuffer,(readFrames));
	*data = gSrcBuffer;
	return readFrames;
 //   // reset buffer pointer if it exceeds the number of frames in the file
 //   if(gBufferReadPtr>=gNumFramesInFile)
 //       gBufferReadPtr=0;
   	
 //   int endFrame = gBufferReadPtr + readFrames;
 //   rt_printf("\ngBufferReadPtr = %i",gBufferReadPtr);
 //   rt_printf("\nendFrame = %i",endFrame);
 //   int zeroPad = 0;
    
 //   // if reaching the end of the file take note of the last frame index
 //   // so we can zero-pad the rest later
 //   if((gBufferReadPtr+readFrames)>=gNumFramesInFile-1) {
 //         endFrame = gNumFramesInFile-1;
 //         zeroPad = 1;
 //   }
    
 //   // fill (nonactive) buffer
 //   getSamples(gFilename,gSampleBuf[!gActiveBuffer][0].samples,0,gBufferReadPtr,endFrame);
                    
 //   // zero-pad if necessary
 //   if(zeroPad) {
 //       int numFramesToPad = readFrames - (endFrame-gBufferReadPtr);
 //       for(int n=0;n<numFramesToPad;n++)
 //           gSampleBuf[!gActiveBuffer][0].samples[n+(readFrames-numFramesToPad)] = 0;
 //   }
    
 //   gBufferReadPtr = endFrame;
    
 //   // orginal code
	// *data = gSampleBuf[!gActiveBuffer][0].samples; // pointer to sample data
	// return gSampleBuf[!gActiveBuffer][0].sampleLen; // length of the sample data buffer
	
}

void fillBuffer(void*) {
   	//rt_printf("\nread %i samples", 
   	src_callback_read(audioSrc, gResampRatio, RESAMP_BUFFER_LEN, gResampBuffer);
    gDoneLoadingBuffer = 1;
}

void fillBufferOld(void*) {
    
    // increment buffer read pointer by buffer length
    gBufferReadPtr+=BUFFER_LEN;
    
    // reset buffer pointer if it exceeds the number of frames in the file
    if(gBufferReadPtr>=gNumFramesInFile)
        gBufferReadPtr=0;
    
    int endFrame = gBufferReadPtr + BUFFER_LEN;
    int zeroPad = 0;
    
    // if reaching the end of the file take note of the last frame index
    // so we can zero-pad the rest later
    if((gBufferReadPtr+BUFFER_LEN)>=gNumFramesInFile-1) {
          endFrame = gNumFramesInFile-1;
          zeroPad = 1;
    }
    
    // for(int ch=0;ch<NUM_CHANNELS;ch++) {
        
    //     // fill (nonactive) buffer
    //     getSamples(gFilename,gSampleBuf[!gActiveBuffer][ch].samples,ch
    //                 ,gBufferReadPtr,endFrame);
                    
    //     // zero-pad if necessary
    //     if(zeroPad) {
    //         int numFramesToPad = BUFFER_LEN - (endFrame-gBufferReadPtr);
    //         for(int n=0;n<numFramesToPad;n++)
    //             gSampleBuf[!gActiveBuffer][ch].samples[n+(BUFFER_LEN-numFramesToPad)] = 0;
    //     }
        
    // }
    
    //readAudio(gSampleBuf[!gActiveBuffer][0].samples,BUFFER_LEN);
    readAudio(gSrcBuffer,BUFFER_LEN);
    gDoneLoadingBuffer = 1;
    
    //printf("done loading buffer!\n");
    
}

bool setup(BelaContext *context, void *userData)
{
    
    // Initialise auxiliary tasks
	if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0)
		return false;
	
	int err;
	
    // getNumFrames() and getSamples() are helper functions for getting data from wav files declared in SampleLoader.h
    // SampleData is a struct that contains an array of floats and an int declared in SampleData.h
    
    // Setup resampler
    audioSrc = src_callback_new(srcCallback, SRC_LINEAR, 1, &err, NULL);
    
    // load the sound file
    
	sfinfo.format = 0;
	if (!(sndfile = sf_open (gFilename.c_str(), SFM_READ, &sfinfo))) {
		cout << "Couldn't open file " << gFilename << ": " << sf_strerror(sndfile) << endl;
		return 1;
	}

	int numChannelsInFile = sfinfo.channels;
	
	printf("File contains %i channel(s)", numChannelsInFile);
	
	// check that the soundfile includes the channel we're looking for
	// if(numChannelsInFile < channel+1)
	// {
	// 	cout << "Error: " << file << " doesn't contain requested channel" << endl;
	// 	return 1;
	// }
    
    gNumFramesInFile = getNumFrames(gFilename);
    
    printf("File contains %i frames", gNumFramesInFile);
    
    // if(gNumFramesInFile <= 0)
    //     return false;
    
    // if(gNumFramesInFile <= BUFFER_LEN) {
    //     printf("Sample needs to be longer than buffer size. This example is intended to work with long samples.");
    //     return false;
    // }
    
    for(int ch=0;ch<NUM_CHANNELS;ch++) {
        for(int i=0;i<2;i++) {
            gSampleBuf[i][ch].sampleLen = BUFFER_LEN;
        	gSampleBuf[i][ch].samples = new float[BUFFER_LEN];
            if(getSamples(gFilename,gSampleBuf[i][ch].samples,ch,0,BUFFER_LEN))
                return false;
        }
    }

	return true;
}

void render(BelaContext *context, void *userData)
{
    for(unsigned int n = 0; n < context->audioFrames; n++) {
        
        // Increment read pointer and reset to 0 when end of file is reached
        if(++gReadPtr >= RESAMP_BUFFER_LEN) {
            if(!gDoneLoadingBuffer)
                rt_printf("Couldn't load buffer in time :( -- try increasing buffer size!");
            gDoneLoadingBuffer = 0;
            gReadPtr = 0;
            //gActiveBuffer = !gActiveBuffer;
            Bela_scheduleAuxiliaryTask(gFillBufferTask);
        }
        
        //src_set_ratio(audioSrc, gResampRatio);

    	for(unsigned int channel = 0; channel < context->audioOutChannels; channel++) {
    	    // Wrap channel index in case there are more audio output channels than the file contains
    	    // float out = gSampleBuf[gActiveBuffer][channel%NUM_CHANNELS].samples[gReadPtr];
    	    float out = gResampBuffer[gReadPtr];
    		audioWrite(context, n, channel, out);
    	}
    	
    }
}


void cleanup(BelaContext *context, void *userData)
{
    // Delete the allocated buffers
    for(int ch=0;ch<NUM_CHANNELS;ch++) {
        for(int i=0;i<2;i++)
        	delete[] gSampleBuf[i][ch].samples;
    }
}


/**
\example sample-streamer/render.cpp

Playback of large wav files
---------------------------

When dealing with large wav files it is usually not a good idea to load the
entire file into memory. This example shows how to use two buffers to continually
load chunks of the file, thus allowing playback of very large files. While one buffer
is being used for playback the other buffer is being filled with the next chunk
of samples.

In order to do this, an AuxiliaryTask is used to load the file into the inactive
buffer without interrupting the audio thread. We can set the global variable
`gDoneLoadingBuffer` to 1 each time the buffer has finished loading, allowing us
to detect cases were the buffers havn't been filled in time. These cases can usually
be mitigated by using a larger buffer size.

Try uploading a large wav file into the project and playing it back. You will need
specify the amount of channels (`#``define NUM_CHANNELS`) and the name of the file (`gFilename`).


*/
