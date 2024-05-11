#ifndef MUX_H
#define MUX_H

struct audio_data {
	float* data;
	int samples;
};

struct audio_data* load_mp3(char*);
int init_record(char*,int,int,float**);
int add_frame();
void stop_record();

#endif //MUX_H
