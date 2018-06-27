/*

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
#define RESAMP_BUFFER_LEN 512   // Playback/resamp BUFFER LENGTH
#define SRC_BUFFER_LEN 220500 // Source buffer length

using namespace std;

string gFilename = "ella-mono.wav";
int gNumFramesInFile;
static SRC_STATE *audioSrc = NULL;
double gResampRatio = 1.23333;

// Two buffers for each channel:
// one of them loads the next chunk of audio while the other one is used for playback
SampleData gSampleBuf[2][NUM_CHANNELS];

float gSrcBuffer[SRC_BUFFER_LEN];
float gResampBuffer[2][RESAMP_BUFFER_LEN]; //two buffers to allow playback while the other loads

// read pointer relative current buffer (range 0-BUFFER_LEN)
// initialise at BUFFER_LEN to pre-load second buffer (see render())
int gReadPtr = 0;
// keeps track of which buffer is currently active (switches between 0 and 1)
int gActiveBuffer = 0;
// this variable will let us know if the buffer doesn't manage to load in time
int gDoneLoadingBuffer = 1;

SNDFILE *sndfile ;
SF_INFO sfinfo ;

AuxiliaryTask gFillBufferTask;

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
	
}

void fillBuffer(void*) {
   	//rt_printf("\nread %i samples", 
   	src_callback_read(audioSrc, gResampRatio, RESAMP_BUFFER_LEN, gResampBuffer[!gActiveBuffer]);
    gDoneLoadingBuffer = 1;
}


bool setup(BelaContext *context, void *userData)
{
    
    // Initialise auxiliary tasks
	if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0)
		return false;
	
	int err;
	
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
	
    gNumFramesInFile = getNumFrames(gFilename);
    
    printf("File contains %i frames", gNumFramesInFile);

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
            Bela_scheduleAuxiliaryTask(gFillBufferTask);
            gActiveBuffer = !gActiveBuffer;
        }
        
        src_set_ratio(audioSrc, gResampRatio);

    	for(unsigned int channel = 0; channel < context->audioOutChannels; channel++) {
    	    // Wrap channel index in case there are more audio output channels than the file contains
    	    // float out = gSampleBuf[gActiveBuffer][channel%NUM_CHANNELS].samples[gReadPtr];
    	    float out = gResampBuffer[gActiveBuffer][gReadPtr];
    		audioWrite(context, n, channel, out);
    	}
    	
    }
}


void cleanup(BelaContext *context, void *userData)
{
    // TODO Delete the allocated buffers
}
