#include <audio.h>
#include <stdio.h>

audio_struct* audio_;

int	init_audio() {
	audio_ = malloc(sizeof(audio_struct));
		
	if(HIWORD(BASS_GetVersion()) != BASSVERSION) {
		fprintf(stderr,"An incorrect version of BASS.DLL was loaded\n");
		return 0;
	}
	if(!BASS_Init(-1, 44100, BASS_DEVICE_MONO, NULL, NULL)) {
		fprintf(stderr,"CANT INITIALIZE AUDIO DEVICE %d\n",BASS_ErrorGetCode());
		return 0;
	}
	
	audio_->stream = BASS_StreamCreate(30000,1,BASS_SAMPLE_FLOAT,STREAMPROC_PUSH,NULL);
	if(!audio_->stream) {
		fprintf(stderr,"CANT CREATE AUDIO STREAM %d\n",BASS_ErrorGetCode());
		return 0;
	}
	
	if(!BASS_ChannelStart(audio_->stream)) {
		fprintf(stderr,"CAN'T START AUDIO STREAM %d\n",BASS_ErrorGetCode());
		return 0;
	}
	
	return 1;
}
void destroy_audio() {
	BASS_Free();
	free(audio_);
}